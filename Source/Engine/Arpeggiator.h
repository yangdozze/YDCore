// GLOBUS — tempo-synced arpeggiator. Sits before the synth engine and
// transforms note input into pattern steps (up/down/up-down/random, 1-3 octaves,
// gate control, hold/latch). Locks to the host grid when the transport runs.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
class Arpeggiator
{
public:
    void prepare (double sampleRate);
    void reset();

    /** When the arp is off, input passes through untouched. */
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples,
                  double bpm, bool hostPlaying, double ppqPosition, const ParamRefs& params);

private:
    void noteOn (int note, float vel, bool hold);
    void noteOff (int note, bool hold);
    void buildSequence (ArpMode mode, int octaves);
    void stopCurrent (juce::MidiBuffer& out, int samplePos);

    static constexpr int kMaxHeld = 16;
    static constexpr int kMaxSeq  = kMaxHeld * 3 * 2;

    int   held[kMaxHeld] {};      // sorted ascending
    float heldVel[kMaxHeld] {};
    int   heldCount = 0;
    int   physicalCount = 0;      // keys actually down (for latch chord replacement)

    int  seq[kMaxSeq] {};
    int  seqLen = 0, seqPos = 0;

    double sr = 44100.0;
    double samplesToNextStep = 0.0;
    double gateRemaining = -1.0;  // <0 → no note sounding
    int    currentArpNote = -1;
    float  currentVel = 0.8f;
    bool   wasOn = false;
    Rng    rng;
};

} // namespace ydc
