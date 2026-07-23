# YD Core

**YD Core** is a polyphonic workstation-style software synthesizer by **Yangdozze**.
VST3 + Standalone, C++17, built on the JUCE framework. Original DSP, original UI,
original presets — inspired by the *workflow* of modern workstation synths, copied
from none of them.

![Engine](https://img.shields.io/badge/voices-32-blue) ![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20Standalone-8b7cf8) ![License](https://img.shields.io/badge/license-AGPL--3.0-lightgrey)

## Features

- **32-voice polyphonic engine** with Poly / Mono / Legato modes, portamento,
  note priority, sustain pedal, click-free voice stealing
- **2 oscillators**: sine, triangle, saw, square, pulse (PW), supersaw (7-stack),
  noise; octave/semi/fine, level, pan, phase/random phase, 1–7 voice unison with
  detune & stereo spread, analog drift
- **Sub oscillator** (sine/square, −1 octave) and **noise generator** (white/pink + tone)
- **Multimode filter**: LP12/LP24/HP12/HP24/BP/Notch (TPT SVF), cutoff, resonance,
  drive, key tracking, envelope amount — stable at high resonance
- **3 envelopes**: amp, filter, mod (with destination routing) — live envelope graphs
- **2 LFOs**: 5 shapes incl. S&H, Hz or tempo-sync (1/1 … 1/32 incl. triplets),
  fade-in, phase, bipolar/unipolar, retrigger or free-running, quick-assign destination
- **8-slot modulation matrix**: velocity, mod wheel, aftertouch, key tracking,
  3 envelopes, 2 LFOs, per-note random, pitch bend → pitch, fine, level, pan, PW,
  cutoff, resonance, amp, LFO rates, FX mix, stereo width
- **Arpeggiator**: up/down/up-down/random, tempo-synced rates, gate, 1–3 octaves, hold
- **Master FX**: distortion, chorus, tempo-synced delay, reverb, 3-band EQ —
  individually bypassable with click-free crossfades
- **Preset system**: 51 factory presets in 10 categories, browser with search &
  favorites, save/save-as (portable JSON), init & randomize, state saved with the project
- **~145 host-automatable parameters** with stable IDs and sensible ranges

## Requirements

| | |
|---|---|
| **Windows** | Windows 10/11 x64, Visual Studio 2022 (Desktop C++), CMake 3.22+ |
| **Linux** | GCC 11+/Clang, CMake 3.22+, Ninja, X11/ALSA dev packages (see CI file) |
| **JUCE** | 8.0.15 — fetched automatically at configure time (or place a checkout in `libs/JUCE`) |

No paid or proprietary dependencies.

## Building

### Windows x64 (Visual Studio 2022)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Outputs:

- VST3 → `build\YDCore_artefacts\Release\VST3\YD Core.vst3`
- Standalone → `build\YDCore_artefacts\Release\Standalone\YD Core.exe`
- Tests → `build\YDCoreTests_artefacts\Release\YDCoreTests.exe`

The GitHub Actions workflow (`.github/workflows/windows-build.yml`) performs this
exact build on a Windows x64 runner and uploads both binaries as artifacts.

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
./build/YDCoreTests_artefacts/Release/YDCoreTests   # exit code 0 = pass
```

Renders the real processor offline and checks: audio output at 44.1/48 kHz across
buffer sizes 32–4096, note-on/off click bounds, NaN/denormal freedom, all factory
presets load & sound, state save/restore round-trip, rapid preset switching during
playback, poly/mono behaviour, 500+ random-note stress, automation sweeps,
arpeggiator output, parameter ID uniqueness, user preset save/reload.

## Installing (FL Studio, Windows)

1. Download `YDCore-VST3-Windows-x64.zip` from the GitHub Actions build artifacts
   (or build locally as above).
2. Extract it and copy the **`YD Core.vst3`** folder into:
   `C:\Program Files\Common Files\VST3\`
3. Start FL Studio → **Options ▸ Manage plugins** → make sure the VST3 path above
   is listed → **Find installed plugins**.
4. YD Core appears under **Installed ▸ Generators ▸ VST3** (vendor *Yangdozze*).
   Add it to the channel rack and play.

User presets are stored in `Documents\YDCore\Presets\` as portable JSON files.

## Using the plugin

- **Top bar**: preset name (click to open the browser), ◀ ▶ preset navigation,
  SAVE (save-as), INIT, RAND, MIDI activity LED, CPU/voice meter.
- **Preset browser**: category list, search box, ★ favorites (click the star),
  double-click to load, save-as row at the bottom.
- **Knobs**: drag = adjust, **Shift-drag = fine**, **double-click = reset to default**.
  Every control has a tooltip.
- The window resizes freely from 900×600 upward (the layout scales).

## Known limitations (non-blocking)

- Free-running (non-retriggered) LFOs ignore per-voice *LFO Rate* modulation;
  global sources (wheel, aftertouch, pitch bend) still modulate their rate.
  Retriggered LFOs honour all rate modulation per voice.
- FX-mix and stereo-width matrix destinations respond to global modulation
  sources (wheel/AT/bend/LFOs), not per-voice envelopes — the FX bus is shared.
- Triangle oscillator is naive (not BLEP-corrected); inaudible aliasing except at
  extreme pitches.
- The distortion stage is not oversampled (CPU economy); at maximum drive with
  very bright material some aliasing can be provoked.
- MIDI CC hard-wiring: CC1 (mod wheel), CC64 (sustain), CC120/123 (all sound/notes
  off), pitch bend, channel & poly aftertouch. Other CCs are available through
  host automation of parameters rather than a MIDI-learn system.

## Project structure

```
Source/
  Parameters.*           parameter contract (IDs, layout, tooltips)
  PluginProcessor.*      MIDI → arp → engine → FX; state save/restore
  PluginEditor.*         1200×760 scalable UI shell
  Engine/                DspUtils, Oscillator, Filter, Envelope, LFO,
                         ModMatrix, Voice, SynthEngine, Arpeggiator
  Effects/               Distortion, ChorusFx, DelayFx, ReverbFx, EqFx, EffectsChain
  Presets/               PresetManager (JSON factory + user presets)
  UI/                    LookAndFeel, controls, sections, browser, top bar
Presets/                 51 factory presets (JSON, embedded at build time)
Tests/                   headless verification suite
tools/gen_presets.py     factory preset generator
.github/workflows/       Windows x64 + Linux CI
```

## License

AGPL-3.0 (matching the JUCE open-source license). © 2026 Yangdozze.
"YD Core" and "Yangdozze" identify this original work; no third-party presets,
samples, artwork or code beyond JUCE itself are included.
