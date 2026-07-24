// GLOBUS (Ninth Parallel Audio) — plugin processor:
// MIDI → arpeggiator → 32-voice engine → FX chain.
// Internal class names keep the original YDCore identifiers for stability.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "Engine/SynthEngine.h"
#include "Engine/Arpeggiator.h"
#include "Engine/Wavetable.h"
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

    //==============================================================================
    // v1.2 wavetable selection (message thread; the audio thread only reads the
    // atomic bank pointers). Selection is stored as a state property — NOT a host
    // parameter — so table identity survives save/reload without index fragility.
    static constexpr const char* wtStateProp (int oscIndex0) noexcept
    {
        return oscIndex0 == 0 ? "osc1Wavetable" : "osc2Wavetable";
    }

    /** Select a wavetable by name. Factory names resolve immediately; unknown
        names keep the default bank audible but preserve the requested name in
        the state (missing-asset recoverability). */
    void setOscWavetableByName (int oscIndex0, const juce::String& name);

    juce::String getOscWavetableName (int oscIndex0) const
    {
        return apvts.state.getProperty (wtStateProp (oscIndex0),
                                        ydc::WavetableLibrary::get().defaultBank()->name).toString();
    }

    bool isOscWavetableMissing (int oscIndex0) const noexcept
    {
        return wtMissing[juce::jlimit (0, 1, oscIndex0)].load (std::memory_order_relaxed);
    }

    /** Re-resolve both oscillators' banks from the current state properties
        (used after preset load / state restore / init). */
    void refreshWavetablesFromState();

    /** Import a WAV as a user wavetable on a worker thread; on success the file
        is copied into Documents/GLOBUS/Wavetables, the bank is registered and
        selected for `oscIndex0`, and wtEvents broadcasts. On failure the error
        lands in lastImportError and the current sound is untouched. */
    void importWavetableAsync (int oscIndex0, const juce::File& file);

    juce::String getLastWavetableImportError() const
    {
        const juce::ScopedLock sl (importLock);
        return lastImportError;
    }

    /** User banks currently registered (message thread). */
    juce::StringArray getUserWavetableNames() const;

    /** UI listens here for wavetable selection/import changes. */
    juce::ChangeBroadcaster wtEvents;

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

    // v1.2: published banks (immutable; factory banks are process-lifetime)
    std::atomic<const ydc::WavetableBank*> wtBankPtr[2] { nullptr, nullptr };
    std::atomic<bool> wtMissing[2] { false, false };
    const ydc::WavetableBank* hqShapesCache = nullptr;

    // user wavetable registry (message thread owns; audio thread reads raw
    // pointers published via wtBankPtr). Replaced banks are retired with the
    // block epoch and only destroyed two epochs later, off the audio thread.
    const ydc::WavetableBank* findUserBank (const juce::String& name) const;
    void registerUserBank (std::shared_ptr<const ydc::WavetableBank> bank);
    void cleanupRetiredBanks();

    std::vector<std::shared_ptr<const ydc::WavetableBank>> userBanks;
    struct RetiredBank { std::shared_ptr<const ydc::WavetableBank> bank; juce::uint64 epoch; };
    std::vector<RetiredBank> retiredBanks;
    std::atomic<juce::uint64> blockEpoch { 0 };

    juce::CriticalSection importLock;      // guards lastImportError only
    juce::String lastImportError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YDCoreAudioProcessor)
};
