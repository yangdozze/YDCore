// YD Core — small audio-thread-safe DSP helpers. Header-only, no allocations.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ydc
{
/** Samples per control tick: modulation (LFO/matrix/filter coeffs) is evaluated
    at this granularity, audio is rendered per-sample. */
constexpr int kControlBlock = 32;

/** Max unison voices per oscillator, and supersaw partials. */
constexpr int kMaxUnison   = 7;
constexpr int kSuperSawN   = 7;
constexpr int kMaxPartials = kMaxUnison * kSuperSawN;

constexpr float kTwoPi = 6.283185307179586f;

/** Tiny xorshift RNG — audio-thread safe, no locks, no allocation. */
struct Rng
{
    uint32_t state = 0x9E3779B9u;
    void seed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }
    inline uint32_t next() noexcept
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }
    /** Uniform [0, 1) */
    inline float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    /** Uniform [-1, 1) */
    inline float bi() noexcept { return uni() * 2.0f - 1.0f; }
};

inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

/** Exact float equality without -Wfloat-equal noise (parameter-change caching). */
inline bool exactEq (float a, float b) noexcept { return ! (a < b) && ! (a > b); }

/** Equal-power pan: pan in [-1, 1] → L/R gains. */
inline void panGains (float pan, float& l, float& r) noexcept
{
    const float p = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kTwoPi * 0.5f; // 0..pi/2
    l = std::cos (p);
    r = std::sin (p);
}

/** Gentle cubic soft clip for the master bus safety stage (transparent below ~0.7). */
inline float softClip (float x) noexcept
{
    if (x >  1.5f) return  1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) * (4.0f / 27.0f);
}

/** semitones → frequency ratio */
inline float semisToRatio (float semis) noexcept { return std::exp2 (semis * (1.0f / 12.0f)); }

/** MIDI note (float, allows glide/detune) → Hz */
inline float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }

/** Flush denormals / non-finite garbage to zero. */
inline float sanitize (float v) noexcept
{
    if (! std::isfinite (v) || std::abs (v) < 1.0e-25f) return 0.0f;
    return v;
}

/** One-pole smoother for control values (not audio). */
struct OnePoleSmoother
{
    float value = 0.0f, coeff = 0.0f;
    void prepare (float sampleRate, float timeSec, float initial) noexcept
    {
        value = initial;
        coeff = timeSec > 0.0f ? 1.0f - std::exp (-1.0f / (timeSec * sampleRate)) : 1.0f;
    }
    inline float processTowards (float target, int numSamples) noexcept
    {
        // control-rate approximation: apply n steps of the one-pole at once
        const float k = 1.0f - std::pow (1.0f - coeff, (float) numSamples);
        value += k * (target - value);
        return value;
    }
    void snap (float v) noexcept { value = v; }
};

} // namespace ydc
