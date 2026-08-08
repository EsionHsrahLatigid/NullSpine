#include "NullSpinePlugin.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr int numChannels = 2;
constexpr int blockSamples = 4096;

class PluginHarness
{
public:
    PluginHarness()
        : audio (numChannels, blockSamples)
        , context { audio, midi, automation, nullptr, {}, {} }
    {
        const auto parameters = plugin.getParameters();
        parameters[2]->setValue (0.03f);
        plugin.prepareToPlay (yup::AudioSpec (48000.0f, blockSamples, numChannels));
    }

    float process()
    {
        audio.clear();
        plugin.processBlock (context);
        return peak();
    }

    std::array<float, blockSamples> processLeft()
    {
        audio.clear();
        plugin.processBlock (context);

        std::array<float, blockSamples> result {};
        const auto* samples = audio.getReadPointer (0);
        for (int sample = 0; sample < blockSamples; ++sample)
            result[static_cast<std::size_t> (sample)] = samples[sample];
        return result;
    }

    void addMidiNoteOn (int note, int sample)
    {
        midi.addEvent (yup::MidiMessage::noteOn (1, note, 1.0f), sample);
    }

    void addMidiNoteOff (int note, int sample)
    {
        midi.addEvent (yup::MidiMessage::noteOff (1, note), sample);
    }

    float peak() const
    {
        float result = 0.0f;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                result = std::max (result, std::fabs (samples[sample]));
        }
        return result;
    }

    nullspine::plugin::NullSpinePlugin plugin;

private:
    yup::AudioBuffer<float> audio;
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer automation;
    yup::AudioProcessContext<float> context;
};

void processUntilSilent (PluginHarness& harness)
{
    for (int i = 0; i < 80; ++i)
    {
        const auto peak = harness.process();
        if (peak < 1.0e-5f)
            return;
    }

    assert (false);
}

void testHeldSyntheticTriggerRendersAndMeters()
{
    PluginHarness harness;
    harness.plugin.setStandaloneTriggerGate (true);

    const auto peak = harness.process();

    assert (harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.plugin.getOutputPeakLevel() > 0.0f);
    assert (peak > 1.0e-5f);
}

void testHostParametersAreStableAndTriggerIsRuntimeOnly()
{
    nullspine::plugin::NullSpinePlugin plugin;
    const auto parameters = plugin.getParameters();

    assert (parameters.size() == 6u);
    const std::array<yup::String, 6> ids {
        "pitch_offset", "spine", "decay", "fold", "tone", "output"
    };
    const std::array<yup::String, 6> names {
        "Pitch offset", "Spine", "Decay", "Fold", "Tone", "Output"
    };

    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        assert (parameters[i]->getID() == ids[i]);
        assert (parameters[i]->getName() == names[i]);
    }

    assert (plugin.getParameterByID ("trigger") == nullptr);
    assert (plugin.getParameterByID ("Trigger") == nullptr);
}

void testStateRoundTripUsesNullSpineMagicAndStableIds()
{
    nullspine::plugin::NullSpinePlugin source;
    nullspine::plugin::NullSpinePlugin target;

    source.setCurrentPreset (2);
    source.getParameterByID ("pitch_offset")->setValue (-12.0f);
    source.getParameterByID ("spine")->setValue (0.91f);
    source.getParameterByID ("decay")->setValue (2.75f);
    source.getParameterByID ("fold")->setValue (0.44f);
    source.getParameterByID ("tone")->setValue (0.23f);
    source.getParameterByID ("output")->setValue (0.81f);
    source.setStandaloneTriggerGate (true);

    yup::MemoryBlock state;
    assert (source.saveStateIntoMemory (state).wasOk());
    const auto* bytes = static_cast<const char*> (state.getData());
    assert (state.getSize() > 16u);
    assert (bytes[0] == 'N' && bytes[1] == 'S' && bytes[2] == 'P' && bytes[3] == '1');

    assert (target.loadStateFromMemory (state).wasOk());
    assert (target.getCurrentPreset() == 2);
    assert (! target.isStandaloneTriggerGateRequested());

    const auto sourceParameters = source.getParameters();
    const auto targetParameters = target.getParameters();
    for (std::size_t i = 0; i < sourceParameters.size(); ++i)
        assert (sourceParameters[i]->getValue() == targetParameters[i]->getValue());
}

void testRapidOnOffBeforeCallbackStillRendersRelease()
{
    PluginHarness harness;

    const auto startEdges = harness.plugin.getStandaloneTriggerEdgeCountForTests();
    harness.plugin.setStandaloneTriggerGate (true);
    harness.plugin.setStandaloneTriggerGate (false);
    assert (harness.plugin.getStandaloneTriggerEdgeCountForTests() == startEdges + 2u);
    assert (! harness.plugin.isStandaloneTriggerGateRequested());

    const auto peak = harness.process();
    assert (peak > 1.0e-5f);

    processUntilSilent (harness);
}

void testMidiNoteOffRestartsHeldStandaloneGate()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.addMidiNoteOn (60, 0);
    assert (harness.process() > 1.0e-5f);

    harness.addMidiNoteOff (60, 0);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (harness);
}

void testUiPressReleaseDoesNotInterruptHeldMidi()
{
    PluginHarness midiOnly;
    midiOnly.addMidiNoteOn (60, 0);
    const auto expected = midiOnly.processLeft();

    PluginHarness withUiEdges;
    withUiEdges.addMidiNoteOn (60, 0);
    withUiEdges.plugin.setStandaloneTriggerGate (true);
    withUiEdges.plugin.setStandaloneTriggerGate (false);
    const auto actual = withUiEdges.processLeft();

    assert (actual == expected);

    withUiEdges.addMidiNoteOff (60, 0);
    withUiEdges.process();
    processUntilSilent (withUiEdges);
}

void testHeldUiGateDoesNotInterruptMidiUntilMidiOff()
{
    PluginHarness midiOnly;
    midiOnly.addMidiNoteOn (60, 0);
    const auto expected = midiOnly.processLeft();

    PluginHarness withHeldUiGate;
    withHeldUiGate.addMidiNoteOn (60, 0);
    withHeldUiGate.plugin.setStandaloneTriggerGate (true);
    const auto actual = withHeldUiGate.processLeft();

    assert (actual == expected);
    assert (withHeldUiGate.plugin.isStandaloneTriggerGateRequested());

    withHeldUiGate.addMidiNoteOff (60, 0);
    assert (withHeldUiGate.process() > 1.0e-5f);

    withHeldUiGate.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (withHeldUiGate);
}

void testFlushDoesNotSuppressHeldStandaloneGate()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.flush();
    assert (harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    processUntilSilent (harness);
}

void testReleaseGateDecaysToSilence()
{
    PluginHarness harness;

    harness.plugin.setStandaloneTriggerGate (true);
    assert (harness.process() > 1.0e-5f);

    harness.plugin.setStandaloneTriggerGate (false);
    assert (! harness.plugin.isStandaloneTriggerGateRequested());
    assert (harness.process() > 1.0e-5f);

    processUntilSilent (harness);
}

std::string readTextFile (const char* path)
{
    std::ifstream stream (path);
    assert (stream.is_open());
    return { std::istreambuf_iterator<char> (stream), std::istreambuf_iterator<char>() };
}

std::string extractFunctionBody (const std::string& source, const std::string& signature)
{
    const auto signaturePosition = source.find (signature);
    assert (signaturePosition != std::string::npos);
    const auto openBrace = source.find ('{', signaturePosition);
    assert (openBrace != std::string::npos);

    int depth = 0;
    for (std::size_t i = openBrace; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}')
        {
            --depth;
            if (depth == 0)
                return source.substr (openBrace, i - openBrace + 1);
        }
    }

    assert (false);
    return {};
}

void testProcessBlockHasNoObviousRealtimeForbiddenOperations()
{
    const auto path = std::string (NULLSPINE_SOURCE_DIR) + "/source/NullSpinePlugin.cpp";
    const auto source = readTextFile (path.c_str());
    const auto body = extractFunctionBody (source, "void NullSpinePlugin::processBlock");
    const std::vector<std::string> forbidden {
        "new ",
        "delete ",
        "malloc",
        "calloc",
        "realloc",
        "free (",
        "std::lock",
        "std::mutex",
        "std::scoped_lock",
        "std::unique_lock",
        "std::lock_guard",
        "std::cout",
        "std::cerr",
        "printf",
        "fprintf",
        "fopen",
        "ifstream",
        "ofstream",
        "AudioProcessorEditor",
        "createEditor",
        "addAndMakeVisible",
        "setText",
        "repaint"
    };

    for (const auto& token : forbidden)
        assert (body.find (token) == std::string::npos);
}
} // namespace

int main()
{
    testHeldSyntheticTriggerRendersAndMeters();
    testHostParametersAreStableAndTriggerIsRuntimeOnly();
    testStateRoundTripUsesNullSpineMagicAndStableIds();
    testRapidOnOffBeforeCallbackStillRendersRelease();
    testMidiNoteOffRestartsHeldStandaloneGate();
    testUiPressReleaseDoesNotInterruptHeldMidi();
    testHeldUiGateDoesNotInterruptMidiUntilMidiOff();
    testFlushDoesNotSuppressHeldStandaloneGate();
    testReleaseGateDecaysToSilence();
    testProcessBlockHasNoObviousRealtimeForbiddenOperations();

    std::cout << "NullSpinePluginBridgeTests passed\n";
    return 0;
}
