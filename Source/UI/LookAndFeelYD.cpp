#include <juce_audio_utils/juce_audio_utils.h>
#include "LookAndFeelYD.h"

namespace ydc
{
juce::Font LookAndFeelYD::uiFont (float height, bool bold)
{
    auto opts = juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font (opts);
}

LookAndFeelYD::LookAndFeelYD()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Slider::textBoxTextColourId, theme::textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    setColour (juce::Slider::thumbColourId, theme::accent);
    setColour (juce::Slider::trackColourId, theme::track);
    setColour (juce::Slider::backgroundColourId, theme::track);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::ComboBox::backgroundColourId, theme::panelLight);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, theme::panelLight);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, theme::panelLight);
    setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, theme::text);
    setColour (juce::ToggleButton::tickColourId, theme::accent);
    setColour (juce::TextEditor::backgroundColourId, theme::bg);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent.withAlpha (0.6f));
    setColour (juce::ListBox::backgroundColourId, theme::panel);
    setColour (juce::ScrollBar::thumbColourId, theme::outline.brighter (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff262b35));
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::outline);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd0d4dc));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff14161a));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent.withAlpha (0.7f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha (0.3f));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void LookAndFeelYD::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                      float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float arcR   = radius - 2.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW  = juce::jmax (2.4f, radius * 0.14f);

    // knob body
    const auto faceR = arcR - lineW * 0.9f;
    g.setColour (theme::knobFace);
    g.fillEllipse (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour (theme::outline);
    g.drawEllipse (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

    // background track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme::track);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc: bipolar knobs sweep from 12 o'clock
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum())) : 0.0f;
    const float zeroAngle = rotaryStartAngle + zeroPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path value;
    if (std::abs (angle - zeroAngle) > 0.01f)
    {
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        const auto accentCol = slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId)
                                                  : theme::textDim.withAlpha (0.4f);
        g.setColour (accentCol);
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pointer
    const float pr = faceR - 3.0f;
    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (pr * 0.35f, angle));
    pointer.lineTo (centre.getPointOnCircumference (pr, angle));
    g.setColour (slider.isEnabled() ? theme::text : theme::textDim);
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // keyboard-focus ring
    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (theme::accent.withAlpha (0.55f));
        g.drawEllipse (centre.x - arcR - 2.5f, centre.y - arcR - 2.5f, (arcR + 2.5f) * 2.0f, (arcR + 2.5f) * 2.0f, 1.4f);
    }
}

void LookAndFeelYD::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                      float minSliderPos, float maxSliderPos,
                                      juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused (minSliderPos, maxSliderPos);
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical)
    {
        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const auto track = horizontal
            ? juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 2.0f, (float) w, 4.0f)
            : juce::Rectangle<float> ((float) x + w * 0.5f - 2.0f, (float) y, 4.0f, (float) h);

        g.setColour (theme::track);
        g.fillRoundedRectangle (track, 2.0f);

        // bipolar ranges fill from their zero point, unipolar from the start
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        auto filled = track;
        if (horizontal)
        {
            if (bipolar)
            {
                const float zeroX = (float) x + (float) w
                                  * (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()));
                filled = juce::Rectangle<float> (juce::jmin (zeroX, sliderPos), track.getY(),
                                                 std::abs (sliderPos - zeroX), track.getHeight());
            }
            else
                filled = filled.withWidth (sliderPos - (float) x);
        }
        else
            filled = filled.withTop (sliderPos);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.fillRoundedRectangle (filled, 2.0f);

        const auto thumbPos = horizontal ? juce::Point<float> (sliderPos, (float) y + h * 0.5f)
                                         : juce::Point<float> ((float) x + w * 0.5f, sliderPos);
        g.setColour (theme::text);
        g.fillEllipse (thumbPos.x - 5.0f, thumbPos.y - 5.0f, 10.0f, 10.0f);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}

void LookAndFeelYD::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? theme::accent.withAlpha (0.5f) : theme::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 13.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (theme::textDim);
    g.fillPath (arrow);
}

void LookAndFeelYD::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeelYD::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                      bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    // LED
    const float led = juce::jmin (10.0f, bounds.getHeight() * 0.45f);
    const auto ledBounds = juce::Rectangle<float> (2.0f, bounds.getCentreY() - led * 0.5f, led, led);
    g.setColour (on ? theme::accent : theme::track);
    g.fillEllipse (ledBounds);
    if (on)
    {
        g.setColour (theme::accent.withAlpha (0.35f));
        g.fillEllipse (ledBounds.expanded (2.5f));
    }
    g.setColour (theme::outline);
    g.drawEllipse (ledBounds, 1.0f);

    g.setColour (on ? theme::text : (highlighted ? theme::text : theme::textDim));
    g.setFont (uiFont (juce::jmin (13.0f, bounds.getHeight() * 0.7f)));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (led + 7.0f).toNearestInt(),
                juce::Justification::centredLeft, true);

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (theme::accent.withAlpha (0.45f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.2f);
    }
}

void LookAndFeelYD::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& bgCol,
                                          bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bgCol;
    if (down)             c = theme::accent.withAlpha (0.45f);
    else if (highlighted) c = c.brighter (0.15f);
    g.setColour (c);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (button.getToggleState() ? theme::accent.withAlpha (0.8f) : theme::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

juce::Font LookAndFeelYD::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f); }
juce::Font LookAndFeelYD::getPopupMenuFont()                { return uiFont (14.0f); }
juce::Font LookAndFeelYD::getLabelFont (juce::Label& l)     { return uiFont (juce::jmin (13.0f, l.getFont().getHeight())); }

} // namespace ydc
