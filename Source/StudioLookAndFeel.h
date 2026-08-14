#pragma once

#include <JuceHeader.h>
#include "AppState.h"

namespace oi
{
struct Palette
{
    juce::Colour background;
    juce::Colour bar;
    juce::Colour panel;
    juce::Colour raised;
    juce::Colour hover;
    juce::Colour line;
    juce::Colour lineSoft;
    juce::Colour text;
    juce::Colour muted;
    juce::Colour faint;
    juce::Colour accent;
    juce::Colour accentSoft;
    juce::Colour accentInk;
    juce::Colour blue;
    juce::Colour coral;
    juce::Colour yellow;
    juce::Colour green;
    juce::Colour red;
};

class StudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel();

    void setTheme (ThemeMode mode);
    [[nodiscard]] const Palette& colours() const noexcept { return palette; }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

private:
    Palette palette;
};
} // namespace oi
