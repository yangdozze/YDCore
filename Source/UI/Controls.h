// GLOBUS — reusable UI building blocks: labelled knobs, selectors, toggles,
// section panels. Every control auto-wires tooltips, double-click reset and
// shift-drag fine adjustment.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeelYD.h"
#include "../Parameters.h"

namespace ydc
{
/** Rotary slider with shift-drag fine control. */
class FineSlider : public juce::Slider
{
public:
    FineSlider() = default;
    void mouseDown (const juce::MouseEvent& e) override
    {
        updateSensitivity (e);
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        updateSensitivity (e);
        juce::Slider::mouseDrag (e);
    }
private:
    void updateSensitivity (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1800 : 250);
    }
};

/** Labelled rotary knob bound to one parameter.
    The label swaps to a live value readout while hovering or dragging. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& text) : name (text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (slider);

        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (LookAndFeelYD::uiFont (11.0f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        slider.addMouseListener (this, false);
        slider.onValueChange = [this] { if (showingValue) updateValueText(); };
    }

    ~Knob() override { slider.removeMouseListener (this); }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        param = apvts.getParameter (paramID);
        if (param != nullptr)
        {
            const auto& range = param->getNormalisableRange();
            slider.setDoubleClickReturnValue (true, range.convertFrom0to1 (param->getDefaultValue()));
        }
        const auto tip = getTooltip (paramID);
        slider.setTooltip (tip);
        label.setTooltip (tip);
    }

    void setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void mouseEnter (const juce::MouseEvent&) override { showingValue = true;  updateValueText(); }
    void mouseExit  (const juce::MouseEvent&) override
    {
        showingValue = false;
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setText (name, juce::dontSendNotification);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromBottom (13));
        slider.setBounds (b);
    }

    FineSlider slider;
    juce::Label label;

private:
    void updateValueText()
    {
        juce::String v;
        if (param != nullptr)
            v = param->getText (param->getValue(), 12);
        else
            v = slider.getTextFromValue (slider.getValue());
        label.setColour (juce::Label::textColourId, theme::text);
        label.setText (v, juce::dontSendNotification);
    }

    juce::String name;
    bool showingValue = false;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Labelled combo box bound to a choice parameter. */
class Selector : public juce::Component
{
public:
    explicit Selector (const juce::String& text, bool showLabel = true)
    {
        addAndMakeVisible (box);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (LookAndFeelYD::uiFont (10.5f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setVisible (showLabel);
        addAndMakeVisible (label);
        hasLabel = showLabel;
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
        {
            box.clear (juce::dontSendNotification);
            box.addItemList (choice->choices, 1);
        }
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, box);
        box.setTooltip (getTooltip (paramID));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (hasLabel)
            label.setBounds (b.removeFromTop (12));
        box.setBounds (b);
    }

    juce::ComboBox box;
    juce::Label label;

private:
    bool hasLabel = true;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** LED toggle bound to a bool parameter. */
class Toggle : public juce::Component
{
public:
    explicit Toggle (const juce::String& text)
    {
        button.setButtonText (text);
        addAndMakeVisible (button);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, button);
        button.setTooltip (getTooltip (paramID));
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Titled panel that hosts a section's controls (frameless mode = bare content
    area for embedding inside another panel, e.g. the modulation editor). */
class SectionPanel : public juce::Component
{
public:
    explicit SectionPanel (const juce::String& titleText, juce::Colour accent = theme::accent)
        : title (titleText), accentColour (accent) {}

    void setFrameless (bool shouldBeFrameless) { frameless = shouldBeFrameless; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (frameless)
            return;
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setGradientFill (juce::ColourGradient (theme::panel.brighter (0.02f), 0, 0,
                                                 theme::panel.darker (0.04f), 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 6.0f, 1.0f);

        g.setColour (accentColour);
        g.fillRoundedRectangle (8.0f, 6.0f, 3.0f, 11.0f, 1.5f);
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (12.0f, true));
        g.drawText (title, 17, 3, getWidth() - 24, 16, juce::Justification::centredLeft);
    }

    juce::Rectangle<int> content() const
    {
        return frameless ? getLocalBounds().reduced (4)
                         : getLocalBounds().reduced (7).withTrimmedTop (16);
    }

private:
    juce::String title;
    juce::Colour accentColour;
    bool frameless = false;
};

//==============================================================================
/** Padlock toggle for the randomizer section locks (session UI state; not a
    host parameter). All buttons bound to the same section stay in sync. */
class LockButton : public juce::Button
{
public:
    LockButton (std::function<bool()> get, std::function<void (bool)> set, const juce::String& sectionName)
        : juce::Button ("lock"), getter (std::move (get)), setter (std::move (set))
    {
        setTooltip ("Lock the " + sectionName + " section against randomization");
        setClickingTogglesState (false);
        onClick = [this] { setter (! getter()); repaint(); };
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const bool locked = getter();
        const auto col = locked ? theme::warn : (highlighted ? theme::text : theme::textDim.withAlpha (0.55f));
        auto b = getLocalBounds().toFloat();
        const float s = juce::jmin (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();

        // body
        const float bw = s * 0.52f, bh = s * 0.40f;
        g.setColour (col);
        g.fillRoundedRectangle (c.x - bw / 2, c.y - bh * 0.1f, bw, bh, 2.0f);
        // shackle (open when unlocked)
        juce::Path sh;
        const float r = bw * 0.34f;
        sh.addCentredArc (c.x + (locked ? 0.0f : bw * 0.18f), c.y - bh * 0.15f, r, r * 1.25f,
                          0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (sh, juce::PathStrokeType (1.6f));
    }

private:
    std::function<bool()> getter;
    std::function<void (bool)> setter;
};

/** Lay a row of equally-sized cells across a bounds rectangle. */
inline std::vector<juce::Rectangle<int>> rowCells (juce::Rectangle<int> area, int count, int gap = 4)
{
    std::vector<juce::Rectangle<int>> cells;
    if (count <= 0)
        return cells;
    const int w = (area.getWidth() - gap * (count - 1)) / count;
    for (int i = 0; i < count; ++i)
    {
        cells.push_back (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }
    return cells;
}

} // namespace ydc
