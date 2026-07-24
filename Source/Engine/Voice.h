// GLOBUS — one polyphonic voice: 2 osc units + sub + noise → multimode filter
// → amp envelope. All modulation evaluated at control rate (32 samples).
#pragma once
#include "DspUtils.h"
#include "Oscillator.h"
#include "OscillatorHQ.h"
#include "Filter.h"
#include "FilterHQ.h"
#include "Envelope.h"
#include "LFO.h"
#include "ModMatrix.h"
#include "../Parameters.h"

namespace ydc
{
/** Stateless hash noise — deterministic S&H for the shared free-running LFO. */
inline float hashNoise (uint64_t i) noexcept
{
    i ^= i >> 33; i *= 0xff51afd7ed558ccdULL;
    i ^= i >> 33; i *= 0xc4ceb9fe1a85ec53ULL;
    i ^= i >> 33;
    return (float) (i & 0xffffffu) * (2.0f / 16777215.0f) - 1.0f;
}

/** Per-block context handed from the engine to every voice. */
struct RenderContext
{
    const ParamRefs* params = nullptr;
    const SlotState* slots  = nullptr;   // 8 decoded matrix slots
    double sampleRate = 44100.0;
    double bpm        = 120.0;
    float  modWheel   = 0.0f;            // 0..1 smoothed
    float  aftertouch = 0.0f;            // 0..1 smoothed (channel pressure)
    float  pitchBendNorm  = 0.0f;        // -1..1 smoothed
    float  pitchBendRange = 2.0f;        // semitones
    double lfoBasePhase[2] { 0.0, 0.0 }; // unwrapped master phase at block start
    float  lfoRateHz[2]    { 0.0f, 0.0f };

    // v1.2: immutable banks published by the processor for this block
    const WavetableBank* wtBank[2] { nullptr, nullptr };
    const WavetableBank* hqShapes  = nullptr;
    QualityMode quality = QualityMode::Legacy;
};

class Voice
{
public:
    void prepare (double sampleRate);
    void reset();

    /** glideFromNote < 0 → no glide. */
    void startNote (const ParamRefs& params, int midiNote, float velocity, uint32_t order,
                    bool legato, float glideFromNote, float glideTimeSec, uint32_t seed);
    void stopNote();
    void fastSteal();          // ~3 ms fade-out, then the voice frees itself

    void changeNoteLegato (int midiNote, float glideTimeSec);

    void setPolyAftertouch (float v) noexcept { polyAT = v; }
    void setSustained (bool s) noexcept       { sustained = s; }

    bool  isActive() const noexcept     { return active; }
    bool  isReleasing() const noexcept  { return ampEnv.isReleasing(); }
    bool  isSustained() const noexcept  { return sustained; }
    int   getNote() const noexcept      { return note; }
    float getCurrentNote() const noexcept { return currentNote; }
    uint32_t getOrder() const noexcept  { return order; }
    float getAmpLevel() const noexcept  { return ampEnv.getLevel(); }

    /** Render and ADD into L/R (which point at the start of the host block).
        absStart = offset of this segment inside the block. */
    void render (float* L, float* R, int absStart, int numSamples, const RenderContext& ctx);

private:
    void renderChunk (float* L, float* R, int absStart, int n, const RenderContext& ctx);

    OscUnit  osc[2];
    BasicHqOsc        oscHq[2];   // v1.2 engines: render only when selected;
    WavetableVoiceOsc oscWt[2];   // their RNG never touches the legacy sequence
    float wtPosState[2]  { 0.0f, 0.0f };   // chunk-ramp smoothing state
    float warpState[2]   { 0.0f, 0.0f };
    SubOsc   sub;
    NoiseGen noise;
    MultimodeFilter filter;
    FilterHQ filterHq;            // v1.2 appended models (Ladder/OTA/SEM)
    AdsrEnv  ampEnv, filEnv, modEnv;
    Lfo      lfo[2];                     // used when retrigger is on
    float    lfoFadeElapsed[2] { 0, 0 }; // fade tracking for free-running mode
    float    lastLfoRateMul[2] { 1.0f, 1.0f };
    Rng      rng;

    double sr = 44100.0;
    bool  active = false, sustained = false;
    int   note = -1;
    float velocity = 0.0f, polyAT = 0.0f, randomVal = 0.0f;
    uint32_t order = 0;

    float currentNote = 60.0f, targetNote = 60.0f, glideRate = 0.0f; // semitones/sec
    float ampModPrev = 1.0f;
};

} // namespace ydc
