// GLOBUS — live ADSR visualisation. Repaints (30 Hz, dirty-flagged) whenever
// its four parameters move — from the UI, host automation or preset loads.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeelYD.h"

namespace ydc
{
class EnvelopeDisplay : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    EnvelopeDisplay (juce::AudioProcessorValueTreeState& state,
                     juce::String attackID, juce::String decayID,
                     juce::String sustainID, juce::String releaseID,
                     juce::Colour accentColour = theme::accent)
        : apvts (state), ids { attackID, decayID, sustainID, releaseID }, accent (accentColour)
    {
        for (auto& id : ids)
            apvts.addParameterListener (id, this);
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~EnvelopeDisplay() override
    {
        for (auto& id : ids)
            apvts.removeParameterListener (id, this);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg.brighter (0.02f));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const float a = get (0), d = get (1), s = get (2), r = get (3);

        // segment widths: log-ish scaling so short times stay visible
        auto seg = [] (float t) { return 0.08f + 0.92f * std::pow (juce::jlimit (0.0f, 1.0f, t / 10.0f), 0.35f); };
        const float wa = seg (a), wd = seg (d), wr = seg (r);
        const float sustainW = 0.55f;
        const float total = wa + wd + sustainW + wr;

        const auto area = b.reduced (5.0f);
        const float x0 = area.getX(), y1 = area.getBottom(), h = area.getHeight(), w = area.getWidth();
        const float xa = x0 + w * (wa / total);
        const float xd = xa + w * (wd / total);
        const float xs = xd + w * (sustainW / total);
        const float x1 = area.getRight();
        const float ys = y1 - h * s;

        juce::Path p;
        p.startNewSubPath (x0, y1);
        p.quadraticTo (x0 + (xa - x0) * 0.4f, y1 - h * 1.0f, xa, y1 - h);          // attack
        p.quadraticTo (xa + (xd - xa) * 0.3f, ys + (y1 - h - ys) * 0.2f, xd, ys);  // decay
        p.lineTo (xs, ys);                                                          // sustain
        p.quadraticTo (xs + (x1 - xs) * 0.3f, y1 + (ys - y1) * 0.25f, x1, y1);      // release

        auto fill = p;
        fill.lineTo (x0, y1);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.25f), 0, area.getY(),
                                                 accent.withAlpha (0.02f), 0, y1, false));
        g.fillPath (fill);

        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

        // stage markers
        g.setColour (theme::textDim.withAlpha (0.5f));
        for (float x : { xa, xd, xs })
            g.drawVerticalLine ((int) x, area.getY(), y1);
    }

private:
    float get (int i) const
    {
        if (auto* p = apvts.getRawParameterValue (ids[(size_t) i]))
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
    std::array<juce::String, 4> ids;
    juce::Colour accent;
    std::atomic<bool> dirty { true };
};

} // namespace ydc
