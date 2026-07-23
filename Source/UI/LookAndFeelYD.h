// GLOBUS — dark graphite look & feel with electric-blue / violet accents.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ydc
{
namespace theme
{
    const juce::Colour bg          { 0xff121419 };
    const juce::Colour panel       { 0xff1a1d24 };
    const juce::Colour panelLight  { 0xff20242d };
    const juce::Colour outline     { 0xff2a303b };
    const juce::Colour text        { 0xffd8dce4 };
    const juce::Colour textDim     { 0xff8a93a4 };
    const juce::Colour accent      { 0xff4da3ff };   // electric blue
    const juce::Colour accent2     { 0xff8b7cf8 };   // violet
    const juce::Colour knobFace    { 0xff232833 };
    const juce::Colour track       { 0xff2c323d };
    const juce::Colour warn        { 0xffe8b04b };
}

class LookAndFeelYD : public juce::LookAndFeel_V4
{
public:
    LookAndFeelYD();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    static juce::Font uiFont (float height, bool bold = false);
};

} // namespace ydc
