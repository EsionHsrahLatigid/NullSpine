#include "NullSpinePlugin.h"

#include "ProductState.h"

#if ! NULLSPINE_HEADLESS_TEST
#include "ParameterGridEditor.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace nullspine::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic {{ 'N', 'S', 'P', '1' }};
constexpr int stateVersion = 1;
constexpr int controlUpdatePeriod = 16;
constexpr int standaloneTriggerNote = 36;
constexpr float standaloneTriggerVelocity = 1.0f;

yup::NormalisableRange<float> makePitchOffsetRange()
{
    return yup::NormalisableRange<float> (-24.0f, 24.0f);
}

yup::NormalisableRange<float> makeDecayRange()
{
    auto range = yup::NormalisableRange<float> (0.03f, 6.0f);
    range.setSkewForCentre (1.0f);
    return range;
}

constexpr std::array<std::array<float, 6>, 4> presetValues {{
    {{ 0.0f, 0.56f, 1.20f, 0.34f, 0.62f, 0.72f }},
    {{ 7.0f, 0.82f, 2.40f, 0.48f, 0.70f, 0.66f }},
    {{ -12.0f, 0.38f, 3.80f, 0.25f, 0.42f, 0.78f }},
    {{ 0.0f, 1.00f, 0.42f, 0.72f, 0.88f, 0.58f }}
}};

float sanitizeVelocity (const yup::MidiMessage& message) noexcept
{
    return std::clamp (message.getFloatVelocity(), 0.0f, 1.0f);
}
} // namespace

NullSpinePlugin::NullSpinePlugin()
    : yup::AudioProcessor ("NullSpine",
                           yup::AudioBusLayout ({
                                                    yup::AudioBus ("midi", yup::AudioBus::Midi, yup::AudioBus::Input, 1),
                                                },
                                                {
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                }))
{
    parameters[pitchOffset] = yup::AudioParameterBuilder()
                             .withID ("pitch_offset")
                             .withName ("Pitch offset")
                             .withHostID (pitchOffset)
                             .withRange (makePitchOffsetRange())
                             .withDefault (presetValues[0][pitchOffset])
                             .withSmoothing (20.0f)
                             .withModulatable (true)
                             .build();
    parameters[spine] = yup::AudioParameterBuilder()
                             .withID ("spine")
                             .withName ("Spine")
                             .withHostID (spine)
                             .withRange (0.0f, 1.0f)
                             .withDefault (presetValues[0][spine])
                             .withSmoothing (25.0f)
                             .withModulatable (true)
                             .build();
    parameters[decay] = yup::AudioParameterBuilder()
                            .withID ("decay")
                            .withName ("Decay")
                            .withHostID (decay)
                            .withRange (makeDecayRange())
                            .withDefault (presetValues[0][decay])
                            .withSmoothing (45.0f)
                            .withModulatable (true)
                            .build();
    parameters[fold] = yup::AudioParameterBuilder()
                           .withID ("fold")
                           .withName ("Fold")
                           .withHostID (fold)
                           .withRange (0.0f, 1.0f)
                           .withDefault (presetValues[0][fold])
                           .withSmoothing (20.0f)
                           .withModulatable (true)
                           .build();
    parameters[tone] = yup::AudioParameterBuilder()
                           .withID ("tone")
                           .withName ("Tone")
                           .withHostID (tone)
                           .withRange (0.0f, 1.0f)
                           .withDefault (presetValues[0][tone])
                           .withSmoothing (25.0f)
                           .withModulatable (true)
                           .build();
    parameters[output] = yup::AudioParameterBuilder()
                             .withID ("output")
                             .withName ("Output")
                             .withHostID (output)
                             .withRange (0.0f, 2.0f)
                             .withDefault (presetValues[0][output])
                             .withSmoothing (30.0f)
                             .withModulatable (true)
                             .build();

    for (const auto& parameter : parameters)
        addParameter (parameter);
}

void NullSpinePlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);
        smoothedValues[i] = parameters[i]->getValue();
    }

    engine.reset();
    applyEngineParameters();
    resetPerformanceState();
}

void NullSpinePlugin::releaseResources()
{
}

void NullSpinePlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto midi = context.midi.begin();
    const auto midiEnd = context.midi.end();
    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    float blockPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        consumeStandaloneTriggerGate();

        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            const auto& message = (*midi).getMessage();
            if (message.isNoteOn())
            {
                activeNote = std::clamp (message.getNoteNumber(), 0, 127);
                activeSource = ActiveSource::midi;
                engine.noteOn (activeNote, sanitizeVelocity (message));
            }
            else if (message.isNoteOff())
            {
                const auto note = std::clamp (message.getNoteNumber(), 0, 127);
                if (activeSource == ActiveSource::midi && note == activeNote)
                {
                    engine.noteOff (note);
                    if (standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0)
                    {
                        activeNote = standaloneTriggerNote;
                        activeSource = ActiveSource::standalone;
                        engine.noteOn (standaloneTriggerNote, standaloneTriggerVelocity);
                    }
                    else
                    {
                        activeNote = -1;
                        activeSource = ActiveSource::none;
                    }
                }
            }
            ++midi;
        }

        advanceParameterHandles (sample);
        if (controlUpdateCountdown <= 0)
        {
            applyEngineParameters();
            controlUpdateCountdown = controlUpdatePeriod;
        }
        --controlUpdateCountdown;

        const auto frame = engine.processSample();

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;
        blockPeak = std::max (blockPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    outputPeakMilli.store (static_cast<int> (std::clamp (blockPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    context.midi.clear();
}

void NullSpinePlugin::flush()
{
    engine.reset();
    resetPerformanceState();
}

bool NullSpinePlugin::acceptsMidi() const noexcept
{
    return true;
}

int NullSpinePlugin::getNumVoices() const
{
    return 1;
}

int NullSpinePlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void NullSpinePlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int NullSpinePlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String NullSpinePlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void NullSpinePlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result NullSpinePlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    auto presetIndex = currentPreset.load (std::memory_order_relaxed);
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), presetIndex);
    if (result.wasOk())
        currentPreset.store (presetIndex, std::memory_order_relaxed);
    return result;
}

yup::Result NullSpinePlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool NullSpinePlugin::hasEditor() const
{
#if NULLSPINE_HEADLESS_TEST
    return false;
#else
    return true;
#endif
}

yup::AudioProcessorEditor* NullSpinePlugin::createEditor()
{
#if NULLSPINE_HEADLESS_TEST
    return nullptr;
#else
    return new ParameterGridEditor (*this,
                                    "NullSpine",
                                    "Hold Trigger or Space to strike the spine. External MIDI takes priority.",
                                    0xfff2f0e8u);
#endif
}

void NullSpinePlugin::setStandaloneTriggerGate (bool shouldBeOn) noexcept
{
    const auto newValue = shouldBeOn ? 1 : 0;
    const auto oldValue = standaloneTriggerDesiredGate.exchange (newValue, std::memory_order_relaxed);
    if (oldValue != newValue)
        standaloneTriggerGateEdges.fetch_add (1u, std::memory_order_release);
}

bool NullSpinePlugin::isStandaloneTriggerGateRequested() const noexcept
{
    return standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0;
}

float NullSpinePlugin::getOutputPeakLevel() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

std::uint32_t NullSpinePlugin::getStandaloneTriggerEdgeCountForTests() const noexcept
{
    return standaloneTriggerGateEdges.load (std::memory_order_acquire);
}

void NullSpinePlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
}

void NullSpinePlugin::consumeStandaloneTriggerGate() noexcept
{
    const auto publishedEdges = standaloneTriggerGateEdges.load (std::memory_order_acquire);
    if (publishedEdges == consumedStandaloneGateEdges)
        return;

    ++consumedStandaloneGateEdges;
    audioStandaloneGate = ! audioStandaloneGate;

    if (activeSource == ActiveSource::midi)
        return;

    if (audioStandaloneGate)
    {
        activeNote = standaloneTriggerNote;
        activeSource = ActiveSource::standalone;
        engine.noteOn (standaloneTriggerNote, standaloneTriggerVelocity);
    }
    else if (activeSource == ActiveSource::standalone)
    {
        engine.noteOff (standaloneTriggerNote);
        activeNote = -1;
        activeSource = ActiveSource::none;
    }
}

void NullSpinePlugin::applyEngineParameters() noexcept
{
    nullspine::NullSpineParameters engineParameters;
    engineParameters.pitchOffsetSemitones = smoothedValues[pitchOffset];
    engineParameters.spine = smoothedValues[spine];
    engineParameters.decaySeconds = smoothedValues[decay];
    engineParameters.fold = smoothedValues[fold];
    engineParameters.tone = smoothedValues[tone];
    engineParameters.outputGain = smoothedValues[output];
    engine.setParameters (engineParameters);
}

void NullSpinePlugin::resetPerformanceState() noexcept
{
    activeNote = -1;
    activeSource = ActiveSource::none;
    audioStandaloneGate = false;
    const auto publishedEdges = standaloneTriggerGateEdges.load (std::memory_order_acquire);
    consumedStandaloneGateEdges = standaloneTriggerDesiredGate.load (std::memory_order_relaxed) != 0 && publishedEdges > 0u
                                      ? publishedEdges - 1u
                                      : publishedEdges;
    controlUpdateCountdown = 0;
    outputPeakMilli.store (0, std::memory_order_relaxed);
}

} // namespace nullspine::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new nullspine::plugin::NullSpinePlugin();
}
