# GLOBUS

**GLOBUS** is an original professional polyphonic software synthesizer by
**Ninth Parallel Audio**. VST3 + Standalone, C++17, built on the JUCE framework.
Original DSP, original tabbed UI, original presets.

![Engine](https://img.shields.io/badge/voices-32-blue) ![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20Standalone-8b7cf8) ![License](https://img.shields.io/badge/license-AGPL--3.0-lightgrey)

## Features

- **Tabbed workstation interface** — OSC / FX / MATRIX / GLOBAL / PRESETS pages
  behind a persistent top bar with preset navigation, Save/Init/Randomize,
  MIDI activity LED, CPU and voice meters and a real stereo output meter
- **32-voice polyphonic engine** with Poly / Mono / Legato modes, portamento,
  note priority, sustain pedal, click-free voice stealing
- **2 oscillators** with live waveform displays: sine, triangle, saw, square,
  pulse (PW), supersaw (7-stack), noise; octave/semi/fine, level, pan,
  phase/random phase, 1–7 voice unison with detune & stereo spread, analog drift
- **Sub oscillator** (sine/square, −1 octave) and **noise generator** (white/pink + tone)
- **Multimode filter**: LP12/LP24/HP12/HP24/BP/Notch (TPT SVF), cutoff, resonance,
  drive, key tracking, envelope amount — stable at high resonance
- **3 envelopes**: amp, filter, mod (with destination routing) — live envelope graphs
- **2 LFOs**: 5 shapes incl. S&H, Hz or tempo-sync (1/1 … 1/32 incl. triplets),
  fade-in, phase, bipolar/unipolar, retrigger or free-running, quick-assign destination
- **8-slot modulation matrix** with per-slot activity indicators: velocity,
  mod wheel, aftertouch, key tracking, 3 envelopes, 2 LFOs, per-note random,
  pitch bend → pitch, fine, level, pan, PW, cutoff, resonance, amp, LFO rates,
  FX mix, stereo width
- **Arpeggiator**: up/down/up-down/random, tempo-synced rates, gate, 1–3 octaves, hold
- **Master FX**: distortion, chorus, tempo-synced delay, reverb, 3-band EQ —
  individually bypassable with click-free crossfades
- **Preset bank**: 51 factory presets in 10 categories with author &
  description metadata, three-column browser (sources & categories / searchable
  list / info panel), favorites that persist, keyboard navigation,
  save/save-as (portable JSON), init & randomize, state saved with the project
- **~145 host-automatable parameters** with stable IDs and sensible ranges

## Requirements

| | |
|---|---|
| **Windows** | Windows 10/11 x64, Visual Studio 2022+ (Desktop C++), CMake 3.22+ |
| **Linux** | GCC 11+/Clang, CMake 3.22+, Ninja, X11/ALSA dev packages (see CI file) |
| **JUCE** | 8.0.15 — fetched automatically at configure time (or place a checkout in `libs/JUCE`) |

No paid or proprietary dependencies.

## Building

### Windows x64 (MSVC)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

(If your runner/IDE ships a newer Visual Studio, `cmake -B build -A x64` with the
default generator works identically — that is what CI does.)

Outputs:

- VST3 → `build\YDCore_artefacts\Release\VST3\GLOBUS.vst3`
- Standalone → `build\YDCore_artefacts\Release\Standalone\GLOBUS.exe`
- Tests → `build\YDCoreTests_artefacts\Release\YDCoreTests.exe`

(Internal target/folder names keep the original `YDCore` identifiers on purpose —
this preserves plugin IDs, saved host projects and CI paths across the rebrand.)

### Linux

```bash
sudo apt-get install ninja-build libasound2-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libfreetype6-dev libfontconfig1-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Verification suite

```bash
./build/YDCoreTests_artefacts/Release/YDCoreTests            # exit code 0 = pass
./build/YDCoreTests_artefacts/Release/YDCoreTests --screenshots shots   # per-tab UI captures
```

430+ checks: audio at 44.1/48 kHz across buffer sizes 32–4096, note-on/off click
bounds, NaN/denormal freedom, all factory presets load & sound (incl. brand
metadata), state save/restore round-trip, rapid preset switching during
playback, poly/mono behaviour, 500+ random-note stress, automation sweeps,
arpeggiator output, parameter ID uniqueness, user preset save/reload.

## Installing (FL Studio, Windows)

1. Download `GLOBUS-VST3-Windows-x64.zip` from the GitHub Actions build
   artifacts / `ci-latest` release (or build locally as above).
2. Extract it and copy the **`GLOBUS.vst3`** folder into:
   `C:\Program Files\Common Files\VST3\`
3. Start FL Studio → **Options ▸ Manage plugins** → **Find installed plugins**.
4. GLOBUS appears under **Installed ▸ Generators ▸ VST3** (vendor
   *Ninth Parallel Audio*). Add it to the channel rack and play.

Upgrading from YD Core 1.0: GLOBUS replaces it seamlessly — the plugin IDs are
unchanged, so existing projects load with all sounds intact. User presets are
now stored in `Documents\GLOBUS\Presets\` (the old `Documents\YDCore\Presets\`
folder is still read, and favorites migrate automatically).

## Using the plugin

- **Top bar**: GLOBUS logo, page tabs, preset display (click for the bank),
  ◀ ▶ navigation, SAVE, INIT, RAND, MIDI LED, CPU/voices, output meter.
- **PRESETS page**: sources & categories on the left, searchable list in the
  middle (↑/↓ + Enter to load, double-click, star = favorite, Esc returns to
  OSC), info panel with author/description plus Load / Delete / Init / Random /
  Save As on the right.
- **Knobs**: drag = adjust, **Shift-drag = fine**, **double-click = reset**,
  hover shows the exact value. Every control has a tooltip.
- The window resizes freely from 900×600 up to 2400×1520 (layout scales).

## Known limitations (non-blocking)

- Free-running (non-retriggered) LFOs ignore per-voice *LFO Rate* modulation;
  global sources (wheel, aftertouch, pitch bend) still modulate their rate.
- FX-mix and stereo-width matrix destinations respond to global modulation
  sources (wheel/AT/bend/LFOs), not per-voice envelopes — the FX bus is shared.
- Triangle oscillator is naive (not BLEP-corrected); inaudible aliasing except
  at extreme pitches.
- The distortion stage is not oversampled (CPU economy).
- MIDI CC hard-wiring: CC1 (mod wheel), CC64 (sustain), CC120/123 (all sound/
  notes off), pitch bend, channel & poly aftertouch. Other CCs are available
  through host automation rather than a MIDI-learn system.

## Project structure

```
Source/
  Parameters.*           parameter contract (IDs, layout, tooltips)
  PluginProcessor.*      MIDI → arp → engine → FX; state save/restore; metering
  PluginEditor.*         top bar + five tabbed pages, scale-to-fit
  Engine/                DspUtils, Oscillator, Filter, Envelope, LFO,
                         ModMatrix, Voice, SynthEngine, Arpeggiator
  Effects/               Distortion, ChorusFx, DelayFx, ReverbFx, EqFx, EffectsChain
  Presets/               PresetManager (JSON factory + user presets, favorites)
  UI/                    LookAndFeel, controls, waveform/envelope displays,
                         top bar, pages, preset bank
Presets/                 51 factory presets (JSON + single-file bundle)
Tests/                   headless verification suite + screenshot tool
tools/gen_presets.py     factory preset generator
.github/workflows/       Windows x64 + Linux CI
```

## License

AGPL-3.0 (matching the JUCE open-source license). © 2026 Ninth Parallel Audio.
"GLOBUS" and "Ninth Parallel Audio" are original fictional identities created
for this project; no third-party presets, samples, artwork or code beyond JUCE
itself are included.
