# GLOBUS wavetables — banks, playback and WAV import

## The engine

Each of the two main oscillators has an **Engine** selector:

| Engine | What it is |
|---|---|
| **LEGACY** | The exact GLOBUS 1.1 oscillator. Old presets and saved projects select it automatically and sound identical. |
| **BASIC HQ** | The classic waveforms rendered by harmonic table selection: every partial is placed below ~0.45·fs by construction, so triangle/saw/square/pulse alias measurably less than LEGACY — most audibly in the high octaves. |
| **WAVETABLE** | The 1.2 wavetable engine described here. |

Wavetable playback uses 2048-sample frames preprocessed into 10 band-limited
mip levels (each level halves the allowed harmonics and the table length).
The level is chosen per unison voice from its actual pitch — plus a warp-slope
bias — and neighbouring levels crossfade, so there are no switching steps
across the keyboard. Intra-frame reads are Catmull-Rom interpolated; the
**POS** control (and its modulation) morphs linearly between adjacent frames
with per-sample ramping, so even fast position modulation stays click-free.

At **HIGH/ULTRA** quality the engine blends two mip levels per read;
**ECO/LEGACY** quality uses the nearest level (cheaper, still band-limited).

## Warp modes

| Mode | Behaviour | Anti-aliasing strategy |
|---|---|---|
| **Bend +/−** | Rational phase bend toward the end/start of the frame | mip bias = warp slope (up to +3 levels) |
| **Sync** | Windowed hard sync: the frame replays up to 16× per cycle and the last 3 % of the master period fades toward the cycle-start sample, bounding the wrap discontinuity | mip level follows the effective (multiplied) frequency |
| **Asymmetry** | Piecewise-linear phase knee — pulse-width for wavetables | mip bias = knee slope |
| **Mirror** | Ping-pong frame read blended in by amount | +1 level at full amount |

All warp inputs are division-safe and bounded; warp amount is smoothed per
sample. Sync is deliberately a *windowed* sync — a documented approximation
that trades a slightly softened wrap for stable, band-limited behaviour.

## Factory banks

27 banks × 9 categories, generated **deterministically in code** at first load
(the repository stays text-only; no binary tables are shipped or downloaded):

- **Analog** — Prime Shapes (the init bank), Analog Warmth, Vintage Stack
- **Digital** — Bitframe, Pulse Matrix, Digital Steps
- **Harmonic** — Harmonic Rise, Odd Order, Spectral Comb
- **Formant** — Vowel Morph, Formant Sweep, Vocal Glass
- **Metallic** — Bell Alloy, FM Chrome, Ring Shift
- **Motion** — Orbit, Slow Tide, Wave Traveller
- **Soft** — Soft Glass, Breath Pad, Velvet
- **Aggressive** — Saw Stack, Growl Formant, Torn Edge
- **Experimental** — Random Walk, Fractal Fold, Glitch Line

Factory normalisation policy: per frame, gain = min(0.25/RMS, 0.9/peak) —
consistent loudness across the position axis. DC is removed spectrally.

## The browser

Click the wavetable name button on an oscillator (WAVETABLE engine) to open the
browser: category filter, search, metadata panel, import. **Opening the browser
never changes the sound** — a bank is applied only when you click it. The ◀ ▶
arrows next to the name step through the current filtered list.

## Importing your own WAV

**IMPORT WAV...** in the browser (or drop a file there) accepts:

- **mono or stereo** WAV — stereo is downmixed by averaging both channels
  (documented policy; import a mono file to avoid any downmix)
- integer PCM **8/16/24/32-bit** and **32-bit float**
- **multi-frame tables**: a length divisible by **2048** is read as 2048-sample
  frames (the standard convention, up to 256 frames). Lengths divisible by
  4096/1024/512/256 are segmented at that size and each cycle is resampled to
  2048. Note an even count of 1024-sample cycles is indistinguishable from
  2048-sample frames — the 2048 convention wins.
- **single cycles**: any file up to 65 536 samples that doesn't match a frame
  grid is treated as one cycle and resampled (Catmull-Rom) to 2048.

Import runs on a worker thread — the audio never glitches — and preprocessing
(band-limited mip levels) happens before the table is activated. User tables
are normalised with **one gain for the whole table** (peak → 0.9), so
intentional frame-to-frame dynamics survive; individual frames are never
rewritten.

Malformed, truncated, oversized (>64 MB) or unsupported files are rejected
with a clear message and the current sound is untouched.

### Where imports live, and missing files

Imported WAVs are **copied** into `Documents/GLOBUS/Wavetables/` (a different
file with the same name gets a numbered sibling), and presets/projects
reference the table by name (`User/<name>`). On another machine, copy that
folder along with your presets. If a referenced file is missing, GLOBUS keeps
the reference in your project, plays the default bank instead, and shows a
**MISSING FILE** badge on the oscillator display — restoring the WAV to the
folder fully recovers the sound.
