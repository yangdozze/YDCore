// GLOBUS v1.2 — user wavetable import (WAV → immutable mip-mapped bank).
//
// Runs on a worker or message thread — NEVER the audio thread. The audio thread
// only ever sees the finished immutable bank via the processor's atomic pointer.
//
// Supported input (documented in docs/WAVETABLES.md):
//   • mono WAV, or stereo WAV (downmix policy: average of both channels)
//   • integer PCM 8/16/24/32-bit and 32-bit float
//   • single-cycle files (any length ≤ 65536 → resampled to one 2048 frame)
//   • multi-frame tables: length divisible by 2048 (up to 256 frames), or by
//     4096/1024/512/256 (each cycle resampled to 2048)
//   • explicit frameSizeHint overrides auto-detection
// Normalisation: ONE gain for the whole table (peak → 0.9) — frame dynamics are
// preserved; per-frame levels are never rewritten. DC is removed spectrally.
// Malformed, truncated, oversized or unsupported files fail with a clear error
// and never touch the current sound.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Wavetable.h"

namespace ydc
{
/** User bank names live in their own namespace so factory names can never be
    shadowed: "User/<basename>". */
inline juce::String userWavetablePrefix() { return "User/"; }

struct WavetableImportResult
{
    std::unique_ptr<WavetableBank> bank;   // null on failure
    juce::String error;                    // human-readable reason on failure
    int framesDetected = 0;
    int sourceFrameSize = 0;
    bool wasStereo = false;

    bool ok() const noexcept { return bank != nullptr; }
};

/** Import a WAV file into a bank. frameSizeHint 0 = auto-detect. */
WavetableImportResult importWavetableFromFile (const juce::File& file, int frameSizeHint = 0);

/** The user wavetable asset directory: Documents/GLOBUS/Wavetables. Imported
    files are copied here (versioned copies — an existing different file gets a
    numbered sibling) so saved projects and presets can re-resolve their tables. */
juce::File userWavetableDirectory();

/** Copy `source` into the asset directory and return the asset file actually
    referenced (may be a numbered sibling when a different file with the same
    name already exists). Returns an invalid File on copy failure. */
juce::File copyToWavetableAssets (const juce::File& source);

} // namespace ydc
