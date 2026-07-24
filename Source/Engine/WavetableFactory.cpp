// GLOBUS — deterministic factory wavetable generation.
//
// Every bank is generated from a parametric recipe (seeded where randomness is
// musical), analysed to a harmonic spectrum, then re-synthesised into
// band-limited mip levels. The repository stays text-only: tables are built at
// first use on the message thread, never at build time and never on the audio
// thread. Rebuilding always produces identical data (fixed seeds, fixed math).
//
// Normalisation policy (documented): per frame, gain = min(0.25 / rms, 0.9 / peak)
// computed on the level-0 render; the same gain is applied to every mip level of
// that frame so level crossfades stay seamless. DC is removed in the spectral
// domain (bin 0 is always dropped).
#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>
#include <functional>

namespace ydc
{
namespace
{
    constexpr double kPi2 = 6.283185307179586476925286766559;

    /** Sparse harmonic spectrum: index 1..kWtBaseHarmonics, amplitude + phase. */
    struct Spectrum
    {
        std::array<float, (size_t) kWtBaseHarmonics + 1> amp {};   // [0] unused (DC dropped)
        std::array<float, (size_t) kWtBaseHarmonics + 1> phase {};

        void set (int k, float a, float ph = 0.0f) noexcept
        {
            if (k >= 1 && k <= kWtBaseHarmonics) { amp[(size_t) k] = a; phase[(size_t) k] = ph; }
        }
        void add (int k, float a, float ph = 0.0f) noexcept
        {
            if (k >= 1 && k <= kWtBaseHarmonics)
            {
                // coherent complex addition
                const std::complex<float> cur = std::polar (amp[(size_t) k], phase[(size_t) k]);
                const std::complex<float> nxt = cur + std::polar (a, ph);
                amp[(size_t) k]   = std::abs (nxt);
                phase[(size_t) k] = std::arg (nxt);
            }
        }
    };

    /** Calibrated inverse-FFT synthesiser for one table size. The calibration
        run measures JUCE's inverse scaling once, so synthesis is exact
        regardless of library conventions. */
    struct LevelSynth
    {
        explicit LevelSynth (int tableSize)
            : size (tableSize), order (orderFor (tableSize)), fft (order),
              work ((size_t) tableSize * 2, 0.0f)
        {
            // calibrate: unit-magnitude bin 1 (cos phase) → measure time amplitude
            std::fill (work.begin(), work.end(), 0.0f);
            work[2] = 1.0f;  // bin 1 real
            fft.performRealOnlyInverseTransform (work.data());
            float peak = 0.0f;
            for (int i = 0; i < size; ++i)
                peak = std::max (peak, std::abs (work[(size_t) i]));
            scale = peak > 1.0e-12f ? 1.0f / peak : 1.0f;
        }

        static int orderFor (int n)
        {
            int o = 0;
            while ((1 << o) < n) ++o;
            return o;
        }

        /** Synthesise harmonics 1..maxHarm of `spec` into out[size]. */
        void render (const Spectrum& spec, int maxHarm, float* out)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            const int lim = std::min ({ maxHarm, kWtBaseHarmonics, size / 2 - 1 });
            for (int k = 1; k <= lim; ++k)
            {
                const float a = spec.amp[(size_t) k];
                if (a <= 1.0e-9f)
                    continue;
                const float ph = spec.phase[(size_t) k];
                work[(size_t) (2 * k)]     = a * scale * std::cos (ph);
                work[(size_t) (2 * k + 1)] = a * scale * std::sin (ph);
            }
            fft.performRealOnlyInverseTransform (work.data());
            std::copy (work.begin(), work.begin() + size, out);
        }

        int size, order;
        juce::dsp::FFT fft;
        float scale = 1.0f;
        std::vector<float> work;
    };

    /** Analyse a kWtFrameSize time-domain frame into a Spectrum (DC dropped). */
    Spectrum analyseFrame (const std::vector<float>& frame)
    {
        jassert ((int) frame.size() == kWtFrameSize);
        static thread_local std::unique_ptr<juce::dsp::FFT> fwd;
        static thread_local float fwdScale = 0.0f;
        if (fwd == nullptr)
        {
            fwd = std::make_unique<juce::dsp::FFT> (LevelSynth::orderFor (kWtFrameSize));
            // calibrate forward magnitude: unit cosine at bin 1
            std::vector<float> cal ((size_t) kWtFrameSize * 2, 0.0f);
            for (int i = 0; i < kWtFrameSize; ++i)
                cal[(size_t) i] = std::cos ((float) (kPi2 * i / kWtFrameSize));
            fwd->performRealOnlyForwardTransform (cal.data());
            fwdScale = std::sqrt (cal[2] * cal[2] + cal[3] * cal[3]);
            if (fwdScale < 1.0e-12f) fwdScale = 1.0f;
        }
        std::vector<float> buf ((size_t) kWtFrameSize * 2, 0.0f);
        std::copy (frame.begin(), frame.end(), buf.begin());
        fwd->performRealOnlyForwardTransform (buf.data());

        Spectrum s;
        for (int k = 1; k <= kWtBaseHarmonics; ++k)
        {
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            const float mag = std::sqrt (re * re + im * im) / fwdScale;
            if (mag > 1.0e-9f)
            {
                s.amp[(size_t) k]   = mag;
                s.phase[(size_t) k] = std::atan2 (im, re);
            }
        }
        return s;
    }

    // ---------- recipe helpers (all deterministic) ----------

    std::vector<float> timeFrame (const std::function<float (double)>& f)
    {
        std::vector<float> out ((size_t) kWtFrameSize);
        for (int i = 0; i < kWtFrameSize; ++i)
            out[(size_t) i] = f ((double) i / kWtFrameSize);
        return out;
    }

    Spectrum sawSpectrum (float brightness = 1.0f)  // brightness: rolloff exponent scale
    {
        Spectrum s;
        for (int k = 1; k <= kWtBaseHarmonics; ++k)
            s.set (k, 1.0f / std::pow ((float) k, 1.0f / std::max (0.25f, brightness)), 0.0f);
        return s;
    }

    /** Gaussian formant bump over a 1/k skeleton. f0Hz assumes a 110 Hz fundamental grid. */
    void addFormant (Spectrum& s, float centreHz, float widthHz, float gain, float f0Hz = 110.0f)
    {
        for (int k = 1; k <= kWtBaseHarmonics; ++k)
        {
            const float f = (float) k * f0Hz;
            const float g = gain * std::exp (-0.5f * ((f - centreHz) / widthHz) * ((f - centreHz) / widthHz));
            if (g > 1.0e-4f)
                s.add (k, g / std::sqrt ((float) k), 0.0f);
        }
    }

    float lerp (float a, float b, float t) { return a + (b - a) * t; }

} // namespace

//==============================================================================
std::unique_ptr<WavetableBank> buildWavetableBank (const juce::String& name,
                                                   const juce::String& category,
                                                   const juce::String& description,
                                                   const std::vector<std::vector<float>>& frames,
                                                   bool isFactory,
                                                   WtNormalise normalise)
{
    auto bank = std::make_unique<WavetableBank>();
    bank->name = name;
    bank->category = category;
    bank->description = description;
    bank->isFactory = isFactory;
    bank->numFrames = juce::jlimit (1, kWtMaxFrames, (int) frames.size());

    // analyse every frame once
    std::vector<Spectrum> specs;
    specs.reserve ((size_t) bank->numFrames);
    for (int f = 0; f < bank->numFrames; ++f)
        specs.push_back (analyseFrame (frames[(size_t) f]));

    // synths per level (sizes 2048,1024,512,256,128,64,64,...)
    bank->levels.resize ((size_t) kWtNumLevels);
    std::vector<std::unique_ptr<LevelSynth>> synths;
    for (int L = 0; L < kWtNumLevels; ++L)
    {
        const int size = std::max (64, kWtFrameSize >> L);
        auto& lv = bank->levels[(size_t) L];
        lv.tableSize = size;
        lv.rowStride = size + kWtGuard;
        lv.data.assign ((size_t) bank->numFrames * (size_t) lv.rowStride, 0.0f);
        bool found = false;
        for (auto& sy : synths)
            if (sy->size == size) { found = true; break; }
        if (! found)
            synths.push_back (std::make_unique<LevelSynth> (size));
    }
    auto synthFor = [&synths] (int size) -> LevelSynth&
    {
        for (auto& sy : synths)
            if (sy->size == size)
                return *sy;
        jassertfalse;
        return *synths.front();
    };

    std::vector<float> tmp ((size_t) kWtFrameSize);

    // TablePeak policy: one gain for the whole table (preserves frame dynamics)
    float tableGain = 1.0f;
    if (normalise == WtNormalise::TablePeak)
    {
        float tablePeak = 0.0f;
        auto& l0 = bank->levels[0];
        for (int f = 0; f < bank->numFrames; ++f)
        {
            synthFor (l0.tableSize).render (specs[(size_t) f], kWtBaseHarmonics, tmp.data());
            for (int i = 0; i < l0.tableSize; ++i)
                tablePeak = std::max (tablePeak, std::abs (tmp[(size_t) i]));
        }
        tableGain = tablePeak > 1.0e-9f ? 0.90f / tablePeak : 1.0f;
    }

    for (int f = 0; f < bank->numFrames; ++f)
    {
        // level 0 render decides the frame gain
        auto& l0 = bank->levels[0];
        synthFor (l0.tableSize).render (specs[(size_t) f], kWtBaseHarmonics, tmp.data());

        float gain = 1.0f;
        if (normalise == WtNormalise::PerFrame)
        {
            double sumSq = 0.0;
            float peak = 0.0f;
            for (int i = 0; i < l0.tableSize; ++i)
            {
                sumSq += (double) tmp[(size_t) i] * tmp[(size_t) i];
                peak = std::max (peak, std::abs (tmp[(size_t) i]));
            }
            const float rms = (float) std::sqrt (sumSq / std::max (1, l0.tableSize));
            gain = std::min (rms > 1.0e-9f ? 0.25f / rms : 1.0f,
                             peak > 1.0e-9f ? 0.90f / peak : 1.0f);
        }
        else if (normalise == WtNormalise::TablePeak)
        {
            gain = tableGain;
        }

        for (int L = 0; L < kWtNumLevels; ++L)
        {
            auto& lv = bank->levels[(size_t) L];
            const int cap = std::max (1, kWtBaseHarmonics >> L);
            if (L > 0)
                synthFor (lv.tableSize).render (specs[(size_t) f], cap, tmp.data());

            float* row = lv.data.data() + (size_t) f * (size_t) lv.rowStride;
            // guard-wrapped layout: row[0] = d[size-1], row[1..size] = d, row[size+1..] = d[0..]
            for (int i = 0; i < lv.tableSize; ++i)
                row[i + 1] = tmp[(size_t) i] * gain;
            row[0] = row[lv.tableSize];                 // d[size-1]
            row[lv.tableSize + 1] = row[1];             // d[0]
            row[lv.tableSize + 2] = row[2];             // d[1]
            row[lv.tableSize + 3] = row[3];             // d[2] (spare)
        }
    }
    return bank;
}

//==============================================================================
const WavetableLibrary& WavetableLibrary::get()
{
    static WavetableLibrary lib;   // built once per process, off the audio thread
    return lib;
}

WavetableLibrary::WavetableLibrary()
{
    using Frames = std::vector<std::vector<float>>;

    // internal BASIC HQ shape bank: mathematical saw + triangle, no normalisation
    // (amplitudes must match the ideal waveforms for legacy-consistent gain staging)
    {
        Frames shapes;
        shapes.push_back (timeFrame ([] (double t) { return 2.0 * t - 1.0; }));
        shapes.push_back (timeFrame ([] (double t) { return 1.0 - 4.0 * std::abs (t - 0.5); }));
        hqBank = buildWavetableBank ("__hq_shapes", "Internal", "internal", shapes, true, WtNormalise::None);
    }

    auto fromSpectra = [] (const std::vector<Spectrum>& specs)
    {
        // spectra are converted through a level-0 additive render so every bank
        // flows through the same analyse→synthesise pipeline
        Frames frames;
        LevelSynth synth (kWtFrameSize);
        for (const auto& s : specs)
        {
            std::vector<float> t ((size_t) kWtFrameSize);
            synth.render (s, kWtBaseHarmonics, t.data());
            frames.push_back (std::move (t));
        }
        return frames;
    };
    auto addBank = [this] (const char* name, const char* cat, const char* desc, Frames frames)
    {
        list.push_back (buildWavetableBank (name, cat, desc, frames, true));
    };

    // ============================ ANALOG ============================
    {
        // 0 — Prime Shapes: the init bank. sine → triangle → saw → square → pulses.
        Frames f;
        f.push_back (timeFrame ([] (double t) { return std::sin (kPi2 * t); }));
        f.push_back (timeFrame ([] (double t) { return 1.0 - 4.0 * std::abs (t - 0.5); }));
        f.push_back (timeFrame ([] (double t) { return 2.0 * t - 1.0; }));
        f.push_back (timeFrame ([] (double t) { return t < 0.5 ? 1.0 : -1.0; }));
        f.push_back (timeFrame ([] (double t) { return t < 0.25 ? 1.0 : -1.0; }));
        f.push_back (timeFrame ([] (double t) { return t < 0.125 ? 1.0 : -1.0; }));
        f.push_back (timeFrame ([] (double t) { return 0.6 * std::sin (kPi2 * t) + 0.4 * std::sin (2.0 * kPi2 * t); }));
        f.push_back (timeFrame ([] (double t) { return std::sin (kPi2 * t + 0.6 * std::sin (2.0 * kPi2 * t)); }));
        addBank ("Prime Shapes", "Analog", "Classic single-cycle shapes from sine to narrow pulse — the go-to starting bank.", f);
    }
    {
        // 1 — Analog Warmth: saw through a slowly opening 1-pole lowpass + 2nd-harmonic glow
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float open = (float) fr / 15.0f;                    // 0..1
            const float fc = 2.0f + open * open * 200.0f;             // knee harmonic
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; ++k)
            {
                const float lp = 1.0f / std::sqrt (1.0f + std::pow ((float) k / fc, 4.0f));
                s.set (k, lp / (float) k, 0.0f);
            }
            s.add (2, 0.10f * (1.0f - open), 0.0f);
            specs.push_back (s);
        }
        addBank ("Analog Warmth", "Analog", "A saw behind a slowly opening vintage lowpass with a touch of octave glow.", fromSpectra (specs));
    }
    {
        // 2 — Vintage Stack: two detuned saws captured at rotating beat phases
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float beat = (float) fr / 16.0f;    // phase offset between the two saws
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; ++k)
            {
                const float a = 0.5f / (float) k;
                s.add (k, a, 0.0f);
                s.add (k, a, (float) (kPi2 * beat * k));
            }
            specs.push_back (s);
        }
        addBank ("Vintage Stack", "Analog", "Twin detuned saws frozen at rotating beat phases — position scans the beating cycle.", fromSpectra (specs));
    }

    // ============================ DIGITAL ============================
    {
        // 3 — Bitframe: sine crushed through decreasing step resolution
        Frames f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float levels = lerp (48.0f, 2.0f, std::pow ((float) fr / 15.0f, 1.5f));
            f.push_back (timeFrame ([levels] (double t)
            {
                const double v = std::sin (kPi2 * t);
                return std::round (v * levels * 0.5) / (levels * 0.5);
            }));
        }
        addBank ("Bitframe", "Digital", "A sine collapsing through coarser and coarser bit steps.", f);
    }
    {
        // 4 — Pulse Matrix: PWM sweep with a digital odd-harmonic tilt
        Frames f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const double pw = lerp (0.5f, 0.06f, (float) fr / 15.0f);
            f.push_back (timeFrame ([pw] (double t) { return t < pw ? 1.0 : -1.0; }));
        }
        addBank ("Pulse Matrix", "Digital", "A pulse narrowing from square to sliver — instant PWM under position modulation.", f);
    }
    {
        // 5 — Digital Steps: stair-stepped ramps with increasing step counts
        Frames f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const int steps = 2 + fr;
            f.push_back (timeFrame ([steps] (double t)
            {
                const double q = std::floor (t * steps) / (steps - 1 > 0 ? steps - 1 : 1);
                return q * 2.0 - 1.0;
            }));
        }
        addBank ("Digital Steps", "Digital", "Hard staircase ramps — few steps to many, from gritty to smooth.", f);
    }

    // ============================ HARMONIC ============================
    {
        // 6 — Harmonic Rise: partial count grows with position
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const int top = 1 + fr * 4;
            Spectrum s;
            for (int k = 1; k <= top && k <= kWtBaseHarmonics; ++k)
                s.set (k, 1.0f / std::sqrt ((float) k), 0.0f);
            specs.push_back (s);
        }
        addBank ("Harmonic Rise", "Harmonic", "Partials stack up as the position rises — from pure tone to full spectrum.", fromSpectra (specs));
    }
    {
        // 7 — Odd Order: odd harmonics with a morphing rolloff exponent
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float expo = lerp (1.0f, 2.5f, (float) fr / 15.0f);
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; k += 2)
                s.set (k, 1.0f / std::pow ((float) k, expo), 0.0f);
            specs.push_back (s);
        }
        addBank ("Odd Order", "Harmonic", "Odd partials only, tilting from bright square-like to hollow and soft.", fromSpectra (specs));
    }
    {
        // 8 — Spectral Comb: harmonics on a widening comb grid
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const int spacing = 1 + fr / 2;
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; k += spacing)
                s.set (k, 1.0f / std::sqrt ((float) k), 0.0f);
            specs.push_back (s);
        }
        addBank ("Spectral Comb", "Harmonic", "A comb of partials spreading apart — resonant, ringing, organ-like at the top.", fromSpectra (specs));
    }

    // ============================ FORMANT ============================
    {
        // 9 — Vowel Morph: A → E → I → O → U formant sets
        struct Vowel { float f1, f2, f3; };
        const Vowel vowels[5] = { { 800, 1150, 2900 },   // A
                                  { 400, 2000, 2800 },   // E
                                  { 250, 2250, 3050 },   // I
                                  { 400,  800, 2830 },   // O
                                  { 350,  600, 2700 } }; // U
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 32; ++fr)
        {
            const float pos = (float) fr / 31.0f * 4.0f;
            const int vi = juce::jlimit (0, 3, (int) pos);
            const float t = pos - (float) vi;
            const Vowel a = vowels[vi], b = vowels[vi + 1];
            Spectrum s;
            addFormant (s, lerp (a.f1, b.f1, t), 90.0f,  1.0f);
            addFormant (s, lerp (a.f2, b.f2, t), 120.0f, 0.6f);
            addFormant (s, lerp (a.f3, b.f3, t), 160.0f, 0.25f);
            specs.push_back (s);
        }
        addBank ("Vowel Morph", "Formant", "Vocal formants gliding A–E–I–O–U across the position axis.", fromSpectra (specs));
    }
    {
        // 10 — Formant Sweep: one strong formant travelling up the spectrum
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 32; ++fr)
        {
            const float centre = 180.0f * std::pow (2.0f, (float) fr / 31.0f * 4.5f); // 180 Hz → ~4 kHz
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; ++k)
                s.set (k, 0.15f / std::sqrt ((float) k), 0.0f);          // thin skeleton
            addFormant (s, centre, centre * 0.16f, 1.0f);
            specs.push_back (s);
        }
        addBank ("Formant Sweep", "Formant", "A single vocal resonance sweeping four and a half octaves.", fromSpectra (specs));
    }
    {
        // 11 — Vocal Glass: two narrow formants in contrary motion
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 24; ++fr)
        {
            const float t = (float) fr / 23.0f;
            Spectrum s;
            addFormant (s, lerp (300.0f, 2400.0f, t), 70.0f, 1.0f);
            addFormant (s, lerp (2400.0f, 300.0f, t), 70.0f, 0.8f);
            specs.push_back (s);
        }
        addBank ("Vocal Glass", "Formant", "Two glassy resonances crossing over each other mid-table.", fromSpectra (specs));
    }

    // ============================ METALLIC ============================
    {
        // 12 — Bell Alloy: sparse stretched partials snapped to the harmonic grid
        std::vector<Spectrum> specs;
        Rng rng; rng.seed (0xBE11A110u);
        // one fixed partial recipe, stretched more per frame
        float basePartials[7];
        for (auto& p : basePartials)
            p = 1.0f + rng.uni() * 11.0f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float stretch = 1.0f + 0.9f * (float) fr / 15.0f;
            Spectrum s;
            s.set (1, 1.0f, 0.0f);
            for (int pi = 0; pi < 7; ++pi)
            {
                const int k = juce::jlimit (1, kWtBaseHarmonics,
                                            (int) std::round (basePartials[pi] * stretch * 1.71f));
                s.add (k, 0.7f / std::sqrt ((float) k), (float) pi * 0.7f);
            }
            specs.push_back (s);
        }
        addBank ("Bell Alloy", "Metallic", "Sparse bell partials stretching apart — position pulls the alloy brighter.", fromSpectra (specs));
    }
    {
        // 13 — FM Chrome: 2-op FM (ratio 3:1), index grows with position
        Frames f;
        for (int fr = 0; fr < 32; ++fr)
        {
            const double index = (double) fr / 31.0 * 6.0;
            f.push_back (timeFrame ([index] (double t)
            {
                return std::sin (kPi2 * t + index * std::sin (3.0 * kPi2 * t));
            }));
        }
        addBank ("FM Chrome", "Metallic", "Two-operator FM at ratio 3 — clean chrome at the front, snarling sidebands at the back.", f);
    }
    {
        // 14 — Ring Shift: sine ring-modulated by a rising partner
        Frames f;
        for (int fr = 0; fr < 24; ++fr)
        {
            const double ratio = 1.0 + (double) fr / 23.0 * 6.0;
            f.push_back (timeFrame ([ratio] (double t)
            {
                const double m = std::sin (kPi2 * t * std::floor (ratio))
                               * (1.0 - (ratio - std::floor (ratio)))
                               + std::sin (kPi2 * t * (std::floor (ratio) + 1.0))
                               * (ratio - std::floor (ratio));
                return std::sin (kPi2 * t) * (0.35 + 0.65 * m);
            }));
        }
        addBank ("Ring Shift", "Metallic", "Ring modulation against a partner sliding up seven partials.", f);
    }

    // ============================ MOTION ============================
    {
        // 15 — Orbit: constant saw-like spectrum, phases rotate per frame (loops seamlessly)
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 32; ++fr)
        {
            const float rot = (float) fr / 32.0f;
            Spectrum s;
            for (int k = 1; k <= 128; ++k)
                s.set (k, 1.0f / (float) k, (float) (kPi2 * rot * std::sqrt ((double) k) * 4.0));
            specs.push_back (s);
        }
        addBank ("Orbit", "Motion", "Same tone, swirling phases — position modulation turns it into pure motion.", fromSpectra (specs));
    }
    {
        // 16 — Slow Tide: lowpass sweeping down and back up across the table (loops)
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 32; ++fr)
        {
            const float ph = (float) fr / 32.0f;
            const float fc = 4.0f + 180.0f * (0.5f - 0.5f * std::cos ((float) kPi2 * ph));
            Spectrum s;
            for (int k = 1; k <= kWtBaseHarmonics; ++k)
                s.set (k, (1.0f / (float) k) / (1.0f + std::pow ((float) k / fc, 6.0f)), 0.0f);
            specs.push_back (s);
        }
        addBank ("Slow Tide", "Motion", "A filter tide rolling across the table — loop the position for endless swells.", fromSpectra (specs));
    }
    {
        // 17 — Wave Traveller: a wave packet travelling through the cycle
        Frames f;
        for (int fr = 0; fr < 32; ++fr)
        {
            const double centre = (double) fr / 32.0;
            f.push_back (timeFrame ([centre] (double t)
            {
                double d = t - centre;
                d -= std::round (d);                       // wrap distance
                const double env = std::exp (-d * d * 260.0);
                return env * std::sin (kPi2 * t * 9.0) + 0.18 * std::sin (kPi2 * t);
            }));
        }
        addBank ("Wave Traveller", "Motion", "A sine packet touring the cycle — scan it for talking, sweeping movement.", f);
    }

    // ============================ SOFT ============================
    {
        // 18 — Soft Glass: few partials, steep rolloff, gentle brightness tilt
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const float tilt = (float) fr / 15.0f;
            Spectrum s;
            for (int k = 1; k <= 10; ++k)
                s.set (k, 1.0f / std::pow ((float) k, 2.6f - tilt), k % 2 == 0 ? 0.35f : 0.0f);
            specs.push_back (s);
        }
        addBank ("Soft Glass", "Soft", "Rounded glassy partials that gain a little sparkle toward the end.", fromSpectra (specs));
    }
    {
        // 19 — Breath Pad: warm base + a whispering seeded top end
        std::vector<Spectrum> specs;
        Rng rng; rng.seed (0xB4EA7Bu);
        std::array<float, 200> sparkle {};
        for (auto& v : sparkle)
            v = rng.uni();
        for (int fr = 0; fr < 16; ++fr)
        {
            const float breath = (float) fr / 15.0f;
            Spectrum s;
            s.set (1, 1.0f, 0.0f);
            s.set (2, 0.35f, 0.0f);
            s.set (3, 0.15f, 0.0f);
            for (int k = 24; k < 224; ++k)
                s.set (k, sparkle[(size_t) (k - 24)] * breath * 0.035f / std::sqrt ((float) k / 24.0f),
                       sparkle[(size_t) (k - 24)] * 6.0f);
            specs.push_back (s);
        }
        addBank ("Breath Pad", "Soft", "A warm core with a breath of fixed, seeded air rising over it.", fromSpectra (specs));
    }
    {
        // 20 — Velvet: triangle family rounding off into a pure tone
        Frames f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const double round_ = (double) fr / 15.0;
            f.push_back (timeFrame ([round_] (double t)
            {
                const double tri = 1.0 - 4.0 * std::abs (t - 0.5);
                const double sin_ = std::sin (kPi2 * t - kPi2 * 0.25) ;
                return tri * (1.0 - round_) + sin_ * round_;
            }));
        }
        addBank ("Velvet", "Soft", "Triangle melting into sine — quiet, dark, and smooth all the way.", f);
    }

    // ============================ AGGRESSIVE ============================
    {
        // 21 — Saw Stack: widening stack of phase-spread saws
        std::vector<Spectrum> specs;
        for (int fr = 0; fr < 16; ++fr)
        {
            const int stack = 1 + fr / 2;                       // 1..8 saws
            Spectrum s;
            for (int v = 0; v < stack; ++v)
            {
                const float ph = (float) v / (float) stack;
                for (int k = 1; k <= 256; ++k)
                    s.add (k, (1.0f / (float) k) / std::sqrt ((float) stack), (float) (kPi2 * ph * k * 0.31));
            }
            specs.push_back (s);
        }
        addBank ("Saw Stack", "Aggressive", "One saw fattening into an eight-deep wall — instant hoover fuel.", fromSpectra (specs));
    }
    {
        // 22 — Growl Formant: low snarling formant with rising soft-clip drive
        Frames f;
        LevelSynth synth (kWtFrameSize);
        for (int fr = 0; fr < 24; ++fr)
        {
            const float drive = 1.0f + (float) fr / 23.0f * 6.0f;
            Spectrum s;
            for (int k = 1; k <= 96; ++k)
                s.set (k, 1.0f / (float) k, 0.0f);
            addFormant (s, 280.0f, 120.0f, 1.4f);
            addFormant (s, 900.0f, 220.0f, 0.7f);
            std::vector<float> t ((size_t) kWtFrameSize);
            synth.render (s, kWtBaseHarmonics, t.data());
            float peak = 0.0f;
            for (auto v : t) peak = std::max (peak, std::abs (v));
            const float norm = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
            for (auto& v : t)
                v = std::tanh (v * norm * drive);
            f.push_back (std::move (t));
        }
        addBank ("Growl Formant", "Aggressive", "A snarling low formant driven harder and harder into saturation.", f);
    }
    {
        // 23 — Torn Edge: saw clipped at a falling ceiling
        Frames f;
        for (int fr = 0; fr < 16; ++fr)
        {
            const double ceil_ = lerp (1.0f, 0.12f, (float) fr / 15.0f);
            f.push_back (timeFrame ([ceil_] (double t)
            {
                const double v = 2.0 * t - 1.0;
                return juce::jlimit (-ceil_, ceil_, v) / ceil_;
            }));
        }
        addBank ("Torn Edge", "Aggressive", "A saw slammed into a collapsing ceiling — squarer, meaner, louder-feeling.", f);
    }

    // ============================ EXPERIMENTAL ============================
    {
        // 24 — Random Walk: seeded spectra drifting smoothly frame to frame
        std::vector<Spectrum> specs;
        Rng rng; rng.seed (0x5EEDBA5Eu);
        std::array<float, 64> amp {}, target {};
        for (auto& v : target) v = rng.uni();
        for (int fr = 0; fr < 32; ++fr)
        {
            Spectrum s;
            for (int k = 0; k < 64; ++k)
            {
                amp[(size_t) k] += (target[(size_t) k] - amp[(size_t) k]) * 0.35f;
                if (rng.uni() < 0.12f)
                    target[(size_t) k] = rng.uni();
                s.set (k + 1, amp[(size_t) k] * amp[(size_t) k] / std::sqrt ((float) (k + 1)), 0.0f);
            }
            specs.push_back (s);
        }
        addBank ("Random Walk", "Experimental", "Sixty-four partials on a seeded drift — different everywhere, never twice.", fromSpectra (specs));
    }
    {
        // 25 — Fractal Fold: sine folded deeper and deeper
        Frames f;
        for (int fr = 0; fr < 24; ++fr)
        {
            const double depth = 1.0 + (double) fr / 23.0 * 5.0;
            f.push_back (timeFrame ([depth] (double t)
            {
                return std::sin (kPi2 * t * 1.0 + depth * std::sin (kPi2 * t * 2.0)
                                 + 0.5 * depth * std::sin (kPi2 * t * 5.0));
            }));
        }
        addBank ("Fractal Fold", "Experimental", "Nested sine folding — gentle vibrato of chaos at the end of the table.", f);
    }
    {
        // 26 — Glitch Line: deterministically permuted saw segments
        Frames f;
        Rng rng; rng.seed (0x611C7C11u);
        for (int fr = 0; fr < 16; ++fr)
        {
            const int segs = 4 + (fr / 2) * 2;
            std::array<int, 20> perm {};
            for (int i = 0; i < segs; ++i) perm[(size_t) i] = i;
            for (int i = segs - 1; i > 0; --i)                     // seeded Fisher–Yates
                std::swap (perm[(size_t) i], perm[(size_t) (rng.next() % (uint32_t) (i + 1))]);
            f.push_back (timeFrame ([segs, &perm] (double t)
            {
                const int seg = juce::jlimit (0, segs - 1, (int) (t * segs));
                const double local = t * segs - seg;
                const double src = (perm[(size_t) seg] + local) / segs;
                return 2.0 * src - 1.0;
            }));
        }
        addBank ("Glitch Line", "Experimental", "A saw cut into segments and reshuffled — orderly at first, scrambled at the end.", f);
    }

    jassert ((int) list.size() >= 24);
}

} // namespace ydc
