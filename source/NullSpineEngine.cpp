#include "nullspine/NullSpineEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nullspine
{

namespace
{
constexpr float ceiling = 0.98f;
constexpr float silenceThreshold = 1.0e-8f;
constexpr float denormalThreshold = 1.0e-20f;
constexpr std::array<float, 8> harmonicRatios {
    1.0f, 1.503f, 1.997f, 2.414f, 3.01f, 3.702f, 4.618f, 5.391f
};
constexpr std::array<float, 8> baseGains {
    0.16f, 0.12f, 0.095f, 0.08f, 0.062f, 0.048f, 0.036f, 0.028f
};

float midiNoteToFrequency (int noteNumber, float offsetSemitones) noexcept
{
    const auto semitones = static_cast<float> (std::clamp (noteNumber, 0, 127) - 69) + offsetSemitones;
    return 440.0f * std::pow (2.0f, semitones / 12.0f);
}

float sanitizeState (float value) noexcept
{
    if (! std::isfinite (value) || std::fabs (value) < denormalThreshold)
        return 0.0f;
    return value;
}
} // namespace

NullSpineEngine::NullSpineEngine()
{
    prepare (44100.0);
    reset (1u);
}

void NullSpineEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0 ? newSampleRate : 44100.0;
    exciterLengthSamples = std::clamp (static_cast<int> (sampleRate * 0.00055), 8, 48);
    tuneResonators();
}

void NullSpineEngine::reset (std::uint32_t seed) noexcept
{
    baseSeed = seed != 0u ? seed : 1u;
    currentNote = -1;
    currentFrequency = midiNoteToFrequency (48, params.pitchOffsetSemitones);
    active = false;
    released = true;
    velocity = 0.0f;
    exciterSamplesRemaining = 0;
    toneLeft = 0.0f;
    toneRight = 0.0f;

    exciterNoise.reset (mixSeed (baseSeed ^ 0x6a09e667u));
    textureNoise.reset (mixSeed (baseSeed ^ 0xbb67ae85u));
    for (auto& resonator : resonators)
    {
        resonator.z1 = 0.0f;
        resonator.z2 = 0.0f;
    }
    tuneResonators();
}

void NullSpineEngine::setParameters (const NullSpineParameters& parameters) noexcept
{
    params.pitchOffsetSemitones = clampFinite (parameters.pitchOffsetSemitones, -24.0f, 24.0f, NullSpineParameters {}.pitchOffsetSemitones);
    params.spine = clampFinite (parameters.spine, 0.0f, 1.0f, NullSpineParameters {}.spine);
    params.decaySeconds = clampFinite (parameters.decaySeconds, 0.03f, 6.0f, NullSpineParameters {}.decaySeconds);
    params.fold = clampFinite (parameters.fold, 0.0f, 1.0f, NullSpineParameters {}.fold);
    params.tone = clampFinite (parameters.tone, 0.0f, 1.0f, NullSpineParameters {}.tone);
    params.outputGain = clampFinite (parameters.outputGain, 0.0f, 2.0f, NullSpineParameters {}.outputGain);

    if (currentNote >= 0)
        currentFrequency = midiNoteToFrequency (currentNote, params.pitchOffsetSemitones);
    tuneResonators();
}

void NullSpineEngine::noteOn (int noteNumber, float newVelocity) noexcept
{
    const auto incomingNote = clampNote (noteNumber);
    const auto incomingVelocity = clampFinite (newVelocity, 0.0f, 1.0f, 1.0f);

    if (incomingVelocity <= 0.0f)
    {
        noteOff (incomingNote);
        return;
    }

    currentNote = incomingNote;
    currentFrequency = midiNoteToFrequency (incomingNote, params.pitchOffsetSemitones);
    velocity = incomingVelocity;
    active = true;
    released = false;
    exciterSamplesRemaining = exciterLengthSamples;

    const auto noteSeed = mixSeed (baseSeed ^ (static_cast<std::uint32_t> (incomingNote) * 0x45d9f3bu));
    exciterNoise.reset (mixSeed (noteSeed ^ 0x3c6ef372u));
    textureNoise.reset (mixSeed (noteSeed ^ 0xa54ff53au));
    for (auto& resonator : resonators)
    {
        resonator.z1 = 0.0f;
        resonator.z2 = 0.0f;
    }
    toneLeft = 0.0f;
    toneRight = 0.0f;
    tuneResonators();

    for (auto& resonator : resonators)
    {
        resonator.z1 = (exciterNoise.nextFloat() + 1.0f) * std::numbers::pi_v<float>;
        resonator.z2 = (0.38f + 0.24f * exciterNoise.nextFloat()) * resonator.gain * velocity;
    }
}

void NullSpineEngine::noteOff (int noteNumber) noexcept
{
    const auto safeNote = clampNote (noteNumber);
    if (currentNote == safeNote)
    {
        released = true;
        tuneResonators();
    }
}

StereoFrame NullSpineEngine::processSample() noexcept
{
    if (! active)
        return {};

    const auto excitation = nextExciterSample();
    auto frame = processResonators (excitation);

    frame.left = foldSample (shapeTone (frame.left, toneLeft)) * params.outputGain;
    frame.right = foldSample (shapeTone (frame.right, toneRight)) * params.outputGain;
    frame = sanitizeFrame (frame.left, frame.right);

    flushDenormals();
    if (released && exciterSamplesRemaining <= 0 && resonatorsAreSilent())
    {
        active = false;
        currentNote = -1;
        velocity = 0.0f;
        return {};
    }

    return frame;
}

void NullSpineEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample();
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

std::uint32_t NullSpineEngine::mixSeed (std::uint32_t value) noexcept
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value != 0u ? value : 0x6d2b79f5u;
}

int NullSpineEngine::clampNote (int noteNumber) noexcept
{
    return std::clamp (noteNumber, 0, 127);
}

void NullSpineEngine::tuneResonators() noexcept
{
    const auto safeRate = static_cast<float> (sampleRate);
    const auto nyquistLimit = safeRate * 0.46f;
    const auto inharmonicity = 0.018f + params.spine * 0.17f;
    const auto chainSpread = 0.65f + params.spine * 0.75f;

    for (std::size_t i = 0; i < resonators.size(); ++i)
    {
        auto& resonator = resonators[i];
        const auto index = static_cast<float> (i);
        const auto bentRatio = harmonicRatios[i] * (1.0f + inharmonicity * index * index * 0.11f);
        const auto frequency = std::clamp (currentFrequency * bentRatio, 18.0f, nyquistLimit);
        const auto omega = 2.0f * std::numbers::pi_v<float> * frequency / safeRate;
        const auto modeDecay = params.decaySeconds * (1.25f - std::min (0.62f, index * 0.055f + params.spine * 0.22f));
        const auto releaseScale = released ? 0.75f : 1.0f;
        const auto radius = std::exp (-1.0f / std::max (1.0f, safeRate * modeDecay * releaseScale));

        resonator.radius = std::clamp (radius, 0.0f, 0.999995f);
        resonator.coefficient = omega;
        resonator.gain = baseGains[i] * (1.0f + params.spine * (index / static_cast<float> (resonators.size()))) * chainSpread;
        resonator.pan = ((i & 1u) == 0u ? -1.0f : 1.0f) * (0.08f + params.spine * 0.24f + index * 0.018f);
    }
}

float NullSpineEngine::nextExciterSample() noexcept
{
    if (exciterSamplesRemaining <= 0)
        return 0.0f;

    const auto age = exciterLengthSamples - exciterSamplesRemaining;
    const auto phase = static_cast<float> (age) / static_cast<float> (std::max (1, exciterLengthSamples - 1));
    --exciterSamplesRemaining;

    const auto click = age == 0 ? 1.0f : 0.0f;
    const auto shapedPulse = std::sin ((1.0f - phase) * std::numbers::pi_v<float>) * (1.0f - phase);
    const auto grit = exciterNoise.nextFloat() * (0.16f + 0.30f * params.spine);
    const auto texture = textureNoise.nextBinary() * 0.055f * (1.0f - params.tone);
    return (click + shapedPulse * 0.62f + grit + texture) * velocity;
}

StereoFrame NullSpineEngine::processResonators (float excitation) noexcept
{
    float left = 0.0f;
    float right = 0.0f;
    auto chainFeed = excitation;

    for (auto& resonator : resonators)
    {
        resonator.z2 += (excitation * resonator.gain + chainFeed * params.spine * 0.018f);
        resonator.z2 = std::isfinite (resonator.z2) ? std::clamp (resonator.z2, -4.0f, 4.0f) : 0.0f;
        const auto sample = std::sin (resonator.z1) * resonator.z2;
        resonator.z1 += resonator.coefficient;
        if (resonator.z1 >= 2.0f * std::numbers::pi_v<float>)
            resonator.z1 -= 2.0f * std::numbers::pi_v<float>;
        resonator.z2 *= resonator.radius;
        chainFeed = sample;

        const auto leftGain = 0.5f * (1.0f - resonator.pan);
        const auto rightGain = 0.5f * (1.0f + resonator.pan);
        left += sample * leftGain;
        right += sample * rightGain;
    }

    return { left * 0.18f, right * 0.18f };
}

float NullSpineEngine::shapeTone (float input, float& state) const noexcept
{
    const auto cutoff = 220.0f + params.tone * params.tone * 7600.0f;
    const auto coefficient = std::exp (-2.0f * std::numbers::pi_v<float> * cutoff / static_cast<float> (sampleRate));
    state = sanitizeState ((1.0f - coefficient) * input + coefficient * state);
    const auto high = input - state;
    return state + high * (0.18f + params.tone * 0.82f);
}

float NullSpineEngine::foldSample (float input) const noexcept
{
    const auto drive = 1.0f + params.fold * 9.0f;
    auto x = std::clamp (std::isfinite (input) ? input * drive : 0.0f, -2.0f, 2.0f);
    if (x > 1.0f)
        x = 2.0f - x;
    else if (x < -1.0f)
        x = -2.0f - x;
    return boundedDrive (x, 1.0f + params.fold * 0.8f);
}

StereoFrame NullSpineEngine::sanitizeFrame (float left, float right) const noexcept
{
    const auto safeLeft = std::isfinite (left) ? left : 0.0f;
    const auto safeRight = std::isfinite (right) ? right : 0.0f;
    return { std::clamp (safeLeft, -ceiling, ceiling),
             std::clamp (safeRight, -ceiling, ceiling) };
}

bool NullSpineEngine::resonatorsAreSilent() const noexcept
{
    float peak = std::max (std::fabs (toneLeft), std::fabs (toneRight));
    for (const auto& resonator : resonators)
        peak = std::max (peak, std::fabs (resonator.z2));
    return peak <= silenceThreshold;
}

void NullSpineEngine::flushDenormals() noexcept
{
    toneLeft = sanitizeState (toneLeft);
    toneRight = sanitizeState (toneRight);
    for (auto& resonator : resonators)
    {
        if (! std::isfinite (resonator.z1))
            resonator.z1 = 0.0f;
        resonator.z2 = sanitizeState (resonator.z2);
    }
}

} // namespace nullspine
