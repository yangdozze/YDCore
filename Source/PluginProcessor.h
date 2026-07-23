// GLOBUS (Ninth Parallel Audio) — plugin processor:
// MIDI → arpeggiator → 32-voice engine → FX chain.
// Internal class names keep the original YDCore identifiers for stability.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "Engine/SynthEngine.h"
#include "Engine/Arpeggiator.h"
#include "Effects/EffectsChain.h"
#include "Presets/PresetManager.h"

class YDCoreAudioProcessor : public juce::AudioProcessor
{
public:
    YDCoreAudioProcessor();
    ~YDCoreAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override           { return "GLOBUS"; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 4.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    ydc::ParamRefs&        getParamRefs() noexcept          { return refs; }
    ydc::PresetManager&    getPresetManager() noexcept      { return presetManager; }
    ydc::SynthEngine&      getEngine() noexcept             { return engine; }
    juce::MidiKeyboardState& getKeyboardState() noexcept    { return keyboardState; }

    /** UI helpers */
    bool  consumeMidiActivity() noexcept { return midiActivity.exchange (0, std::memory_order_relaxed) > 0; }
    float getCpuLoad() const noexcept    { return (float) loadMeasurer.getLoadAsProportion(); }

    /** Output peak since the last UI read (lock-free; the meter decays in the UI). */
    float consumeOutputPeak (int channel) noexcept
    {
        return outPeak[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    ydc::ParamRefs   refs;
    ydc::SynthEngine engine;
    ydc::Arpeggiator arp;
    ydc::EffectsChain fx;
    ydc::PresetManager presetManager;

    juce::MidiKeyboardState keyboardState;
    juce::MidiBuffer arpBuffer;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<int> midiActivity { 0 };
    std::atomic<float> outPeak[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YDCoreAudioProcessor)
};
