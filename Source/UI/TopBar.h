// GLOBUS — persistent top bar: brand, tab navigation, preset display &
// transport-style actions, MIDI LED, CPU, voices and a real output meter.
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ydc
{
/** Custom-drawn main tab button. */
class TabButton : public juce::Button
{
public:
    explicit TabButton (const juce::String& tabText) : juce::Button (tabText)
    {
        setClickingTogglesState (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const bool active = getToggleState();

        if (active)
        {
            g.setGradientFill (juce::ColourGradient (theme::panelLight.brighter (0.08f), 0, 0,
                                                     theme::panel, 0, b.getBottom(), false));
            g.fillRoundedRectangle (b.reduced (1.0f), 5.0f);
            g.setColour (theme::accent);
            g.fillRoundedRectangle (b.getX() + 8.0f, b.getY() + 2.0f, b.getWidth() - 16.0f, 3.0f, 1.5f);
        }
        else if (highlighted || down)
        {
            g.setColour (theme::panelLight.withAlpha (down ? 0.9f : 0.55f));
            g.fillRoundedRectangle (b.reduced (1.0f), 5.0f);
        }

        g.setColour (active ? theme::text : theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (13.0f, active));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
/** Compact stereo output meter fed by the processor's lock-free peaks. */
class OutputMeter : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit OutputMeter (YDCoreAudioProcessor& p) : processor (p)
    {
        setTooltip ("Master output level");
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::bg);
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 3.0f, 1.0f);

        auto area = b.reduced (2.5f);
        const float bh = (area.getHeight() - 2.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const auto bar = juce::Rectangle<float> (area.getX(), area.getY() + c * (bh + 2.0f),
                                                     area.getWidth(), bh);
            // dB-ish mapping: -60..0 dB over the bar width
            const float db = juce::Decibels::gainToDecibels (level[c], -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            if (frac > 0.001f)
            {
                const auto fill = bar.withWidth (bar.getWidth() * frac);
                const auto col = level[c] > 0.99f ? theme::warn
                                                  : (c == 0 ? theme::accent : theme::accent2);
                g.setColour (col.withAlpha (0.9f));
                g.fillRoundedRectangle (fill, 2.0f);
            }
        }
    }

private:
    void timerCallback() override
    {
        bool changed = false;
        for (int c = 0; c < 2; ++c)
        {
            const float peak = processor.consumeOutputPeak (c);
            const float next = juce::jmax (peak, level[c] * 0.82f);   // fall-back decay
            if (std::abs (next - level[c]) > 0.001f)
            {
                level[c] = next;
                changed = true;
            }
            else
                level[c] = next;
        }
        if (changed)
            repaint();
    }

    YDCoreAudioProcessor& processor;
    float level[2] { 0.0f, 0.0f };
};

//==============================================================================
class TopBar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kNumTabs = 5;

    explicit TopBar (YDCoreAudioProcessor& p)
        : processor (p), meter (p)
    {
        const char* names[kNumTabs] = { "OSC", "FX", "MATRIX", "GLOBAL", "PRESETS" };
        for (int i = 0; i < kNumTabs; ++i)
        {
            tabs[i] = std::make_unique<TabButton> (names[i]);
            tabs[i]->setTooltip (juce::String ("Show the ") + names[i] + " page");
            tabs[i]->onClick = [this, i] { if (onTabSelected) onTabSelected (i); };
            addAndMakeVisible (*tabs[i]);
        }
        tabs[0]->setToggleState (true, juce::dontSendNotification);

        presetButton.setTooltip ("Open the preset bank");
        presetButton.onClick = [this] { if (onTabSelected) onTabSelected (4); };
        addAndMakeVisible (presetButton);

        prevButton.setButtonText ("<");
        prevButton.setTooltip ("Previous preset");
        prevButton.onClick = [this] { processor.getPresetManager().loadNext (false); refreshPresetName(); };
        addAndMakeVisible (prevButton);

        nextButton.setButtonText (">");
        nextButton.setTooltip ("Next preset");
        nextButton.onClick = [this] { processor.getPresetManager().loadNext (true); refreshPresetName(); };
        addAndMakeVisible (nextButton);

        saveButton.setButtonText ("SAVE");
        saveButton.setTooltip ("Save the current sound as a user preset");
        saveButton.onClick = [this] { if (onSaveRequested) onSaveRequested(); };
        addAndMakeVisible (saveButton);

        initButton.setButtonText ("INIT");
        initButton.setTooltip ("Reset to the init patch");
        initButton.onClick = [this] { processor.getPresetManager().initPatch(); refreshPresetName(); };
        addAndMakeVisible (initButton);

        randButton.setButtonText ("RAND");
        randButton.setTooltip ("Generate a random patch");
        randButton.onClick = [this] { processor.getPresetManager().randomizePatch(); refreshPresetName(); };
        addAndMakeVisible (randButton);

        statusLabel.setJustificationType (juce::Justification::centredRight);
        statusLabel.setFont (LookAndFeelYD::uiFont (10.5f));
        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (statusLabel);

        addAndMakeVisible (meter);

        startTimerHz (8);
        refreshPresetName();
    }

    std::function<void (int)> onTabSelected;   // wired by the editor
    std::function<void()>     onSaveRequested;

    void setActiveTab (int index)
    {
        for (int i = 0; i < kNumTabs; ++i)
            tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    void refreshPresetName()
    {
        auto& pm = processor.getPresetManager();
        const auto cat = pm.getCurrentCategory();
        presetButton.setButtonText ((cat.isNotEmpty() && cat != "Init" ? cat + "  |  " : juce::String())
                                    + pm.getCurrentName());
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (theme::panel.brighter (0.04f), 0, 0,
                                                 theme::panel.darker (0.1f), 0, (float) getHeight(), false));
        g.fillRect (getLocalBounds());
        g.setColour (theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // --- brand: globe glyph (latitude/longitude arcs) + wordmark
        {
            const float cx = 25.0f, cy = getHeight() * 0.5f - 4.0f, r = 12.0f;
            g.setColour (theme::accent);
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.6f);
            g.setColour (theme::accent.withAlpha (0.75f));
            g.drawEllipse (cx - r * 0.45f, cy - r, r * 0.9f, r * 2.0f, 1.1f);      // meridian
            g.setColour (theme::accent2.withAlpha (0.9f));
            g.drawLine (cx - r, cy, cx + r, cy, 1.1f);                              // equator
            const float pr = r * 0.62f;
            g.setColour (theme::accent2.withAlpha (0.55f));
            g.drawLine (cx - pr, cy - r * 0.52f, cx + pr, cy - r * 0.52f, 0.9f);    // 9th parallel up
            g.drawLine (cx - pr, cy + r * 0.52f, cx + pr, cy + r * 0.52f, 0.9f);    // and down

            g.setColour (theme::text);
            g.setFont (LookAndFeelYD::uiFont (21.0f, true));
            g.drawText ("GLOBUS", 44, 8, 100, 26, juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (8.5f));
            g.drawText ("NINTH PARALLEL AUDIO", 45, 36, 130, 11, juce::Justification::centredLeft);
        }

        // --- MIDI LED
        const auto led = midiLedBounds();
        g.setColour (midiLit ? theme::accent : theme::track);
        g.fillEllipse (led);
        if (midiLit)
        {
            g.setColour (theme::accent.withAlpha (0.4f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (8.5f));
        g.drawText ("MIDI", (int) led.getX() - 7, (int) led.getBottom() + 1, 26, 10, juce::Justification::centred);
    }

    void resized() override
    {
        const int h = getHeight();
        const int bh = 26, y = (h - bh) / 2;

        int x = 168;
        for (int i = 0; i < kNumTabs; ++i)
        {
            tabs[i]->setBounds (x, 6, i == 4 ? 74 : 62, h - 12);
            x += (i == 4 ? 74 : 62) + 3;
        }

        prevButton.setBounds (528, y, 26, bh);
        presetButton.setBounds (557, y, 196, bh);
        nextButton.setBounds (756, y, 26, bh);

        saveButton.setBounds (794, y, 52, bh);
        initButton.setBounds (849, y, 46, bh);
        randButton.setBounds (898, y, 52, bh);

        meter.setBounds (getWidth() - 76, (h - 22) / 2, 64, 22);
        statusLabel.setBounds (getWidth() - 232, 0, 148, h);
    }

private:
    juce::Rectangle<float> midiLedBounds() const
    {
        return { (float) getWidth() - 96.0f, getHeight() * 0.5f - 9.0f, 10.0f, 10.0f };
    }

    void timerCallback() override
    {
        const bool lit = processor.consumeMidiActivity();
        if (lit != midiLit)
        {
            midiLit = lit;
            repaint (midiLedBounds().expanded (6.0f).toNearestInt());
        }
        const int cpu = juce::roundToInt (processor.getCpuLoad() * 100.0f);
        statusLabel.setText ("CPU " + juce::String (cpu) + "%  VOX "
                             + juce::String (processor.getEngine().getActiveVoiceCount()),
                             juce::dontSendNotification);
        refreshPresetName();
    }

    YDCoreAudioProcessor& processor;
    std::unique_ptr<TabButton> tabs[kNumTabs];
    juce::TextButton presetButton, prevButton, nextButton, saveButton, initButton, randButton;
    juce::Label statusLabel;
    OutputMeter meter;
    bool midiLit = false;
};

} // namespace ydc
