// GLOBUS v1.2 — the two NEW oscillator engines. The legacy OscUnit is untouched;
// these render only when an oscillator's Engine parameter selects them.
//
//   BasicHqOsc        — band-limited classic waves via harmonic table selection:
//                       saw/triangle read from an internal mip-mapped shape bank
//                       (harmonics truncated per pitch, Catmull-Rom, level
//                       crossfade), pulse/square via the differentiated-saw
//                       identity, sine analytically. This keeps every partial
//                       below ~0.45·fs by construction — measurably less
//                       aliasing than the legacy polyBLEP/naive paths.
//   WavetableVoiceOsc — mip-mapped wavetable playback with position morphing,
//                       warp modes and frequency-aware level selection.
//
// Shared HQ unison rules (Part G): deterministic placement, centred odd stacks,
// balanced even stacks, equal-power stereo distribution, outer-voice gain taper
// with sum-of-squares compensation, golden-ratio phase offsets when random
// phase is off.
//
// Real-time safety: fixed-size state, no allocation, no locks, no I/O. All
// table pointers reference immutable banks built off the audio thread.
#pragma once
#include "DspUtils.h"
#include "Oscillator.h"     // kSuperSaw tables, OscBlockParams
#include "Wavetable.h"
#include "../Parameters.h"

namespace ydc
{
//==============================================================================
/** Deterministic HQ unison layout for voice u of N (0-based):
    position in [-1,1], gain taper, golden-phase offset. */
struct UnisonSlot { float pos, gain, phaseOfs; };

inline UnisonSlot hqUnisonSlot (int u, int count) noexcept
{
    UnisonSlot s;
    if (count <= 1)
    {
        s.pos = 0.0f; s.gain = 1.0f; s.phaseOfs = 0.0f;
        return s;
    }
    s.pos = (float) u * 2.0f / (float) (count - 1) - 1.0f;   // centred odd / balanced even
    const float a = std::abs (s.pos);
    s.gain = 1.0f - 0.28f * a * std::sqrt (a);               // outer voices taper (supersaw-style)
    s.phaseOfs = (float) u * 0.61803398875f;                 // golden ratio — avoids phase alignment
    return s;
}

/** Gain normalisation so unison count changes keep perceived level stable. */
inline float hqUnisonNorm (int count) noexcept
{
    float sumSq = 0.0f;
    for (int u = 0; u < count; ++u)
    {
        const auto s = hqUnisonSlot (u, count);
        sumSq += s.gain * s.gain;
    }
    return sumSq > 0.0f ? 1.0f / std::sqrt (sumSq) : 1.0f;
}

//==============================================================================
/** Extra per-chunk inputs for the v1.2 engines (on top of OscBlockParams). */
struct HqBlockParams
{
    // wavetable-only
    const WavetableBank* bank = nullptr;
    float posStart = 0.0f, posEnd = 0.0f;     // 0..1 across the chunk (already clamped)
    WarpMode warpMode = WarpMode::Off;
    float warpStart = 0.0f, warpEnd = 0.0f;   // 0..1 across the chunk
    // both engines
    QualityMode quality = QualityMode::Legacy;
};

//==============================================================================
/** Mip-level pair + crossfade helper shared by both HQ engines. */
struct WtLevelPick
{
    int L0, L1;
    float frac;
};
inline WtLevelPick pickWtLevel (double phaseInc, float biasLevels) noexcept
{
    const float levelF = wtLevelForIncrement (phaseInc, biasLevels);
    WtLevelPick p;
    p.L0 = (int) levelF;
    p.frac = levelF - (float) p.L0;
    p.L1 = std::min (p.L0 + 1, kWtNumLevels - 1);
    return p;
}

//==============================================================================
class BasicHqOsc
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        invSr = 1.0 / sampleRate;
    }

    void reset (Rng& voiceRng, bool randomPhase, float phase01) noexcept
    {
        for (int i = 0; i < kMaxPartials; ++i)
        {
            const auto slot = hqUnisonSlot (i % kMaxUnison, kMaxUnison);
            phases[i] = randomPhase ? (double) voiceRng.uni()
                                    : (double) phase01 + (double) slot.phaseOfs * 0.13
                                      + (i / kMaxUnison) * 0.0173;
            phases[i] -= std::floor (phases[i]);
        }
        for (int i = 0; i < kMaxUnison; ++i)
            driftState[i] = 0.0f;
        rng.seed (voiceRng.next());
    }

    /** shapes = the internal HQ shape bank (frame 0 = saw, frame 1 = triangle),
        built once by WavetableLibrary with normalisation OFF so amplitudes match
        the mathematical waveforms. Render and ADD into outL/outR. */
    void render (float* outL, float* outR, int n, const OscBlockParams& p,
                 const WavetableBank& shapes) noexcept
    {
        if (p.gain <= 0.0001f || n <= 0)
            return;

        const int uni = std::max (1, std::min (kMaxUnison, p.uniCount));
        const bool super = (p.wave == OscWave::SuperSaw);
        const int partialsPerUni = super ? kSuperSawN : 1;

        for (int u = 0; u < uni; ++u)
        {
            driftState[u] = driftState[u] * 0.995f + rng.bi() * 0.005f;
            driftState[u] = clampf (driftState[u], -1.0f, 1.0f);
        }

        // sum-of-squares comp for the supersaw partial gains
        float superNorm = 1.0f;
        if (super)
        {
            float sq = 0.0f;
            for (float gsp : kSuperSawGains)
                sq += gsp * gsp;
            superNorm = 1.0f / std::sqrt (sq);
        }
        const float norm = hqUnisonNorm (uni) * superNorm;

        if (p.wave == OscWave::Noise)
        {
            float gl, gr;
            panGains (p.pan, gl, gr);
            const float g = p.gain * 0.7f;
            for (int s = 0; s < n; ++s)
            {
                const float v = rng.bi() * g;
                outL[s] += v * gl;
                outR[s] += v * gr;
            }
            return;
        }

        for (int u = 0; u < uni; ++u)
        {
            const auto slot = hqUnisonSlot (u, uni);
            const float detuneRatio = semisToRatio (slot.pos * p.detuneCents * 0.01f);
            const float driftRatio  = semisToRatio (driftState[u] * p.drift * 0.08f);
            const float uniFreq = p.freqHz * detuneRatio * driftRatio;

            float gl, gr;
            panGains (clampf (p.pan + slot.pos * p.spread, -1.0f, 1.0f), gl, gr);
            const float g = p.gain * norm * slot.gain;

            for (int k = 0; k < partialsPerUni; ++k)
            {
                const int idx = u * kSuperSawN + k;
                double ph = phases[idx];

                float freq = uniFreq;
                float pg   = g;
                if (super)
                {
                    freq *= 1.0f + kSuperSawOffsets[k] * (p.detuneCents * 0.04f);
                    pg   *= kSuperSawGains[k];
                }
                const double dt = clampf (freq, 0.01f, (float) (sr * 0.45)) * invSr;
                const auto lv = pickWtLevel (dt, 0.0f);

                auto readShape = [&shapes, &lv] (int frame, float phase01) noexcept
                {
                    const float a = shapes.readFrame (lv.L0, frame, phase01);
                    const float b = lv.L1 != lv.L0 ? shapes.readFrame (lv.L1, frame, phase01) : a;
                    return a + lv.frac * (b - a);
                };

                switch (super ? OscWave::Saw : p.wave)
                {
                    case OscWave::Sine:
                        for (int s = 0; s < n; ++s)
                        {
                            const float v = std::sin ((float) ph * kTwoPi);
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Triangle:
                        for (int s = 0; s < n; ++s)
                        {
                            const float v = readShape (1, (float) ph);
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Saw:
                        for (int s = 0; s < n; ++s)
                        {
                            const float v = readShape (0, (float) ph);
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Square:
                    case OscWave::Pulse:
                    {
                        // differentiated-saw identity: pulse(φ, w) = saw(φ) − saw(φ+w) − (2w − 1);
                        // both reads are band-limited, so the pulse is too. Continuous in w.
                        const float pw = p.wave == OscWave::Square ? 0.5f : clampf (p.pw, 0.05f, 0.95f);
                        const float dc = 2.0f * pw - 1.0f;
                        for (int s = 0; s < n; ++s)
                        {
                            float ph2 = (float) ph + pw;
                            if (ph2 >= 1.0f) ph2 -= 1.0f;
                            const float v = readShape (0, (float) ph) - readShape (0, ph2) - dc;
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;
                    }

                    case OscWave::SuperSaw:
                    case OscWave::Noise:
                    default: break;
                }
                phases[idx] = ph;
            }
        }
    }

private:
    double sr = 44100.0, invSr = 1.0 / 44100.0;
    double phases[kMaxPartials] {};
    float  driftState[kMaxUnison] {};
    Rng    rng;
};

//==============================================================================
class WavetableVoiceOsc
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        invSr = 1.0 / sampleRate;
    }

    void reset (Rng& voiceRng, bool randomPhase, float phase01) noexcept
    {
        for (int i = 0; i < kMaxUnison; ++i)
        {
            const auto slot = hqUnisonSlot (i, kMaxUnison);
            phases[i] = randomPhase ? (double) voiceRng.uni()
                                    : (double) phase01 + (double) slot.phaseOfs * 0.13;
            phases[i] -= std::floor (phases[i]);
            driftState[i] = 0.0f;
        }
        rng.seed (voiceRng.next());
    }

    /** Warp pre-map. phase01 in [0,1) → warped read phase in [0,1). Returns the
        mip bias (in levels) for the mode/amount via `biasLevels`. Every mode is
        continuous, monotonic-safe, division-safe and bounded. */
    static inline float warpPhase (float phase01, WarpMode mode, float amt, float& biasLevels) noexcept
    {
        const float a = clampf (amt, 0.0f, 1.0f);
        switch (mode)
        {
            case WarpMode::Off:
                biasLevels = 0.0f;
                return phase01;

            case WarpMode::BendPlus:      // rational bend toward the end of the frame
            {
                const float b = std::exp2 (3.0f * a);          // max local slope = b
                biasLevels = 3.0f * a;
                return phase01 / (phase01 + (1.0f - phase01) * b);
            }
            case WarpMode::BendMinus:
            {
                const float b = std::exp2 (3.0f * a);
                biasLevels = 3.0f * a;
                const float inv = 1.0f - phase01;
                return 1.0f - inv / (inv + phase01 * b);
            }
            case WarpMode::Sync:          // windowed hard sync: slave runs at (1+15a)×
            {
                const float m = 1.0f + 15.0f * a;
                biasLevels = std::log2 (m);
                float w = phase01 * m;
                w -= std::floor (w);
                return w;
            }
            case WarpMode::Asymmetry:     // piecewise-linear knee (PW for wavetables)
            {
                const float c = 0.5f - 0.45f * a;              // 0.05 .. 0.5 (never 0)
                biasLevels = std::log2 (std::max (0.5f / c, 0.5f / (1.0f - c)));
                if (phase01 < c)
                    return 0.5f * phase01 / c;
                return 0.5f + 0.5f * (phase01 - c) / (1.0f - c);
            }
            case WarpMode::Mirror:        // ping-pong read, blended in by amount
            {
                biasLevels = a;            // slope 2 at full amount ≈ +1 level
                const float m = 1.0f - std::abs (1.0f - 2.0f * phase01);
                return phase01 + (m - phase01) * a;
            }
            default:
                biasLevels = 0.0f;
                return phase01;
        }
    }

    /** Render and ADD into outL/outR (n <= kControlBlock). */
    void render (float* outL, float* outR, int n, const OscBlockParams& p, const HqBlockParams& hq) noexcept
    {
        const auto* bank = hq.bank;
        if (bank == nullptr || bank->numFrames <= 0 || p.gain <= 0.0001f || n <= 0)
            return;

        const int uni = std::max (1, std::min (kMaxUnison, p.uniCount));
        const float norm = hqUnisonNorm (uni);
        const bool hiQ = hq.quality == QualityMode::High || hq.quality == QualityMode::Ultra;
        const float invN = n > 1 ? 1.0f / (float) n : 1.0f;

        for (int u = 0; u < uni; ++u)
        {
            driftState[u] = driftState[u] * 0.995f + rng.bi() * 0.005f;
            driftState[u] = clampf (driftState[u], -1.0f, 1.0f);
        }

        for (int u = 0; u < uni; ++u)
        {
            const auto slot = hqUnisonSlot (u, uni);
            const float detuneRatio = semisToRatio (slot.pos * p.detuneCents * 0.01f);
            const float driftRatio  = semisToRatio (driftState[u] * p.drift * 0.08f);
            const float uniFreq = clampf (p.freqHz * detuneRatio * driftRatio, 0.01f, (float) (sr * 0.45));
            const double dt = (double) uniFreq * invSr;

            float gl, gr;
            panGains (clampf (p.pan + slot.pos * p.spread, -1.0f, 1.0f), gl, gr);
            const float g = p.gain * norm * slot.gain;

            double ph = phases[u];

            // per-sample ramps for position and warp (click-safe fast modulation)
            float pos  = hq.posStart;
            float warp = hq.warpStart;
            const float posStep  = (hq.posEnd - hq.posStart) * invN;
            const float warpStep = (hq.warpEnd - hq.warpStart) * invN;

            const bool syncMode = hq.warpMode == WarpMode::Sync;

            for (int s = 0; s < n; ++s)
            {
                float bias = 0.0f;
                const float wph = warpPhase ((float) ph, hq.warpMode, warp, bias);

                const auto lv = pickWtLevel (dt, bias);

                const float framePos = pos * (float) (bank->numFrames - 1);
                const int   f0 = juce::jlimit (0, bank->numFrames - 1, (int) framePos);
                const int   f1 = std::min (f0 + 1, bank->numFrames - 1);
                const float ft = clampf (framePos - (float) f0, 0.0f, 1.0f);

                float v;
                if (hiQ)
                {
                    const float a0 = bank->readFrame (lv.L0, f0, wph);
                    const float a1 = f1 != f0 ? bank->readFrame (lv.L0, f1, wph) : a0;
                    const float b0 = lv.L1 != lv.L0 ? bank->readFrame (lv.L1, f0, wph) : a0;
                    const float b1 = lv.L1 != lv.L0 ? (f1 != f0 ? bank->readFrame (lv.L1, f1, wph) : b0) : a1;
                    const float va = a0 + ft * (a1 - a0);
                    const float vb = b0 + ft * (b1 - b0);
                    v = va + lv.frac * (vb - va);
                }
                else
                {
                    const int Ln = lv.frac > 0.5f ? lv.L1 : lv.L0;    // nearest level (ECO / LEGACY quality)
                    const float a0 = bank->readFrame (Ln, f0, wph);
                    const float a1 = f1 != f0 ? bank->readFrame (Ln, f1, wph) : a0;
                    v = a0 + ft * (a1 - a0);
                }

                // windowed sync: fade the last 3% of the master period toward the
                // slave-phase-0 sample so the wrap discontinuity stays bounded
                if (syncMode && ph > 0.97)
                {
                    const float mix = clampf ((float) ((ph - 0.97) / 0.03), 0.0f, 1.0f);
                    const float v0 = bank->readFrame (lv.L0, f0, 0.0f);
                    v += (v0 - v) * mix;
                }

                outL[s] += gl * g * v;
                outR[s] += gr * g * v;

                ph += dt; if (ph >= 1.0) ph -= 1.0;
                pos  += posStep;
                warp += warpStep;
            }
            phases[u] = ph;
        }
    }

private:
    double sr = 44100.0, invSr = 1.0 / 44100.0;
    double phases[kMaxUnison] {};
    float  driftState[kMaxUnison] {};
    Rng    rng;
};

} // namespace ydc
