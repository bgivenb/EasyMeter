#include <JuceHeader.h>
#pragma once

class MmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MmLookAndFeel();

    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool, bool) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    enum ColourIds
    {
        tabActiveBackgroundColourId  = 0x2000100,
        tabInactiveBackgroundColourId
            = 0x2000101,
    };

};
