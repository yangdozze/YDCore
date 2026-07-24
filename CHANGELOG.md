# Changelog

## 1.2.0 — 2026-07-24 — wavetable engine & sound-quality upgrade

Every change is **append-only**: all 1.1 parameter IDs, choice orders, preset
names, the state tag, plugin codes and the installer identity are unchanged.
Old projects and the original 51 presets load bit-compatibly and select the
LEGACY paths automatically (verified sample-exactly against recorded 1.1.0
renders of all 51 presets at 44.1/48/96 kHz).

- **Three oscillator engines** (per oscillator, appended parameter):
  **LEGACY** (exact 1.1 sound), **BASIC HQ** (band-limited classic waves via
  harmonic table selection — the naive triangle is fixed here, and saw/square/
  pulse alias measurably less at high pitches), **WAVETABLE** (new engine).
- **Original wavetable engine**: 27 deterministic factory banks in 9 categories,
  2048-sample frames, 10 crossfaded band-limited mip levels, Catmull-Rom reads,
  click-safe position morphing, warp modes (Bend ±, windowed Sync, Asymmetry,
  Mirror) with slope-aware anti-aliasing. Compact browser (categories, search,
  metadata, import) that never changes the sound on opening; ◀ ▶ bank stepping.
- **User wavetable import**: mono/stereo WAV (documented average downmix),
  integer 8/16/24/32-bit + float32, 2048-frame tables, 4096/1024/512/256
  segmentation, single-cycle resampling; worker-thread import; versioned asset
  copies in `Documents/GLOBUS/Wavetables`; missing files show a badge and fall
  back audibly without losing the reference. Malformed files fail safely.
- **Appended modulation destinations**: WT position, warp amount, unison detune
  and stereo spread per oscillator, plus filter drive; appended LFO quick-assign
  “WT Pos 1+2”.
- **New filter models (appended)**: Ladder 24 (ZDF, drive, self-osc edge),
  OTA 24 (soft-clipped cascade), SEM 12 (style-inspired smooth multimode),
  BP 24 — nonlinear cores oversample 2× at HIGH/ULTRA quality.
- **HQ unison** for the new engines: deterministic placement, outer-voice gain
  taper with power compensation (1–7 voices stay within a fraction of a dB on
  average), equal-power spread, golden-ratio phases, mono-fold safe.
- **Envelope curve controls (appended)**: attack/decay/release shape per
  envelope; “Classic” reproduces the calibrated 1.1 curves exactly
  (decay τ=t/3, release τ=t/4, attack overshoot 1.28).
- **Global quality modes** LEGACY/ECO/HIGH/ULTRA (see `docs/QUALITY_MODES.md`):
  2×/4× oversampled distortion with DC blocking, Catmull-Rom chorus/delay
  interpolation, an original 8-line FDN reverb with damping and subtle
  modulation, smoothed EQ. Latency reported honestly (0/0/2/4 samples).
- **20 new factory presets** on the new engines (Bass, Lead, Pad, Pluck, Keys,
  Sequence, Atmosphere, FX, Digital, Analog) — 71 factory presets total; the
  original 51 are byte-identical. Two new presets are intentionally non-Poly
  (Neon Vector Bass = Mono, Glass Caller = Legato).
- **Randomizer 2.1**: NORMAL/WILD pick the new engines, curated banks and warp
  ranges; SUBTLE never flips engine or bank; locks/undo now cover the wavetable
  selection; never touches quality mode; never creates dangling table refs.
  Stress-tested 1001 runs per mode.
- **Regression harness**: `YDCoreTests --baseline write|check <dir>` renders
  all factory presets at 44.1/48/96 kHz for sample-exact engine comparisons.
- Suite grown from 508 to **1032 checks** (engine contracts, migration, banks,
  warp, aliasing measurements, filters, curves, quality switching, FX paths,
  import fuzzing, unison/mono gain staging, preset validation).
- Old host states missing the new parameters now explicitly reset them to
  LEGACY defaults on load (fixes a latent JUCE `replaceState` edge where new
  parameters could inherit values from a previously loaded project).

Signing status unchanged: artifacts are unsigned development builds
(SmartScreen shows “Unknown publisher”); the pipeline stays signing-ready.

## 1.1.0 quality update — 2026-07-24

- **Polyphony fix:** a note-off now releases exactly ONE voice of its pitch
  (previously every same-pitch voice was stopped), and poly mode no longer
  inherits portamento glide from the previously played note. Verified with a
  new pitch-energy regression suite (overlaps, chords, repeated notes, sustain,
  stealing, priorities, all 51 presets). Preset mode census: 39 Poly, 9 Mono,
  3 Legato (intentional), 6 with arpeggiator — the "silent second note" on
  mono/legato presets is by design and the play mode is now displayed in the
  top bar.
- **Compact UI:** signal-flow OSC page (oscillators → sub/noise → filter →
  tabbed modulation editor with live envelope/LFO graphs → keyboard), FX page
  as a real left-to-right chain with unmistakable ON/BYPASSED states and live
  mix readouts, denser matrix rows with slot activity, tighter GLOBAL page.
  Top bar adds play-mode badge, modified-preset indicator, randomize strength
  menu and undo. Keyboard-focus rings, hover value readouts everywhere.
- **New parameters:** Sub Pan and Noise Pan (real DSP; default centre, fully
  backward compatible).
- **Randomizer 2.0:** SUBTLE / NORMAL / WILD strengths, five section locks
  (OSC, FILTER, ENVELOPES, LFO/MATRIX, FX) via padlocks and the RAND menu,
  one-step Undo Randomize, musical safety rules (guaranteed audible source,
  bounded resonance/feedback/mix, interval-based tuning, no duplicate matrix
  routes, protected master/play-mode/bend/velocity/arp). 1000+ automated
  randomizer runs in the test suite.
- **CI:** SHA-256 checksums published with every release; secure
  signing-ready pipeline (Azure Trusted Signing via encrypted secrets — see
  docs/SIGNING.md). Artifacts are clearly labeled unsigned until credentials
  are provided.
- Test suite grown to 500+ checks.
- **Repository renamed** to github.com/yangdozze/GLOBUS (history, releases and
  Actions preserved; the old YDCore URL redirects automatically).

## 1.1.0 — 2026-07-23 — "GLOBUS"

Rebrand + interface generation 2.

- **New identity:** the synthesizer is now **GLOBUS** by **Ninth Parallel
  Audio** (plugin name, vendor, bundle ID, UI, presets, docs, artifacts).
  Plugin 4-char codes and state format unchanged — existing projects and
  saved sounds load as before.
- **Tabbed interface:** five real pages — OSC, FX, MATRIX, GLOBAL, PRESETS —
  behind a persistent top bar (preset navigation, Save/Init/Random, MIDI LED,
  CPU, voice count, stereo output meter).
- **Live oscillator waveform displays** driven by the actual wave/PW/unison
  parameters; larger two-row oscillator layout.
- **Modulation matrix page:** numbered slots with live activity indicators and
  editable amount value boxes; bipolar sliders now fill from centre.
- **Global page:** play controls, arpeggiator, output and a live status panel
  (MIDI input, sustain pedal, CPU and voice meters).
- **Preset bank:** three-column browser — sources & categories, searchable
  list with favorites and keyboard navigation (↑/↓, Enter, Esc), info panel
  with author & description, Load / Delete / Init / Random / Save As.
- **Preset metadata:** all 51 factory presets carry author and description
  fields. User presets move to Documents/GLOBUS/Presets (legacy YDCore folder
  still read; favorites migrate automatically).
- Knob value readouts on hover/drag; polished hover/pressed states throughout.
- Output metering added to the processor (lock-free).
- Verification suite extended to 430+ checks incl. brand metadata and
  per-tab screenshot capture (`--screenshots <dir>`).
- **Windows installer**: dark-themed Inno Setup wizard
  (`GLOBUS-Installer-Windows-x64.exe`) with original globe artwork and icon,
  selectable VST3/Standalone components, Start Menu + optional Desktop
  shortcuts, optional launch-after-install, in-place upgrades, proper
  uninstaller — user presets (including legacy YDCore folders) are never
  touched. Compiled and silently install/verify/uninstall-tested on CI.

## 1.0.0 — 2026-07-23

Initial release.

- 32-voice polyphonic engine (poly / mono / legato, portamento, note priority,
  sustain pedal, click-free stealing)
- 2 band-limited oscillators (PolyBLEP) with 7-voice unison, supersaw, drift;
  sub oscillator; white/pink noise with tone control
- Multimode TPT SVF filter (LP12/24, HP12/24, BP, notch) with drive, key
  tracking and envelope amount
- 3 ADSR envelopes (amp / filter / mod with routable destination) with live
  envelope displays
- 2 LFOs with tempo sync (1/1–1/32 incl. triplets), fade-in, phase, retrigger,
  bipolar/unipolar, quick-assign
- 8-slot modulation matrix (11 sources × 18 destinations)
- Arpeggiator (up / down / up-down / random, gate, 1–3 octaves, hold, host-grid
  sync)
- Master FX chain: distortion, chorus, tempo-synced delay, reverb, 3-band EQ —
  all individually bypassable without clicks
- Preset system: 51 factory presets across 10 categories, JSON user presets,
  browser with search + favorites, init / randomize
- ~145 host-automatable parameters, full state save/restore
- Headless verification suite (330+ checks)
- CI: Windows x64 (VS 2022) VST3 + Standalone artifacts, Linux verification build
