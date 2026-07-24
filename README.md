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
- **Three oscillator engines per oscillator** *(new in 1.2)*:
  **LEGACY** (the exact 1.1 sound for old presets and projects),
  **BASIC HQ** (band-limited classic waves via harmonic table selection —
  measurably lower aliasing, including a fully band-limited triangle) and
  **WAVETABLE** (original mip-mapped wavetable engine)
- **Original wavetable engine** *(new in 1.2)*: 27 factory banks in 9 musical
  categories (Analog, Digital, Harmonic, Formant, Metallic, Motion, Soft,
  Aggressive, Experimental), 2048-sample frames, position morphing, warp modes
  (Bend ±, windowed Sync, Asymmetry, Mirror), frequency-aware anti-aliasing with
  crossfaded mip levels, a compact browser with categories/search/import, and
  **user WAV import** (single-cycle and multi-frame; see `docs/WAVETABLES.md`)
- **2 oscillators** with live waveform/wavetable displays: sine, triangle, saw,
  square, pulse (PW), supersaw (7-stack), noise; octave/semi/fine, level, pan,
  phase/random phase, 1–7 voice unison with detune & stereo spread, analog drift
- **Sub oscillator** (sine/square, −1 octave) and **noise generator** (white/pink + tone)
- **Multimode filter**: LP12/LP24/HP12/HP24/BP/Notch (TPT SVF) plus *(new in
  1.2, appended)* **Ladder 24**, **OTA 24**, **SEM 12** (style-inspired
  originals with quality-aware oversampling) and **BP 24** — cutoff, resonance,
  drive, key tracking, envelope amount — stable at high resonance
- **3 envelopes**: amp, filter, mod (with destination routing) — live envelope
  graphs, plus *(new in 1.2)* appended attack/decay/release **curve controls**
  whose Classic setting reproduces the calibrated 1.1 shapes exactly
- **2 LFOs**: 5 shapes incl. S&H, Hz or tempo-sync (1/1 … 1/32 incl. triplets),
  fade-in, phase, bipolar/unipolar, retrigger or free-running, quick-assign destination
- **8-slot modulation matrix** with per-slot activity indicators: velocity,
  mod wheel, aftertouch, key tracking, 3 envelopes, 2 LFOs, per-note random,
  pitch bend → pitch, fine, level, pan, PW, cutoff, resonance, amp, LFO rates,
  FX mix, stereo width, plus *(new in 1.2, appended)* wavetable position, warp
  amount, unison detune, stereo spread (per oscillator) and filter drive
- **Global quality modes** *(new in 1.2)*: LEGACY / ECO / HIGH / ULTRA —
  from exact 1.1 processing to 4× oversampled distortion, oversampled nonlinear
  filters, an FDN reverb and smoothed EQ (see `docs/QUALITY_MODES.md`)
- **Arpeggiator**: up/down/up-down/random, tempo-synced rates, gate, 1–3 octaves, hold
- **Master FX**: distortion, chorus, tempo-synced delay, reverb, 3-band EQ —
  individually bypassable with click-free crossfades; HIGH/ULTRA quality adds
  oversampling, Catmull-Rom interpolation and the FDN reverb
- **Preset bank**: 71 factory presets (51 originals + 20 new wavetable/HQ
  sounds) in 13 categories with author & description metadata, three-column
  browser (sources & categories / searchable list / info panel), favorites that
  persist, keyboard navigation, save/save-as (portable JSON), init & randomize,
  state saved with the project
- **~165 host-automatable parameters** with stable IDs and sensible ranges —
  every 1.1 parameter ID and choice order is preserved (append-only contract)

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
./build/YDCoreTests_artefacts/Release/YDCoreTests --screenshots shots [WxH] [preset]  # per-tab UI captures
./build/YDCoreTests_artefacts/Release/YDCoreTests --baseline write|check <dir> [tol]  # engine regression renders
```

1000+ checks: audio at 44.1/48 kHz across buffer sizes 32–4096, note-on/off
click bounds, NaN/denormal freedom, all factory presets load & sound (incl.
brand metadata), state save/restore round-trip, rapid preset switching during
playback, poly/mono behaviour, 500+ random-note stress, automation sweeps,
arpeggiator output, parameter ID uniqueness, user preset save/reload — plus the
v1.2 contracts: append-only parameter/choice guards, legacy-state migration,
wavetable bank integrity, warp/position safety, measured aliasing improvements,
filter stability, envelope-curve shapes, quality switching, oversampled FX
paths, WAV import fuzzing, unison/mono gain staging and factory preset
validation. The `--baseline` harness renders every factory preset at
44.1/48/96 kHz for sample-exact comparisons between engine revisions.

## Installing (FL Studio, Windows)

### Recommended: the GLOBUS installer

Download **`GLOBUS-Installer-Windows-x64.exe`** from the `ci-latest` release and
run it. The dark-themed setup wizard (Welcome → License → Location → Components
→ Install → Finish) installs:

- **VST3 plugin** → `C:\Program Files\Common Files\VST3\GLOBUS.vst3`
- **Standalone app** → `C:\Program Files\Ninth Parallel Audio\GLOBUS\`
  (Start Menu shortcut, optional Desktop shortcut, optional launch after setup)

x64-only, Windows 10/11, admin rights requested once (Program Files access).
Re-running the installer upgrades an existing installation in place. **User
presets are never modified or removed** — not by installation, upgrades or the
uninstaller (available in Windows Settings ▸ Apps). Installer artwork is
generated from `tools/gen_installer_assets.py`; the wizard script lives in
`installer/GLOBUS-Installer.iss` and is compiled + silently install/uninstall
tested on every CI run.

### Manual install

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
- **Oscillator engines** *(1.2)*: the selector next to ON picks LEGACY /
  BASIC HQ / WAVETABLE per oscillator. In WAVETABLE mode the PW knob becomes
  **POS**, a **WARP** knob + mode appear, the display shows the real frames
  with a position bar, and the name button opens the **wavetable browser**
  (◀ ▶ steps banks; IMPORT WAV brings in your own tables —
  see `docs/WAVETABLES.md`).
- **Quality** *(1.2)*: GLOBAL page — LEGACY / ECO / HIGH / ULTRA
  (see `docs/QUALITY_MODES.md`). New sounds default to HIGH via the factory
  presets; old sounds stay LEGACY automatically.
- **PRESETS page**: sources & categories on the left, searchable list in the
  middle (↑/↓ + Enter to load, double-click, star = favorite, Esc returns to
  OSC), info panel with author/description plus Load / Delete / Init / Random /
  Save As on the right.
- **Knobs**: drag = adjust, **Shift-drag = fine**, **double-click = reset**,
  hover shows the exact value. Every control has a tooltip.
- The window resizes freely from 900×600 up to 2400×1520 (layout scales).

## Signing status

Published artifacts are currently **unsigned development builds** — Windows
SmartScreen shows an *Unknown publisher* warning, which is expected and not
bypassed. The CI pipeline is signing-ready: adding Azure Trusted Signing
credentials as encrypted repository secrets enables automatic signing,
verification and honest release labeling. Full guide: `docs/SIGNING.md`.
Verify all downloads against the published `SHA256SUMS.txt`.

## Known limitations (non-blocking)

- Free-running (non-retriggered) LFOs ignore per-voice *LFO Rate* modulation;
  global sources (wheel, aftertouch, pitch bend) still modulate their rate.
- FX-mix and stereo-width matrix destinations respond to global modulation
  sources (wheel/AT/bend/LFOs), not per-voice envelopes — the FX bus is shared.
- The **LEGACY** oscillator engine keeps the 1.1 naive triangle and the LEGACY
  quality mode keeps the non-oversampled distortion **by design** — that is the
  compatibility contract. Select BASIC HQ / WAVETABLE engines and ECO/HIGH/ULTRA
  quality for the improved paths.
- Wavetable **Sync** warp is a windowed sync (documented approximation): the
  wrap discontinuity is faded over the last 3 % of the master period.
- ULTRA quality oversamples the per-voice nonlinear filters at 2× (not 4×) —
  a documented CPU trade; the master distortion does run at 4×.
- MIDI CC hard-wiring: CC1 (mod wheel), CC64 (sustain), CC120/123 (all sound/
  notes off), pitch bend, channel & poly aftertouch. Other CCs are available
  through host automation rather than a MIDI-learn system.

## Project structure

```
Source/
  Parameters.*           parameter contract (IDs, layout, tooltips; append-only)
  PluginProcessor.*      MIDI → arp → engine → FX; state save/restore; metering;
                         wavetable selection/import; quality latency reporting
  PluginEditor.*         top bar + five tabbed pages, scale-to-fit
  Engine/                DspUtils, Oscillator (legacy), OscillatorHQ (BASIC HQ +
                         WAVETABLE voices), Wavetable (mip data + library),
                         WavetableFactory (deterministic banks), WavetableImport,
                         Filter (legacy), FilterHQ (Ladder/OTA/SEM + halfbands),
                         Envelope (with curves), LFO, ModMatrix, Voice,
                         SynthEngine, Arpeggiator
  Effects/               Distortion (2×/4× OS), ChorusFx, DelayFx, ReverbFx,
                         ReverbHQ (FDN), EqFx, EffectsChain (quality routing)
  Presets/               PresetManager (JSON factory + user presets, favorites,
                         randomizer with engine integration)
  UI/                    LookAndFeel, controls, waveform/wavetable displays,
                         wavetable browser, top bar, pages, preset bank
Presets/                 71 factory presets (JSON + single-file bundle)
Tests/                   headless verification suite (1032 checks) + screenshot
                         tool + --baseline regression harness
docs/                    SIGNING.md, WAVETABLES.md, QUALITY_MODES.md
tools/gen_presets.py     factory preset generator
.github/workflows/       Windows x64 CI
```

## License

AGPL-3.0 (matching the JUCE open-source license). © 2026 Ninth Parallel Audio.
"GLOBUS" and "Ninth Parallel Audio" are original fictional identities created
for this project; no third-party presets, samples, artwork or code beyond JUCE
itself are included.
