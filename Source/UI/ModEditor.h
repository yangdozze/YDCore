// GLOBUS — tabbed modulation editor (OSC page): AMP ENV / FILTER ENV / MOD ENV
// / LFO 1 / LFO 2 share one compact panel. Switching tabs is a pure view
// change — no parameter is touched.
#pragma once
#include "Controls.h"
#include "SectionsMod.h"
#include "TopBar.h"          // TabButton
#include "../PluginProcessor.h"

namespace ydc
{
/** Live LFO waveform display driven by the real wave/bipolar parameters. */
class LfoShapeDisplay : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    LfoShapeDisplay (juce::AudioProcessorValueTreeState& state, int lfoNum, juce::Colour accentColour)
        : apvts (state), accent (accentColour)
    {
        ids = { ydc::ids::lfo (lfoNum, "Wave"), ydc::ids::lfo (lfoNum, "Bipolar"),
                ydc::ids::lfo (lfoNum, "Phase") };
        for (auto& id : ids)
            apvts.addParameterListener (id, this);
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~LfoShapeDisplay() override
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

        const auto area = b.reduced (7.0f, 8.0f);
        const auto wave = (LfoWave) (int) get (0);
        const bool bip  = get (1) > 0.5f;
        const float ph  = get (2);

        // zero line (centre for bipolar, bottom for unipolar)
        const float zeroY = bip ? area.getCentreY() : area.getBottom();
        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) zeroY, area.getX(), area.getRight());

        juce::Path p;
        const int n = juce::jmax (48, (int) area.getWidth());
        uint32_t seed = 0xBEEF1234u;
        float sh = 0.0f;
        int lastStep = -1;
        for (int i = 0; i <= n; ++i)
        {
            double t = 2.0 * i / n + ph;      // two cycles
            t -= std::floor (t / 1.0) * 0.0;  // keep running for step detection
            const double frac = t - std::floor (t);
            float v = 0.0f;
            switch (wave)
            {
                case LfoWave::Sine:     v = std::sin ((float) frac * kTwoPi); break;
                case LfoWave::Triangle: v = 1.0f - 2.0f * std::abs (2.0f * (float) frac - 1.0f); break;
                case LfoWave::Saw:      v = 2.0f * (float) frac - 1.0f; break;
                case LfoWave::Square:   v = frac < 0.5 ? 1.0f : -1.0f; break;
                case LfoWave::SampleHold:
                {
                    const int step = (int) std::floor (t * 4.0);   // 4 holds per cycle
                    if (step != lastStep)
                    {
                        lastStep = step;
                        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                        sh = (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
                    }
                    v = sh;
                    break;
                }
                default: break;
            }
            if (! bip)
                v = v * 0.5f + 0.5f;
            const float x = area.getX() + area.getWidth() * (float) i / n;
            const float y = bip ? area.getCentreY() - v * area.getHeight() * 0.45f
                                : area.getBottom() - v * area.getHeight() * 0.92f;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    }

private:
    float get (size_t i) const
    {
        if (auto* p = apvts.getRawParameterValue (ids[i]))
            return p->load();
        return 0.0f;
    }
    void parameterChanged (const juce::String&, float) override { dirty.store (true, std::memory_order_relaxed); }
    void timerCallback() override { if (dirty.exchange (false, std::memory_order_relaxed)) repaint(); }

    juce::AudioProcessorValueTreeState& apvts;
    std::array<juce::String, 3> ids;
    juce::Colour accent;
    std::atomic<bool> dirty { true };
};

//==============================================================================
class ModEditor : public SectionPanel
{
public:
    explicit ModEditor (YDCoreAudioProcessor& p)
        : SectionPanel ("MODULATION", theme::accent2),
          ampEnv (p.getApvts(), "AMP ENV", ids::ampA, ids::ampD, ids::ampS, ids::ampR, theme::accent),
          filterEnv (p.getApvts(), "FILTER ENV", ids::filA, ids::filD, ids::filS, ids::filR, theme::accent2),
          modEnv (p.getApvts()),
          lfo1 (p.getApvts(), 1),
          lfo2 (p.getApvts(), 2),
          lfo1Shape (p.getApvts(), 1, theme::accent),
          lfo2Shape (p.getApvts(), 2, theme::accent2),
          envLock ([&p] { return p.getPresetManager().isSectionLocked (PresetManager::LockEnv); },
                   [&p] (bool v) { p.getPresetManager().setSectionLocked (PresetManager::LockEnv, v); },
                   "envelopes"),
          lfoLock ([&p] { return p.getPresetManager().isSectionLocked (PresetManager::LockLfoMatrix); },
                   [&p] (bool v) { p.getPresetManager().setSectionLocked (PresetManager::LockLfoMatrix, v); },
                   "LFO and matrix")
    {
        const char* names[kTabs] = { "AMP ENV", "FILTER ENV", "MOD ENV", "LFO 1", "LFO 2" };
        for (int i = 0; i < kTabs; ++i)
        {
            tabs[i] = std::make_unique<TabButton> (names[i]);
            tabs[i]->setTooltip (juce::String ("Edit ") + names[i] + " (view only - values are untouched)");
            tabs[i]->onClick = [this, i] { setTab (i); };
            addAndMakeVisible (*tabs[i]);
        }

        for (auto* s : { (SectionPanel*) &ampEnv, (SectionPanel*) &filterEnv, (SectionPanel*) &modEnv,
                         (SectionPanel*) &lfo1, (SectionPanel*) &lfo2 })
        {
            s->setFrameless (true);
            addChildComponent (*s);
        }
        addChildComponent (lfo1Shape);
        addChildComponent (lfo2Shape);
        addAndMakeVisible (envLock);
        addAndMakeVisible (lfoLock);
        setTab (0);
    }

    void setTab (int idx)
    {
        tab = juce::jlimit (0, kTabs - 1, idx);
        for (int i = 0; i < kTabs; ++i)
            tabs[i]->setToggleState (i == tab, juce::dontSendNotification);
        ampEnv.setVisible (tab == 0);
        filterEnv.setVisible (tab == 1);
        modEnv.setVisible (tab == 2);
        lfo1.setVisible (tab == 3);
        lfo2.setVisible (tab == 4);
        lfo1Shape.setVisible (tab == 3);
        lfo2Shape.setVisible (tab == 4);
    }

    void resized() override
    {
        auto c = content();
        auto strip = c.removeFromTop (26);
        lfoLock.setBounds (strip.removeFromRight (22).reduced (1));
        envLock.setBounds (strip.removeFromRight (22).reduced (1));
        for (int i = 0; i < kTabs; ++i)
        {
            tabs[i]->setBounds (strip.removeFromLeft (108));
            strip.removeFromLeft (3);
        }
        c.removeFromTop (4);

        ampEnv.setBounds (c);
        filterEnv.setBounds (c);
        modEnv.setBounds (c);

        auto lfoArea = c;
        auto shape = lfoArea.removeFromLeft (218);
        lfoArea.removeFromLeft (6);
        lfo1Shape.setBounds (shape.reduced (0, 6));
        lfo2Shape.setBounds (shape.reduced (0, 6));
        lfo1.setBounds (lfoArea);
        lfo2.setBounds (lfoArea);
    }

private:
    static constexpr int kTabs = 5;
    std::unique_ptr<TabButton> tabs[kTabs];
    int tab = 0;

    EnvSection ampEnv, filterEnv;
    ModEnvSection modEnv;
    LfoSection lfo1, lfo2;
    LfoShapeDisplay lfo1Shape, lfo2Shape;
    LockButton envLock, lfoLock;
};

} // namespace ydc
