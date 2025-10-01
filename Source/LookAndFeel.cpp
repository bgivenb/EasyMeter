#include <JuceHeader.h>
#include "LookAndFeel.h"

MmLookAndFeel::MmLookAndFeel()
{
    using namespace juce;
    auto background   = Colour::fromRGB (8, 11, 29);
    auto backgroundHi = background.brighter (0.12f);
    auto accent       = Colour::fromRGB (90, 212, 255);
    auto accentAlt    = Colour::fromRGB (146, 128, 255);
    auto accentGlow   = Colour::fromRGB (255, 126, 204);

    setColour (ResizableWindow::backgroundColourId, background);
    setColour (PopupMenu::backgroundColourId, backgroundHi);
    setColour (PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.24f));
    setColour (PopupMenu::textColourId, Colour::fromRGB (216, 230, 255));
    setColour (Slider::thumbColourId, accent);
    setColour (Slider::trackColourId, accent.withAlpha (0.58f));
    setColour (Slider::rotarySliderFillColourId, accent);
    setColour (Slider::rotarySliderOutlineColourId, accentAlt.withAlpha (0.55f));

    setColour (juce::TabbedButtonBar::tabOutlineColourId, Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, accentGlow.withAlpha (0.75f));
    setColour (juce::TabbedButtonBar::tabTextColourId, Colour::fromRGB (138, 150, 196));
    setColour (juce::TabbedButtonBar::frontTextColourId, Colour::fromRGB (226, 240, 255));
    setColour (MmLookAndFeel::tabInactiveBackgroundColourId, background.brighter (0.04f));
    setColour (MmLookAndFeel::tabActiveBackgroundColourId, Colour::fromRGB (32, 56, 102));

    setColour (ComboBox::backgroundColourId, backgroundHi);
    setColour (ComboBox::textColourId, Colour::fromRGB (212, 224, 255));
    setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    setColour (ComboBox::arrowColourId, accent.withAlpha (0.85f));
    setColour (ComboBox::focusedOutlineColourId, accentGlow.withAlpha (0.9f));
    setColour (TextButton::buttonColourId, Colours::transparentBlack);
    setColour (TextButton::buttonOnColourId, accentGlow.withAlpha (0.32f));
    setColour (TextButton::textColourOnId, Colour::fromRGB (226, 240, 255));
    setColour (TextButton::textColourOffId, Colour::fromRGB (168, 182, 226));
}

void MmLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const bool isFront = button.isFrontTab();
    auto bounds = button.getLocalBounds().toFloat().reduced (4.0f, 6.0f);

    const auto background = button.findColour (isFront
                                                   ? MmLookAndFeel::tabActiveBackgroundColourId
                                                   : MmLookAndFeel::tabInactiveBackgroundColourId);
    auto fillColour = background;

    if (! button.isEnabled())
        fillColour = fillColour.withAlpha (0.35f);
    else if (isMouseOver || isMouseDown)
        fillColour = fillColour.brighter (0.08f);

    const float radius = juce::jmin (bounds.getHeight() * 0.5f, 18.0f);
    g.setColour (fillColour.withAlpha (isFront ? 0.95f : 0.72f));
    g.fillRoundedRectangle (bounds, radius);

    if (isFront)
    {
        auto accent = button.findColour (juce::TabbedButtonBar::frontOutlineColourId);
        g.setColour (accent.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, radius, 1.6f);

        auto gloss = bounds.withHeight (bounds.getHeight() * 0.45f);
        juce::ColourGradient glossGradient (accent.withAlpha (0.32f), gloss.getTopLeft(),
                                           accent.withAlpha (0.05f), gloss.getBottomRight(), false);
        g.setGradientFill (glossGradient);
        g.fillRoundedRectangle (gloss, radius * 0.85f);
    }

    auto textColour = button.findColour (isFront
                                             ? juce::TabbedButtonBar::frontTextColourId
                                             : juce::TabbedButtonBar::tabTextColourId);
    if (! button.isEnabled())
        textColour = textColour.withAlpha (0.35f);
    else if (! isFront)
        textColour = textColour.withAlpha (isMouseOver ? 0.9f : 0.7f);

    juce::Font font (juce::FontOptions (15.0f, juce::Font::bold));
    g.setColour (textColour);
    g.setFont (font);
    g.drawFittedText (button.getButtonText().toUpperCase(),
                      button.getLocalBounds().reduced (12, 4), juce::Justification::centred, 1);
}

int MmLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    juce::Font font (juce::FontOptions (15.0f, juce::Font::bold));
    const auto text = button.getButtonText().toUpperCase();

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    const auto bounds = glyphs.getBoundingBox (0, -1, true);
    const int textWidth = juce::roundToInt (bounds.getWidth());

    return juce::jmax (textWidth + 36, tabDepth * 2);
}

void MmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool isMouseOver, [[maybe_unused]] bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.8f);
    const float radius = juce::jmin (9.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f);

    const auto onColour = button.findColour (juce::TextButton::textColourOnId);
    const auto offColour = button.findColour (juce::TextButton::textColourOffId);
    const bool isDown = button.getToggleState();

    auto base = isDown ? onColour : offColour;
    if (isMouseOver)
        base = base.brighter (0.12f);

    juce::ColourGradient gradient (base.withAlpha (0.28f), bounds.getTopLeft(),
                                   base.withAlpha (0.08f), bounds.getBottomRight(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, radius);

    auto outlineColour = isDown ? onColour.withAlpha (0.85f) : offColour.withAlpha (0.6f);
    g.setColour (outlineColour);
    g.drawRoundedRectangle (bounds, radius, 1.2f);

    if (isDown)
    {
        auto glowBounds = bounds.expanded (0.0f, 2.0f);
        g.setColour (onColour.withAlpha (0.18f));
        g.drawRoundedRectangle (glowBounds, radius + 3.0f, 1.0f);
    }
}

void MmLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                  int buttonX, int buttonY, int buttonW, int buttonH,
                                  juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float radius = juce::jmin (bounds.getHeight() * 0.5f, 10.0f);

    auto background = box.findColour (juce::ComboBox::backgroundColourId);
    auto outline = box.findColour (juce::ComboBox::outlineColourId);
    auto focusOutline = box.findColour (juce::ComboBox::focusedOutlineColourId);

    juce::ColourGradient fill (background.brighter (0.12f), bounds.getTopLeft(),
                               background.darker (0.18f), bounds.getBottomRight(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (bounds, radius);

    auto border = box.isEnabled() ? outline : outline.withAlpha (0.25f);
    if (box.hasKeyboardFocus (true))
        border = focusOutline;
    if (isButtonDown)
        border = border.brighter (0.15f);

    g.setColour (border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds, radius, 1.4f);

    juce::Rectangle<float> arrowButton ((float) buttonX, (float) buttonY, (float) buttonW, (float) buttonH);
    if (arrowButton.isEmpty())
    {
        const float size = bounds.getHeight();
        arrowButton = juce::Rectangle<float> (bounds.getRight() - size, bounds.getY(), size, size);
    }

    auto arrowBounds = arrowButton.reduced (2.0f, 3.0f);
    auto arrowBackground = background.brighter (0.2f);
    if (! box.isEnabled())
        arrowBackground = arrowBackground.withAlpha (0.4f);

    juce::ColourGradient arrowFill (arrowBackground.withAlpha (0.95f), arrowBounds.getTopLeft(),
                                    arrowBackground.darker (0.2f).withAlpha (0.95f), arrowBounds.getBottomRight(), false);
    g.setGradientFill (arrowFill);
    g.fillRoundedRectangle (arrowBounds, juce::jmax (2.0f, radius - 3.0f));

    g.setColour (border.withAlpha (0.45f));
    const auto dividerX = juce::jmax (bounds.getX(), arrowBounds.getX() - 2.0f);
    g.drawLine (dividerX, bounds.getY() + 3.0f, dividerX, bounds.getBottom() - 3.0f, 1.0f);

    auto arrowColour = box.findColour (juce::ComboBox::arrowColourId);
    if (! box.isEnabled())
        arrowColour = arrowColour.withAlpha (0.35f);

    juce::Path arrow;
    auto triangle = arrowBounds.reduced (arrowBounds.getWidth() * 0.32f, arrowBounds.getHeight() * 0.38f);
    const auto centre = triangle.getCentre();
    const float halfWidth = triangle.getWidth() * 0.5f;
    const float heightScale = juce::jmax (triangle.getHeight() * 0.6f, 2.0f);
    arrow.addTriangle (centre.x - halfWidth, centre.y - heightScale * 0.5f,
                       centre.x + halfWidth, centre.y - heightScale * 0.5f,
                       centre.x, centre.y + heightScale * 0.5f);

    g.setColour (arrowColour);
    g.fillPath (arrow);
}

juce::Font MmLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.5f));
}

void MmLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    auto bounds = box.getLocalBounds().reduced (10, 4);
    bounds.removeFromRight (juce::roundToInt (bounds.getHeight() * 1.2f));
    label.setBounds (bounds);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (box.getJustificationType());
}
