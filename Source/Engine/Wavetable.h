// GLOBUS — immutable mip-mapped wavetable data.
//
// A bank is a set of frames (single-cycle waves) morphable via the WT Position
// parameter. Every bank is preprocessed into band-limited mip levels:
//   level L: tableSize = max(64, kFrameSize >> L), harmonics ≤ kBaseHarmonics >> L.
// Playback picks the level from the effective phase increment (plus a warp
// slope bias) so no partial ever lands above ~0.45·fs, and crossfades between
// adjacent levels to avoid switching artefacts.
//
// Banks are IMMUTABLE once built. Factory banks live in a process-wide library
// generated deterministically at first use (message thread — never the audio
// thread). The audio thread only ever dereferences const pointers.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include "DspUtils.h"

namespace ydc
{
constexpr int kWtFrameSize     = 2048;  // level-0 samples per frame
constexpr int kWtBaseHarmonics = 512;   // level-0 harmonic cap (¼ of Nyquist headroom)
constexpr int kWtNumLevels     = 10;    // level 9 cap = 1 harmonic (pure sine)
constexpr int kWtGuard         = 4;     // interpolation guard samples per frame row
constexpr int kWtMaxFrames     = 256;

/** One immutable, fully preprocessed wavetable bank. */
struct WavetableBank
{
    juce::String name;
    juce::String category;      // Analog / Digital / Harmonic / Formant / Metallic / Motion / Soft / Aggressive / Experimental / User
    juce::String description;
    bool isFactory = true;

    int numFrames = 0;

    struct Level
    {
        int tableSize = 0;                 // samples per frame at this level
        int rowStride = 0;                 // tableSize + kWtGuard
        std::vector<float> data;           // numFrames × rowStride, guard-wrapped
    };
    std::vector<Level> levels;             // kWtNumLevels entries

    /** Row layout: g[0] = d[size-1], g[1..size] = d[0..size-1], g[size+1] = d[0], g[size+2] = d[1].
        Catmull-Rom for phase p reads offsets i .. i+3 with i = floor(p·size) (0-based into the row). */
    const float* frameRow (int level, int frame) const noexcept
    {
        const auto& lv = levels[(size_t) level];
        return lv.data.data() + (size_t) frame * (size_t) lv.rowStride;
    }

    /** Catmull-Rom read of one frame at one level. phase01 in [0,1). */
    inline float readFrame (int level, int frame, float phase01) const noexcept
    {
        const auto& lv = levels[(size_t) level];
        const float p  = phase01 * (float) lv.tableSize;
        const int   i  = (int) p;                       // 0 .. tableSize-1
        const float t  = p - (float) i;
        const float* r = frameRow (level, frame) + i;   // r[0] = d[i-1] via +1 layout shift
        // r points at g[i]; with the +1 shift, g[i] = d[i-1]
        const float y0 = r[0], y1 = r[1], y2 = r[2], y3 = r[3];
        const float a = 0.5f * (3.0f * (y1 - y2) + y3 - y0);
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = 0.5f * (y2 - y0);
        return ((a * t + b) * t + c) * t + y1;
    }

    /** Linear read (ECO paths / previews). */
    inline float readFrameLinear (int level, int frame, float phase01) const noexcept
    {
        const auto& lv = levels[(size_t) level];
        const float p  = phase01 * (float) lv.tableSize;
        const int   i  = (int) p;
        const float t  = p - (float) i;
        const float* r = frameRow (level, frame) + i + 1;  // r[0] = d[i]
        return r[0] + t * (r[1] - r[0]);
    }
};

//==============================================================================
/** Normalisation policy for bank building (documented in the manual):
      PerFrame  — factory banks: each frame gain = min(0.25/rms, 0.9/peak) so the
                  position axis keeps consistent perceived loudness.
      TablePeak — user imports: ONE gain for the whole table (max peak → 0.9) so
                  intentional frame dynamics survive; never per-frame destructive.
      None      — internal shape banks: mathematical amplitudes preserved.
    DC is always removed in the spectral domain (bin 0 dropped). */
enum class WtNormalise { PerFrame, TablePeak, None };

/** Builds an immutable bank from raw frames (each kWtFrameSize samples).
    Runs FFT preprocessing — call OFF the audio thread only. */
std::unique_ptr<WavetableBank> buildWavetableBank (const juce::String& name,
                                                   const juce::String& category,
                                                   const juce::String& description,
                                                   const std::vector<std::vector<float>>& frames,
                                                   bool isFactory,
                                                   WtNormalise normalise = WtNormalise::PerFrame);

//==============================================================================
/** Process-wide immutable factory wavetable library. First call generates all
    banks deterministically (~a few MB, done once, message thread). */
class WavetableLibrary
{
public:
    static const WavetableLibrary& get();

    const std::vector<std::unique_ptr<WavetableBank>>& banks() const noexcept { return list; }
    int numBanks() const noexcept { return (int) list.size(); }

    /** nullptr when the name is unknown. */
    const WavetableBank* byName (const juce::String& name) const noexcept
    {
        for (const auto& b : list)
            if (b->name == name)
                return b.get();
        return nullptr;
    }

    const WavetableBank* defaultBank() const noexcept { return list.front().get(); }

    /** Internal mip-mapped shape bank for the BASIC HQ engine
        (frame 0 = saw, frame 1 = triangle; NOT normalised, not browsable). */
    const WavetableBank& hqShapes() const noexcept { return *hqBank; }

private:
    WavetableLibrary();
    std::vector<std::unique_ptr<WavetableBank>> list;
    std::unique_ptr<WavetableBank> hqBank;
};

//==============================================================================
/** Mip level selection: returns fractional level for a phase increment
    (freq/fs) plus a warp slope bias in levels. 0 → use level 0 only.

    The +1 shift keeps floor-selection safe across each level's whole range:
    with cap(L) = kWtBaseHarmonics >> L, the selected level's top partial spans
    0.225·fs (range bottom) … 0.45·fs (range top) — never above Nyquist. */
inline float wtLevelForIncrement (double phaseInc, float slopeBiasLevels) noexcept
{
    const double f0max = 0.45 / (double) kWtBaseHarmonics;   // in cycles/sample
    if (phaseInc <= 0.0)
        return 0.0f;
    const float raw = (float) std::log2 (phaseInc / f0max) + 1.0f + slopeBiasLevels;
    return clampf (raw, 0.0f, (float) (kWtNumLevels - 1));
}

} // namespace ydc
