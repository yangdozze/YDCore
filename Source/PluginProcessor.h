// YD Core — plugin processor: MIDI → arpeggiator → 32-voice engine → FX chain.
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

    const juce::String getName() const override           { return "YD Core"; }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YDCoreAudioProcessor)
};
