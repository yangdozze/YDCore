// YD Core — reusable UI building blocks: labelled knobs, selectors, toggles,
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

/** Labelled rotary knob bound to one parameter. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (slider);

        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (LookAndFeelYD::uiFont (11.0f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    }

    void attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        if (auto* p = apvts.getParameter (paramID))
        {
            const auto& range = p->getNormalisableRange();
            slider.setDoubleClickReturnValue (true, range.convertFrom0to1 (p->getDefaultValue()));
        }
        const auto tip = getTooltip (paramID);
        slider.setTooltip (tip);
        label.setTooltip (tip);
    }

    void setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromBottom (13));
        slider.setBounds (b);
    }

    FineSlider slider;
    juce::Label label;

private:
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

/** Titled panel that hosts a section's controls. */
class SectionPanel : public juce::Component
{
public:
    explicit SectionPanel (const juce::String& titleText, juce::Colour accent = theme::accent)
        : title (titleText), accentColour (accent) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (b, 6.0f, 1.0f);

        g.setColour (accentColour);
        g.fillRoundedRectangle (8.0f, 7.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (12.5f, true));
        g.drawText (title, 17, 4, getWidth() - 24, 18, juce::Justification::centredLeft);
    }

    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8).withTrimmedTop (18); }

private:
    juce::String title;
    juce::Colour accentColour;
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
