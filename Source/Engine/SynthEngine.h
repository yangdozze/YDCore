// GLOBUS — 32-voice engine: voice allocation, play modes, sustain pedal,
// click-free stealing, global modulation and master LFO clocking.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Voice.h"

namespace ydc
{
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();                       // hard: silences everything immediately

    /** Renders into `out` (replaces content). MIDI is consumed sample-accurately. */
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                  double bpm, const ParamRefs& params);

    // Global modulation results for the FX section (valid after process()):
    float getFxMixScale() const noexcept   { return fxMixScale; }
    float getStereoWidthMod() const noexcept { return widthMod; }

    int  getActiveVoiceCount() const noexcept { return activeVoiceCount.load (std::memory_order_relaxed); }
    bool isPedalDown() const noexcept { return pedalDown; }

    // v1.2 rendering context — set by the processor right before process()
    // (audio thread only; the pointers reference immutable banks)
    const WavetableBank* wtBankOsc[2] { nullptr, nullptr };
    const WavetableBank* hqShapesBank = nullptr;
    QualityMode qualityForBlock = QualityMode::Legacy;

private:
    void handleMidiEvent (const juce::MidiMessage& m, const ParamRefs& params);
    void noteOn  (int note, float vel, const ParamRefs& params);
    void noteOff (int note, const ParamRefs& params);
    void handlePedal (bool down, const ParamRefs& params);
    void allNotesOff (bool hard);
    void modeChangeFlush();

    Voice* findFreeVoice();
    Voice* chooseStealVictim();
    void   startPendingNotes (const ParamRefs& params);
    void   renderSegment (juce::AudioBuffer<float>& out, int from, int len);
    void   resolveMono (const ParamRefs& params, bool freshPress);

    std::array<Voice, kMaxVoices> voices;
    RenderContext ctx;
    SlotState slots[8];

    // free-running master LFO clocks (unwrapped phase)
    double masterPhaseTotal[2] { 0.0, 0.0 };

    // smoothed global controllers
    OnePoleSmoother wheelSm, atSm, pbSm;
    float wheelTarget = 0.0f, atTarget = 0.0f, pbTarget = 0.0f;

    bool pedalDown = false;
    bool physicallyHeld[128] {};

    // mono/legato note stack in press order
    int   monoStack[128] {};
    float monoVelo[128] {};
    int   monoCount = 0;
    bool  monoSoundingFromPedal = false;

    struct Pending { int note = -1; float vel = 0.0f; int ttl = 0; float glideFrom = -1.0f; };
    Pending pending[8];
    int pendingCount = 0;

    uint32_t orderCounter = 0;
    PlayMode lastMode = PlayMode::Poly;
    float lastPlayedNote = -1.0f;
    Rng seedRng;

    double sr = 44100.0;
    float fxMixScale = 1.0f, widthMod = 0.0f;
    std::atomic<int> activeVoiceCount { 0 };
};

} // namespace ydc
