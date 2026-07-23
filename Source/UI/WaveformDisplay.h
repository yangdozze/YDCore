// GLOBUS — live oscillator waveform display. Renders the actual selected
// waveform from the real parameter values (wave, pulse width, unison, detune)
// — no canned animations.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeelYD.h"
#include "../Parameters.h"

namespace ydc
{
class WaveformDisplay : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    WaveformDisplay (juce::AudioProcessorValueTreeState& state, int oscNum, juce::Colour accentColour)
        : apvts (state), accent (accentColour)
    {
        ids = { ydc::ids::osc (oscNum, "Wave"), ydc::ids::osc (oscNum, "PW"),
                ydc::ids::osc (oscNum, "UniCount"), ydc::ids::osc (oscNum, "UniDetune"),
                ydc::ids::osc (oscNum, "On") };
        for (auto& id : ids)
            apvts.addParameterListener (id, this);
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~WaveformDisplay() override
    {
        for (auto& id : ids)
            apvts.removeParameterListener (id, this);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg.brighter (0.015f));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const auto area = b.reduced (7.0f, 9.0f);
        const bool on   = get (4) > 0.5f;
        const auto wave = (OscWave) (int) get (0);
        const float pw  = get (1);
        const int   uni = (int) get (2);
        const float det = get (3);

        // centre line
        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        const auto col = on ? accent : theme::textDim.withAlpha (0.35f);

        if (wave == OscWave::SuperSaw)
        {
            // seven detuned saws: phase offsets grow with the detune knob
            for (int k = 0; k < 7; ++k)
            {
                const float ofs = (k - 3) * det * 0.002f;
                g.setColour (col.withAlpha (k == 3 ? 0.95f : 0.35f));
                g.strokePath (buildPath (OscWave::Saw, pw, area, ofs),
                              juce::PathStrokeType (k == 3 ? 1.8f : 1.1f, juce::PathStrokeType::curved));
            }
        }
        else
        {
            // unison ghosts behind the main trace
            const int ghosts = juce::jlimit (1, 7, uni);
            for (int k = 1; k < ghosts; ++k)
            {
                const float ofs = (k - ghosts * 0.5f) * det * 0.0015f;
                g.setColour (col.withAlpha (0.22f));
                g.strokePath (buildPath (wave, pw, area, ofs), juce::PathStrokeType (1.1f));
            }
            g.setColour (col);
            g.strokePath (buildPath (wave, pw, area, 0.0f), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
        }
    }

private:
    juce::Path buildPath (OscWave wave, float pw, juce::Rectangle<float> area, float phaseOfs) const
    {
        juce::Path p;
        const int   n  = juce::jmax (32, (int) area.getWidth());
        const float x0 = area.getX(), w = area.getWidth();
        const float yc = area.getCentreY(), h2 = area.getHeight() * 0.5f;

        uint32_t noiseSeed = 0x1234ABCDu;   // deterministic static for the noise wave
        auto noise = [&noiseSeed]
        {
            noiseSeed ^= noiseSeed << 13; noiseSeed ^= noiseSeed >> 17; noiseSeed ^= noiseSeed << 5;
            return (float) (noiseSeed >> 8) * (2.0f / 16777216.0f) - 1.0f;
        };

        for (int i = 0; i <= n; ++i)
        {
            double t = (double) i / n + phaseOfs;
            t -= std::floor (t);
            float v = 0.0f;
            switch (wave)
            {
                case OscWave::Sine:     v = std::sin ((float) t * kTwoPi); break;
                case OscWave::Triangle: v = 1.0f - 2.0f * std::abs (2.0f * (float) t - 1.0f); break;
                case OscWave::Saw:      v = 2.0f * (float) t - 1.0f; break;
                case OscWave::Square:   v = t < 0.5 ? 1.0f : -1.0f; break;
                case OscWave::Pulse:    v = t < pw ? 1.0f : -1.0f; break;
                case OscWave::Noise:    v = noise() * 0.8f; break;
                case OscWave::SuperSaw:
                default:                v = 2.0f * (float) t - 1.0f; break;
            }
            const float x = x0 + w * (float) i / n;
            const float y = yc - v * h2 * 0.85f;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    }

    float get (size_t i) const
    {
        if (auto* p = apvts.getRawParameterValue (ids[i]))
            return p->load();
        return 0.0f;
    }

    void parameterChanged (const juce::String&, float) override { dirty.store (true, std::memory_order_relaxed); }
    void timerCallback() override
    {
        if (dirty.exchange (false, std::memory_order_relaxed))
            repaint();
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::array<juce::String, 5> ids;
    juce::Colour accent;
    std::atomic<bool> dirty { true };
};

} // namespace ydc
