# Changelog

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
