// GLOBUS v1.2 — live wavetable display: draws the REAL frames of the selected
// bank (ghosted stack) with the current frame highlighted and a position bar.
// No canned animation — everything is read from the actual immutable bank data
// and the actual WT Position parameter (including its modulated base value).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeelYD.h"
#include "../PluginProcessor.h"

namespace ydc
{
class WavetableDisplay : public juce::Component,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    WavetableDisplay (YDCoreAudioProcessor& p, int oscNum, juce::Colour accentColour)
        : proc (p), oscIndex (oscNum - 1), accent (accentColour)
    {
        posId = ids::osc (oscNum, "WtPos");
        onId  = ids::osc (oscNum, "On");
        proc.getApvts().addParameterListener (posId, this);
        proc.getApvts().addParameterListener (onId, this);
        proc.wtEvents.addChangeListener (this);
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~WavetableDisplay() override
    {
        proc.getApvts().removeParameterListener (posId, this);
        proc.getApvts().removeParameterListener (onId, this);
        proc.wtEvents.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (theme::bg.brighter (0.015f));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const auto* bank = currentBank();
        if (bank == nullptr || bank->numFrames <= 0)
            return;

        const bool on = proc.getApvts().getRawParameterValue (onId)->load() > 0.5f;
        const float pos = clampf (proc.getApvts().getRawParameterValue (posId)->load(), 0.0f, 1.0f);
        const auto col = on ? accent : theme::textDim.withAlpha (0.35f);

        auto area = b.reduced (7.0f, 9.0f);
        area.removeFromBottom (6.0f);                        // room for the position bar

        // ghost frames (up to 5 evenly spaced), then the current frame on top
        const int ghosts = juce::jmin (5, bank->numFrames);
        for (int k = 0; k < ghosts; ++k)
        {
            const int frame = ghosts > 1 ? k * (bank->numFrames - 1) / (ghosts - 1) : 0;
            g.setColour (col.withAlpha (0.14f));
            g.strokePath (framePath (*bank, frame, area, 0.6f), juce::PathStrokeType (1.0f));
        }

        const float fpos = pos * (float) (bank->numFrames - 1);
        const int   curFrame = juce::jlimit (0, bank->numFrames - 1, juce::roundToInt (fpos));
        g.setColour (col);
        g.strokePath (framePath (*bank, curFrame, area, 1.0f),
                      juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

        // position bar along the bottom
        auto bar = juce::Rectangle<float> (area.getX(), b.getBottom() - 8.0f, area.getWidth(), 3.0f);
        g.setColour (theme::track);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (col);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * pos)), 1.5f);
        g.fillEllipse (bar.getX() + bar.getWidth() * pos - 2.5f, bar.getY() - 1.5f, 6.0f, 6.0f);

        // missing-asset badge
        if (proc.isOscWavetableMissing (oscIndex))
        {
            g.setColour (theme::warn);
            g.setFont (LookAndFeelYD::uiFont (9.5f, true));
            g.drawText ("MISSING FILE - USING DEFAULT", (int) area.getX(), (int) area.getY(),
                        (int) area.getWidth(), 12, juce::Justification::centredRight);
        }
    }

private:
    const WavetableBank* currentBank() const
    {
        // resolve the (factory or user) bank via the same path the engine uses
        const auto name = proc.getOscWavetableName (oscIndex);
        if (const auto* b = WavetableLibrary::get().byName (name))
            return b;
        // user banks and fallback: rely on the library default for painting when unknown
        return WavetableLibrary::get().defaultBank();
    }

    juce::Path framePath (const WavetableBank& bank, int frame, juce::Rectangle<float> area, float scale) const
    {
        juce::Path p;
        const int n = juce::jmax (48, (int) area.getWidth());
        const float yc = area.getCentreY(), h2 = area.getHeight() * 0.5f * 0.85f * scale;
        for (int i = 0; i <= n; ++i)
        {
            const float ph = (float) i / (float) (n + 1);
            const float v = bank.readFrameLinear (0, frame, ph);
            const float x = area.getX() + area.getWidth() * (float) i / (float) n;
            const float y = yc - clampf (v, -1.2f, 1.2f) * h2;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    }

    void parameterChanged (const juce::String&, float) override { dirty.store (true, std::memory_order_relaxed); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { dirty.store (true, std::memory_order_relaxed); }
    void timerCallback() override
    {
        if (dirty.exchange (false, std::memory_order_relaxed))
            repaint();
    }

    YDCoreAudioProcessor& proc;
    int oscIndex;
    juce::Colour accent;
    juce::String posId, onId;
    std::atomic<bool> dirty { true };
};

} // namespace ydc
