# Changelog

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
