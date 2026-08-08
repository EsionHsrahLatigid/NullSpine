#include "nullspine/NullSpineEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using nullspine::NullSpineEngine;
using nullspine::NullSpineParameters;

namespace
{
constexpr double testSampleRate = 48000.0;
constexpr int releaseSample = 1024;

struct RenderedStereo
{
    std::vector<float> left;
    std::vector<float> right;
};

RenderedStereo renderNote (std::uint32_t seed, int note, float velocity, NullSpineParameters params, int samples, bool release = false)
{
    NullSpineEngine engine;
    engine.prepare (testSampleRate);
    engine.setParameters (params);
    engine.reset (seed);
    engine.noteOn (note, velocity);

    RenderedStereo output;
    output.left.reserve (static_cast<std::size_t> (samples));
    output.right.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        if (release && i == releaseSample)
            engine.noteOff (note);
        const auto frame = engine.processSample();
        output.left.push_back (frame.left);
        output.right.push_back (frame.right);
    }

    return output;
}

float maxAbs (const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (const auto sample : samples)
        peak = std::max (peak, std::fabs (sample));
    return peak;
}

float maxAbs (const RenderedStereo& samples)
{
    return std::max (maxAbs (samples.left), maxAbs (samples.right));
}

float rms (const std::vector<float>& samples, std::size_t begin, std::size_t end)
{
    end = std::min (end, samples.size());
    float energy = 0.0f;
    for (auto i = begin; i < end; ++i)
        energy += samples[i] * samples[i];
    return begin < end ? std::sqrt (energy / static_cast<float> (end - begin)) : 0.0f;
}

void assertStereoRmsAtLeast (const RenderedStereo& samples, float threshold)
{
    assert (rms (samples.left, 0, samples.left.size()) >= threshold);
    assert (rms (samples.right, 0, samples.right.size()) >= threshold);
}

void assertStereoFiniteAndBounded (const RenderedStereo& samples)
{
    assert (samples.left.size() == samples.right.size());
    for (std::size_t i = 0; i < samples.left.size(); ++i)
    {
        assert (std::isfinite (samples.left[i]));
        assert (std::isfinite (samples.right[i]));
        assert (samples.left[i] >= -0.9801f && samples.left[i] <= 0.9801f);
        assert (samples.right[i] >= -0.9801f && samples.right[i] <= 0.9801f);
    }
}

float resonantEnergy (const std::vector<float>& samples, float frequency)
{
    const auto omega = 2.0f * 3.14159265358979323846f * frequency / static_cast<float> (testSampleRate);
    float real = 0.0f;
    float imag = 0.0f;

    for (std::size_t i = 2048; i < samples.size(); ++i)
    {
        const auto phase = omega * static_cast<float> (i);
        real += samples[i] * std::cos (phase);
        imag -= samples[i] * std::sin (phase);
    }

    return real * real + imag * imag;
}

float zeroCrossingRate (const std::vector<float>& samples, std::size_t begin, std::size_t end)
{
    end = std::min (end, samples.size());
    int crossings = 0;
    for (auto i = begin + 1; i < end; ++i)
        crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f) ? 1 : 0;
    return begin + 1 < end ? static_cast<float> (crossings) / static_cast<float> (end - begin - 1) : 0.0f;
}

float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    assert (a.size() == b.size());
    float result = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        result = std::max (result, std::fabs (a[i] - b[i]));
    return result;
}

float maxDiff (const RenderedStereo& a, const RenderedStereo& b)
{
    assert (a.left.size() == b.left.size());
    assert (a.right.size() == b.right.size());
    return std::max (maxDiff (a.left, b.left), maxDiff (a.right, b.right));
}

int releaseTailHorizonSamples (float decaySeconds)
{
    const auto horizonSeconds = std::max (4.0f, decaySeconds * 14.0f);
    return static_cast<int> (std::ceil (horizonSeconds * static_cast<float> (testSampleRate)));
}

void testSilentBeforeTrigger()
{
    NullSpineEngine engine;
    engine.prepare (testSampleRate);
    engine.reset (123u);

    for (int i = 0; i < 2048; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

void testDefaultTriggerHasEnergyAndBounds()
{
    NullSpineParameters params;
    const auto samples = renderNote (77u, 48, 1.0f, params, 8192);

    assertStereoRmsAtLeast (samples, 1.0e-4f);
    assert (maxAbs (samples) <= 0.9801f);
    assertStereoFiniteAndBounded (samples);
}

void testDeterministicSameSeedWithinTolerance()
{
    NullSpineParameters params;
    params.spine = 0.73f;
    params.fold = 0.41f;

    const auto a = renderNote (4242u, 52, 0.8f, params, 12000);
    const auto b = renderNote (4242u, 52, 0.8f, params, 12000);
    assert (maxDiff (a, b) <= 1.0e-6f);
}

void testNoteTunedModalTail()
{
    NullSpineParameters params;
    params.decaySeconds = 2.2f;
    params.spine = 0.44f;
    params.fold = 0.0f;

    const auto low = renderNote (99u, 48, 1.0f, params, 24000);
    const auto high = renderNote (99u, 60, 1.0f, params, 24000);

    const auto lowFundamentalLeft = resonantEnergy (low.left, 130.81278f);
    const auto lowOffTargetLeft = resonantEnergy (low.left, 184.99721f);
    const auto lowFundamentalRight = resonantEnergy (low.right, 130.81278f);
    const auto lowOffTargetRight = resonantEnergy (low.right, 184.99721f);
    assert (lowFundamentalLeft > lowOffTargetLeft * 1.8f);
    assert (lowFundamentalRight > lowOffTargetRight * 1.8f);
    assert (maxDiff (low, high) > 0.01f);
    assert (rms (low.left, 9000, 18000) > 1.0e-4f);
    assert (rms (low.right, 9000, 18000) > 1.0e-4f);
}

void testSpineChangesModalIdentity()
{
    NullSpineParameters straight;
    straight.spine = 0.0f;
    straight.decaySeconds = 1.8f;
    straight.fold = 0.0f;

    auto fractured = straight;
    fractured.spine = 1.0f;

    const auto a = renderNote (5150u, 45, 1.0f, straight, 18000);
    const auto b = renderNote (5150u, 45, 1.0f, fractured, 18000);

    assert (maxDiff (a, b) > 0.01f);
    assert (std::fabs (zeroCrossingRate (a.left, 3000, 16000) - zeroCrossingRate (b.left, 3000, 16000)) > 0.0005f);
    assert (std::fabs (zeroCrossingRate (a.right, 3000, 16000) - zeroCrossingRate (b.right, 3000, 16000)) > 0.0005f);
}

void testDecayControlsTailPersistence()
{
    NullSpineParameters shortDecay;
    shortDecay.decaySeconds = 0.08f;
    shortDecay.fold = 0.0f;
    shortDecay.outputGain = 1.0f;

    auto longDecay = shortDecay;
    longDecay.decaySeconds = 2.8f;

    const auto shortTail = renderNote (991u, 50, 1.0f, shortDecay, 36000, true);
    const auto longTail = renderNote (991u, 50, 1.0f, longDecay, 36000, true);

    const auto shortLeftRms = rms (shortTail.left, 8000, 24000);
    const auto shortRightRms = rms (shortTail.right, 8000, 24000);
    const auto longLeftRms = rms (longTail.left, 8000, 24000);
    const auto longRightRms = rms (longTail.right, 8000, 24000);
    assert (longLeftRms > shortLeftRms * 6.0f);
    assert (longRightRms > shortRightRms * 6.0f);
}

void assertReleaseTailFallsBelowThresholdAtHorizon (float decaySeconds)
{
    NullSpineParameters params;
    params.decaySeconds = decaySeconds;
    params.outputGain = 1.0f;

    NullSpineEngine engine;
    engine.prepare (testSampleRate);
    engine.setParameters (params);
    engine.reset (777u);
    engine.noteOn (40, 1.0f);

    for (int i = 0; i < releaseSample; ++i)
        (void) engine.processSample();
    engine.noteOff (40);

    for (int i = 0; i < releaseTailHorizonSamples (decaySeconds); ++i)
        (void) engine.processSample();

    for (int i = 0; i < 2048; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::fabs (frame.left) <= 1.0e-5f);
        assert (std::fabs (frame.right) <= 1.0e-5f);
    }
}

void testReleaseTailFallsBelowThresholdAtDocumentedHorizon()
{
    for (const auto decaySeconds : { 0.04f, NullSpineParameters {}.decaySeconds, 6.0f })
        assertReleaseTailFallsBelowThresholdAtHorizon (decaySeconds);
}

void testFiniteBoundedExtremeParameters()
{
    NullSpineParameters params;
    params.pitchOffsetSemitones = 1000.0f;
    params.spine = 1000.0f;
    params.decaySeconds = 1000.0f;
    params.fold = 1000.0f;
    params.tone = 1000.0f;
    params.outputGain = 1000.0f;

    NullSpineEngine engine;
    engine.prepare (0.0);
    engine.setParameters (params);
    engine.reset (0u);
    engine.noteOn (999, 1000.0f);

    for (int i = 0; i < 16384; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9801f && frame.left <= 0.9801f);
        assert (frame.right >= -0.9801f && frame.right <= 0.9801f);
    }
}

void testNonFiniteParametersFallbackSafely()
{
    NullSpineParameters params;
    params.pitchOffsetSemitones = std::numeric_limits<float>::quiet_NaN();
    params.spine = std::numeric_limits<float>::infinity();
    params.decaySeconds = -std::numeric_limits<float>::infinity();
    params.fold = std::numeric_limits<float>::quiet_NaN();
    params.tone = std::numeric_limits<float>::infinity();
    params.outputGain = std::numeric_limits<float>::quiet_NaN();

    const auto samples = renderNote (31337u, -100, std::numeric_limits<float>::infinity(), params, 4096);
    assertStereoRmsAtLeast (samples, 1.0e-4f);
    assert (maxAbs (samples) <= 0.9801f);
    assertStereoFiniteAndBounded (samples);
}

void testVelocityZeroActsSilent()
{
    NullSpineEngine engine;
    engine.prepare (testSampleRate);
    engine.reset (4u);
    engine.noteOn (44, 0.0f);

    for (int i = 0; i < 1024; ++i)
    {
        const auto frame = engine.processSample();
        assert (frame.left == 0.0f);
        assert (frame.right == 0.0f);
    }
}

} // namespace

int main()
{
    testSilentBeforeTrigger();
    testDefaultTriggerHasEnergyAndBounds();
    testDeterministicSameSeedWithinTolerance();
    testNoteTunedModalTail();
    testSpineChangesModalIdentity();
    testDecayControlsTailPersistence();
    testReleaseTailFallsBelowThresholdAtDocumentedHorizon();
    testFiniteBoundedExtremeParameters();
    testNonFiniteParametersFallbackSafely();
    testVelocityZeroActsSilent();

    std::cout << "NullSpineEngineTests passed\n";
    return 0;
}
