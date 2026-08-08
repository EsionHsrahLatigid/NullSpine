#pragma once

#include "nullspine/NullSpineDspPrimitives.h"

#include <array>
#include <cstdint>

namespace nullspine
{

/** Realtime-safe parameter set for the NullSpine impulse-resonator instrument.

    All values are sanitized by setParameters():
    pitchOffsetSemitones [-24, 24], spine [0, 1], decaySeconds [0.03, 6],
    fold [0, 1], tone [0, 1], outputGain [0, 2].
*/
struct NullSpineParameters
{
    float pitchOffsetSemitones = 0.0f;
    float spine = 0.56f;
    float decaySeconds = 1.2f;
    float fold = 0.34f;
    float tone = 0.62f;
    float outputGain = 0.72f;
};

/** Monophonic MIDI impulse into a deterministic tuned resonator spine.

    noteOn() seeds a short exciter impulse and retunes the damped resonator
    bank. processSample() and process() allocate no memory and always return
    finite, ceiling-bounded samples.
*/
class NullSpineEngine
{
public:
    NullSpineEngine();

    /** Sets the sample rate and rebuilds filters; invalid rates fall back to 44.1 kHz. */
    void prepare (double sampleRate) noexcept;

    /** Clears state and sets the deterministic base seed used by future noteOn calls. */
    void reset (std::uint32_t seed = 1u) noexcept;

    /** Applies sanitized parameters and updates filter/envelope coefficients. */
    void setParameters (const NullSpineParameters& parameters) noexcept;

    /** Starts or retriggers the monophonic note, with velocity clamped to [0, 1]. */
    void noteOn (int noteNumber, float velocity) noexcept;

    /** Releases the current note; mismatched note numbers are ignored while another note is held. */
    void noteOff (int noteNumber) noexcept;

    /** Renders one stereo frame. Silent before noteOn and after envelope decay. */
    [[nodiscard]] StereoFrame processSample() noexcept;

    /** Renders numSamples into stereo buffers when both pointers are valid. */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    struct ClampedParameters
    {
        float pitchOffsetSemitones = 0.0f;
        float spine = 0.56f;
        float decaySeconds = 1.2f;
        float fold = 0.34f;
        float tone = 0.62f;
        float outputGain = 0.72f;
    };

    struct Resonator
    {
        float coefficient = 0.0f;
        float radius = 0.0f;
        float gain = 0.0f;
        float pan = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    static std::uint32_t mixSeed (std::uint32_t value) noexcept;
    static int clampNote (int noteNumber) noexcept;

    void tuneResonators() noexcept;
    [[nodiscard]] float nextExciterSample() noexcept;
    [[nodiscard]] StereoFrame processResonators (float excitation) noexcept;
    [[nodiscard]] float shapeTone (float input, float& state) const noexcept;
    [[nodiscard]] float foldSample (float input) const noexcept;
    [[nodiscard]] StereoFrame sanitizeFrame (float left, float right) const noexcept;
    [[nodiscard]] bool resonatorsAreSilent() const noexcept;
    void flushDenormals() noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    std::uint32_t baseSeed = 1u;
    int currentNote = -1;
    float currentFrequency = 130.81278f;

    static constexpr std::size_t numResonators = 8;
    std::array<Resonator, numResonators> resonators {};
    DeterministicNoise exciterNoise;
    DeterministicNoise textureNoise;

    bool active = false;
    bool released = true;
    float velocity = 0.0f;
    int exciterSamplesRemaining = 0;
    int exciterLengthSamples = 16;
    float toneLeft = 0.0f;
    float toneRight = 0.0f;
};

} // namespace nullspine
