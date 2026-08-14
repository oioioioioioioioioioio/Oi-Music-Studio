#include "StudioLookAndFeel.h"

namespace oi
{
namespace
{
juce::Colour rgb (juce::uint32 value) { return juce::Colour (0xff000000u | value); }
}

StudioLookAndFeel::StudioLookAndFeel()
{
    setTheme (ThemeMode::dark);
}

void StudioLookAndFeel::setTheme (ThemeMode mode)
{
    palette = mode == ThemeMode::dark
        ? Palette { rgb (0x0f1313), rgb (0x151a19), rgb (0x1a201f), rgb (0x212826),
                    rgb (0x29322f), rgb (0x34403c), rgb (0x29322f), rgb (0xedf3f0),
                    rgb (0x9ba8a3), rgb (0x68756f), rgb (0x43c6a2), rgb (0x24463d),
                    rgb (0x071b15), rgb (0x6b9ff1), rgb (0xf17868), rgb (0xdcb055),
                    rgb (0x55ca7a), rgb (0xee5f64) }
        : Palette { rgb (0xedf1ef), rgb (0xf8faf9), rgb (0xffffff), rgb (0xf1f5f3),
                    rgb (0xe7ece9), rgb (0xcad3ce), rgb (0xdce3df), rgb (0x17211e),
                    rgb (0x5e6c66), rgb (0x89948f), rgb (0x128f70), rgb (0xd6eee7),
                    rgb (0xffffff), rgb (0x3976ca), rgb (0xce574b), rgb (0xa8730d),
                    rgb (0x238b48), rgb (0xce4148) };

    setColour (juce::ResizableWindow::backgroundColourId, palette.background);
    setColour (juce::Label::textColourId, palette.text);
    setColour (juce::TextButton::textColourOffId, palette.muted);
    setColour (juce::TextButton::textColourOnId, palette.text);
    setColour (juce::TextEditor::backgroundColourId, palette.background);
    setColour (juce::TextEditor::textColourId, palette.text);
    setColour (juce::TextEditor::outlineColourId, palette.line);
    setColour (juce::ComboBox::backgroundColourId, palette.background);
    setColour (juce::ComboBox::textColourId, palette.text);
    setColour (juce::ComboBox::outlineColourId, palette.line);
    setColour (juce::PopupMenu::backgroundColourId, palette.panel);
    setColour (juce::PopupMenu::textColourId, palette.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.hover);
    setColour (juce::PopupMenu::highlightedTextColourId, palette.text);
    setColour (juce::Slider::thumbColourId, palette.accent);
    setColour (juce::Slider::trackColourId, palette.accent);
    setColour (juce::Slider::backgroundColourId, palette.hover);
    setColour (juce::ScrollBar::thumbColourId, palette.line);
    setColour (juce::ScrollBar::backgroundColourId, palette.background);
    setColour (juce::TooltipWindow::backgroundColourId, palette.raised);
    setColour (juce::TooltipWindow::textColourId, palette.text);
    setColour (juce::TooltipWindow::outlineColourId, palette.line);
}

juce::Font StudioLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jlimit (11.0f, 13.0f, buttonHeight * 0.36f)));
}

juce::Font StudioLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (12.0f));
}

juce::Font StudioLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (11.0f));
}

void StudioLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour&, bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = button.getToggleState() ? palette.accentSoft : palette.panel;

    if (button.getProperties().contains ("accent"))
        colour = palette.accent;
    else if (down)
        colour = palette.line;
    else if (highlighted)
        colour = palette.hover;

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (button.getToggleState() ? palette.accent : palette.line);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void StudioLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool, bool)
{
    const auto accent = button.getProperties().contains ("accent");
    g.setColour (accent ? palette.accentInk : (button.getToggleState() ? palette.text : palette.muted));
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 2),
                      juce::Justification::centred, 1);
}

void StudioLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const auto cy = static_cast<float> (y + height / 2);
        const auto start = static_cast<float> (x + 2);
        const auto end = static_cast<float> (x + width - 2);
        g.setColour (palette.hover);
        g.drawLine (start, cy, end, cy, 4.0f);
        g.setColour (palette.accent);
        g.drawLine (start, cy, sliderPos, cy, 4.0f);
        g.fillEllipse (sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        g.setColour (palette.background);
        g.drawEllipse (sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.0f);
        return;
    }

    LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, sliderPos,
                                      sliderPos, style, slider);
}

void StudioLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool highlighted, bool)
{
    auto box = juce::Rectangle<float> (2.0f, (button.getHeight() - 16.0f) * 0.5f, 29.0f, 16.0f);
    g.setColour (button.getToggleState() ? palette.accentSoft : palette.background);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (button.getToggleState() ? palette.accent : palette.line);
    g.drawRoundedRectangle (box, 8.0f, 1.0f);
    const auto knobX = button.getToggleState() ? box.getRight() - 13.0f : box.getX() + 3.0f;
    g.setColour (button.getToggleState() ? palette.accent : palette.muted);
    g.fillEllipse (knobX, box.getY() + 3.0f, 10.0f, 10.0f);
    g.setColour (highlighted ? palette.text : palette.muted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (38),
                      juce::Justification::centredLeft, 1);
}

void StudioLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox&)
{
    auto bounds = juce::Rectangle<float> (0.5f, 0.5f, static_cast<float> (width - 1), static_cast<float> (height - 1));
    g.setColour (palette.background);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (palette.line);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    juce::Path arrow;
    arrow.addTriangle (static_cast<float> (width - 17), height * 0.43f,
                       static_cast<float> (width - 9), height * 0.43f,
                       static_cast<float> (width - 13), height * 0.62f);
    g.setColour (palette.muted);
    g.fillPath (arrow);
}

void StudioLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (palette.panel);
    g.setColour (palette.line);
    g.drawRect (0, 0, width, height);
}
} // namespace oi
