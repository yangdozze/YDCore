// GLOBUS — band-limited oscillator unit with unison, supersaw and analog drift.
#pragma once
#include "DspUtils.h"
#include "../Parameters.h"

namespace ydc
{
/** PolyBLEP residual for band-limiting step discontinuities. t = phase 0..1, dt = phase inc. */
inline float polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt;
        return (float) (x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return (float) (x * x + x + x + 1.0);
    }
    return 0.0f;
}

/** Classic 7-partial supersaw detune curve (fractions of centre frequency at full spread). */
constexpr float kSuperSawOffsets[kSuperSawN] = { -0.11002f, -0.06288f, -0.01952f, 0.0f, 0.01952f, 0.06288f, 0.11002f };
constexpr float kSuperSawGains  [kSuperSawN] = { 0.6f, 0.8f, 0.92f, 1.0f, 0.92f, 0.8f, 0.6f };

/** Per-control-block parameters for one oscillator unit. */
struct OscBlockParams
{
    OscWave wave       = OscWave::Saw;
    float   freqHz     = 440.0f;   // already includes octave/semi/fine/pitch mod
    float   pw         = 0.5f;
    float   detuneCents= 18.0f;    // unison detune (also supersaw spread scale)
    float   spread     = 0.8f;     // unison stereo spread 0..1
    float   pan        = 0.0f;     // base pan -1..1
    float   gain       = 0.75f;    // level (post mod)
    int     uniCount   = 1;
    float   drift      = 0.15f;    // 0..1 analog drift amount
};

/** One oscillator slot of a voice: up to 7 unison voices, each possibly a 7-saw stack. */
class OscUnit
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
            phases[i] = randomPhase ? (double) voiceRng.uni()
                                    : (double) phase01 + (i * 0.061803); // tiny fixed offsets between partials
        for (int i = 0; i < kMaxPartials; ++i)
            phases[i] -= std::floor (phases[i]);
        for (int i = 0; i < kMaxUnison; ++i)
            driftState[i] = 0.0f;
        rngSeed = voiceRng.next();
        rng.seed (rngSeed);
        gainLPrev = gainRPrev = -1.0f; // force snap on first block
    }

    /** Render and ADD into outL/outR (n <= kControlBlock). */
    void render (float* outL, float* outR, int n, const OscBlockParams& p) noexcept
    {
        if (p.gain <= 0.0001f || n <= 0)
            return;

        const int uni = std::max (1, std::min (kMaxUnison, p.uniCount));
        const bool super = (p.wave == OscWave::SuperSaw);
        const int partialsPerUni = super ? kSuperSawN : 1;

        // slow analog drift: random-walk per unison voice, updated once per control block
        for (int u = 0; u < uni; ++u)
        {
            driftState[u] = driftState[u] * 0.995f + rng.bi() * 0.005f;
            driftState[u] = clampf (driftState[u], -1.0f, 1.0f);
        }

        // overall gain normalisation across all partials
        const float norm = 1.0f / std::sqrt ((float) (uni * partialsPerUni));

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
            // unison detune / spread position in [-1, 1]
            const float pos = uni == 1 ? 0.0f : ((float) u * 2.0f / (float) (uni - 1) - 1.0f);
            const float detuneRatio = semisToRatio (pos * p.detuneCents * 0.01f);
            const float driftRatio  = semisToRatio (driftState[u] * p.drift * 0.08f); // up to ±8 cents
            const float uniFreq = p.freqHz * detuneRatio * driftRatio;

            float gl, gr;
            panGains (clampf (p.pan + pos * p.spread, -1.0f, 1.0f), gl, gr);
            const float g = p.gain * norm;

            for (int k = 0; k < partialsPerUni; ++k)
            {
                const int idx = u * kSuperSawN + k;
                double ph = phases[idx];

                float freq = uniFreq;
                float pg   = g;
                if (super)
                {
                    freq *= 1.0f + kSuperSawOffsets[k] * (p.detuneCents * 0.04f); // detune knob scales the stack
                    pg   *= kSuperSawGains[k];
                }
                const double dt = clampf (freq, 0.01f, (float) (sr * 0.45)) * invSr;

                switch (super ? OscWave::Saw : p.wave)
                {
                    case OscWave::Sine:
                        for (int s = 0; s < n; ++s)
                        {
                            outL[s] += gl * pg * std::sin ((float) ph * kTwoPi);
                            outR[s] += gr * pg * std::sin ((float) ph * kTwoPi);
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Triangle:
                        for (int s = 0; s < n; ++s)
                        {
                            const float v = 2.0f * std::abs (2.0f * (float) ph - 1.0f) - 1.0f;
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Saw:
                        for (int s = 0; s < n; ++s)
                        {
                            const float v = (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;

                    case OscWave::Square:
                    case OscWave::Pulse:
                    {
                        const float pw = p.wave == OscWave::Square ? 0.5f : clampf (p.pw, 0.05f, 0.95f);
                        for (int s = 0; s < n; ++s)
                        {
                            float v = ph < pw ? 1.0f : -1.0f;
                            v += polyBlep (ph, dt);
                            double t2 = ph - pw + 1.0; t2 -= std::floor (t2);
                            v -= polyBlep (t2, dt);
                            outL[s] += gl * pg * v;
                            outR[s] += gr * pg * v;
                            ph += dt; if (ph >= 1.0) ph -= 1.0;
                        }
                        break;
                    }

                    case OscWave::SuperSaw:   // handled via the `super` remap above
                    case OscWave::Noise:      // handled by the early return above
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
    uint32_t rngSeed = 1;
    float gainLPrev = -1.0f, gainRPrev = -1.0f;
};

//==============================================================================
/** Sub oscillator: sine or square, one octave below. */
class SubOsc
{
public:
    void prepare (double sampleRate) noexcept { invSr = 1.0 / sampleRate; sr = sampleRate; }
    void reset (Rng& voiceRng) noexcept { phase = (double) voiceRng.uni(); }

    void render (float* outL, float* outR, int n, float freqHz, bool square, float gain) noexcept
    {
        if (gain <= 0.0001f)
            return;
        const double dt = clampf (freqHz, 0.01f, (float) (sr * 0.45)) * invSr;
        double ph = phase;
        if (square)
        {
            for (int s = 0; s < n; ++s)
            {
                float v = ph < 0.5 ? 1.0f : -1.0f;
                v += polyBlep (ph, dt);
                double t2 = ph + 0.5; t2 -= std::floor (t2);
                v -= polyBlep (t2, dt);
                const float o = v * gain * 0.9f;
                outL[s] += o; outR[s] += o;
                ph += dt; if (ph >= 1.0) ph -= 1.0;
            }
        }
        else
        {
            for (int s = 0; s < n; ++s)
            {
                const float o = std::sin ((float) ph * kTwoPi) * gain;
                outL[s] += o; outR[s] += o;
                ph += dt; if (ph >= 1.0) ph -= 1.0;
            }
        }
        phase = ph;
    }

private:
    double sr = 44100.0, invSr = 1.0 / 44100.0, phase = 0.0;
};

//==============================================================================
/** Noise generator: white or pink (Paul Kellet economy filter) with a tone tilt. */
class NoiseGen
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        lpState = hpState = 0.0f;
        b0 = b1 = b2 = 0.0f;
    }
    void reset (Rng& voiceRng) noexcept { rng.seed (voiceRng.next()); }

    /** tone: -1 (dark) .. +1 (bright) */
    void render (float* outL, float* outR, int n, bool pink, float gain, float tone) noexcept
    {
        if (gain <= 0.0001f)
            return;

        // tone tilt: negative = one-pole LP sweeping down, positive = one-pole HP sweeping up
        const float lpF = tone < 0.0f ? std::exp2 (std::log2 (20000.0f) + tone * 6.0f) : 20000.0f;
        const float hpF = tone > 0.0f ? 20.0f * std::exp2 (tone * 7.0f) : 0.0f;
        const float lpC = 1.0f - std::exp (-kTwoPi * clampf (lpF, 40.0f, 20000.0f) / (float) sr);
        const float hpC = hpF > 0.0f ? 1.0f - std::exp (-kTwoPi * clampf (hpF, 20.0f, 8000.0f) / (float) sr) : 0.0f;

        for (int s = 0; s < n; ++s)
        {
            float v = rng.bi();
            if (pink)
            {
                b0 = 0.99765f * b0 + v * 0.0990460f;
                b1 = 0.96300f * b1 + v * 0.2965164f;
                b2 = 0.57000f * b2 + v * 1.0526913f;
                v  = (b0 + b1 + b2 + v * 0.1848f) * 0.25f;
            }
            lpState += lpC * (v - lpState);
            v = lpState;
            if (hpC > 0.0f)
            {
                hpState += hpC * (v - hpState);
                v -= hpState;
            }
            const float o = v * gain * 0.8f;
            outL[s] += o;
            outR[s] += o;
        }
    }

private:
    double sr = 44100.0;
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, lpState = 0, hpState = 0;
};

} // namespace ydc
