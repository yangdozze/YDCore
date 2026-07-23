# Changelog

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
