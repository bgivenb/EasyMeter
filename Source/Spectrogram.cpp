#include "Spectrogram.h"

#include <algorithm>
#include <cmath>
#include <iterator>

SpectrogramMeter::SpectrogramMeter()
    : MeterComponent ("Spectrogram")
{
    scaleBox.addItem ("Linear", (int) FrequencyScale::linear);
    scaleBox.addItem ("Log", (int) FrequencyScale::logarithmic);
    scaleBox.setSelectedId ((int) FrequencyScale::logarithmic, juce::dontSendNotification);
    scaleBox.setJustificationType (juce::Justification::centredLeft);
    scaleBox.onChange = [this]
    {
        spectrogramDirty = true;
        refreshImage();
        refreshStatusText();
        repaint();
    };

    paletteBox.addItem ("Inferno", 1);
    paletteBox.addItem ("Cosmic", 2);
    paletteBox.setSelectedId (1, juce::dontSendNotification);
    paletteBox.setJustificationType (juce::Justification::centredLeft);
    paletteBox.onChange = [this]
    {
        colourLutDirty = true;
        refreshImage();
        repaint();
    };

    timeSpanBox.addItem ("1 s", 1);
    timeSpanBox.addItem ("3 s", 3);
    timeSpanBox.setJustificationType (juce::Justification::centredLeft);
    timeSpanBox.setTextWhenNothingSelected ("Window");
    timeSpanBox.setSelectedId ((int) targetSpanSeconds, juce::dontSendNotification);
    timeSpanBox.onChange = [this]
    {
        const int id = timeSpanBox.getSelectedId();
        if (id > 0)
            targetSpanSeconds = (double) id;
        spectrogramDirty = true;
        refreshImage();
        refreshStatusText();
        repaint();
    };

    freezeButton.setToggleState (false, juce::dontSendNotification);
    freezeButton.onClick = [this]
    {
        freezeEnabled = freezeButton.getToggleState();
        if (! freezeEnabled)
            spectrogramDirty = true;

        refreshStatusText();
        repaint();
    };

    gridButton.setToggleState (true, juce::dontSendNotification);
    gridButton.onClick = [this]
    {
        gridEnabled = gridButton.getToggleState();
        refreshStatusText();
        repaint();
    };

    beatGridButton.setToggleState (true, juce::dontSendNotification);
    beatGridButton.onClick = [this]
    {
        beatGridEnabled = beatGridButton.getToggleState();
        refreshStatusText();
        repaint();
    };

    floorSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    floorSlider.setRange (-160.0, -20.0, 1.0);
    floorSlider.setSkewFactorFromMidPoint (-70.0);
    floorSlider.setTextValueSuffix (" dB");
    floorSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    floorSlider.setValue (minDb, juce::dontSendNotification);
    floorSlider.onValueChange = [this]
    {
        minDb = (float) floorSlider.getValue();
        spectrogramDirty = true;
        refreshImage();
        refreshStatusText();
        repaint();
    };

    intensitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    intensitySlider.setRange (0.4, 2.5, 0.01);
    intensitySlider.setSkewFactor (0.7);
    intensitySlider.setTextValueSuffix (" x");
    intensitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    intensitySlider.setValue (1.0, juce::dontSendNotification);
    intensitySlider.onValueChange = [this]
    {
        spectrogramDirty = true;
        refreshImage();
        repaint();
    };

    floorLabel.setJustificationType (juce::Justification::centredLeft);
    intensityLabel.setJustificationType (juce::Justification::centredLeft);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (scaleBox);
    addAndMakeVisible (paletteBox);
    addAndMakeVisible (timeSpanBox);
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (gridButton);
    addAndMakeVisible (beatGridButton);
    addAndMakeVisible (floorSlider);
    addAndMakeVisible (intensitySlider);
    addAndMakeVisible (floorLabel);
    addAndMakeVisible (intensityLabel);
    addAndMakeVisible (statusLabel);

    updateControlColours();
    refreshStatusText();
}

void SpectrogramMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
    {
        colourLutDirty = true;
        updateControlColours();
    }

    if (snapshot.sampleRate > 0.0)
        sampleRate = snapshot.sampleRate;

    transport = snapshot.transport;

    if (! freezeEnabled)
    {
        spectrogramData = snapshot.spectrogram;
        writePosition = snapshot.spectrogramWritePosition;
        wrapped = snapshot.spectrogramWrapped;
        spectrogramDirty = true;
    }

    if (spectrogramDirty || themeChanged || colourLutDirty)
        refreshImage();

    refreshStatusText();
    repaint();
}

void SpectrogramMeter::paint (juce::Graphics& g)
{
    auto panelBounds = drawPanelFrame (g);
    auto headerBounds = getPanelHeaderBounds();
    auto content = drawPanelHeader (g, panelBounds);

    if (! headerBounds.isEmpty())
    {
        auto info = headerBounds.reduced (12.0f, 6.0f);
        info.removeFromLeft (juce::jmin (info.getWidth(), 320.0f));
        if (info.getWidth() > 60.0f && hasData)
        {
            g.setColour (theme.text.withAlpha (0.6f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            juce::String infoText;
            infoText << juce::String (visibleSeconds, visibleSeconds < 10.0 ? 1 : 0) << " s span";
            infoText << "  •  " << visibleColumns << " frames";
            infoText << "  •  Nyquist " << juce::String (sampleRate * 0.5 / 1000.0, 1) << " kHz";
            g.drawFittedText (infoText, info.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    auto controlsArea = content.removeFromTop ((float) controlsHeight);
    if (! controlsArea.isEmpty())
    {
        auto block = controlsArea.reduced (4.0f, 2.0f);
        g.setColour (theme.background.withAlpha (0.22f));
        g.fillRoundedRectangle (block, 10.0f);
        g.setColour (theme.outline.withAlpha (0.24f));
        g.drawRoundedRectangle (block, 10.0f, 1.0f);
    }

    auto slidersArea = content.removeFromTop ((float) slidersHeight);
    if (! slidersArea.isEmpty())
    {
        auto block = slidersArea.reduced (4.0f, 2.0f);
        g.setColour (theme.background.withAlpha (0.18f));
        g.fillRoundedRectangle (block, 10.0f);
        g.setColour (theme.primary.withAlpha (0.18f));
        g.drawRoundedRectangle (block, 10.0f, 1.0f);
    }

    auto statusArea = content.removeFromBottom ((float) statusHeight);
    if (! statusArea.isEmpty())
    {
        auto block = statusArea.reduced (4.0f, 2.0f);
        g.setColour (theme.background.withAlpha (0.26f));
        g.fillRoundedRectangle (block, 8.0f);
        g.setColour (theme.outline.withAlpha (0.22f));
        g.drawRoundedRectangle (block, 8.0f, 1.0f);
    }

    auto graphBounds = content.reduced (12.0f, 8.0f);
    const float scaleWidth = 58.0f;
    const float axisAreaHeight = (float) axisHeight;

    juce::Rectangle<float> frequencyLabelArea = graphBounds.withWidth (scaleWidth);
    juce::Rectangle<float> timeAxisArea = graphBounds.withHeight (axisAreaHeight)
                                                        .withY (graphBounds.getBottom() - axisAreaHeight)
                                                        .withX (graphBounds.getX() + scaleWidth)
                                                        .withWidth (graphBounds.getWidth() - scaleWidth);

    auto heatmapArea = graphBounds;
    heatmapArea.removeFromLeft (scaleWidth);
    heatmapArea.removeFromBottom (axisAreaHeight);

    g.setColour (theme.background.darker (0.32f));
    g.fillRoundedRectangle (graphBounds, 14.0f);
    g.setColour (theme.outline.withAlpha (0.6f));
    g.drawRoundedRectangle (graphBounds, 14.0f, 1.1f);

    g.setColour (theme.background.darker (0.45f).withAlpha (0.65f));
    g.fillRoundedRectangle (heatmapArea, 10.0f);

    g.setColour (theme.outline.withAlpha (0.35f));
    g.drawLine (heatmapArea.getX(), heatmapArea.getY(), heatmapArea.getX(), heatmapArea.getBottom(), 1.0f);
    g.drawLine (heatmapArea.getX(), heatmapArea.getBottom(), heatmapArea.getRight(), heatmapArea.getBottom(), 1.0f);

    if (spectrogramImage.isValid() && hasData)
    {
        g.setOpacity (1.0f);
        g.drawImage (spectrogramImage, heatmapArea, juce::RectanglePlacement::stretchToFit);

        auto highlight = heatmapArea.withX (heatmapArea.getRight() - heatmapArea.getWidth() / juce::jmax (1, visibleColumns));
        g.setColour (theme.secondary.withAlpha (0.08f));
        g.fillRect (highlight);
    }
    else
    {
        g.setColour (theme.text.withAlpha (0.28f));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        auto message = freezeEnabled ? juce::String ("Frozen") : juce::String ("Waiting for audio...");
        g.drawFittedText (message, heatmapArea.toNearestInt(), juce::Justification::centred, 1);
    }

    std::vector<BeatMarker> beatMarkers;
    if (hasData && beatGridEnabled && shouldDrawBeatGrid())
        beatMarkers = createBeatMarkers (visibleSeconds);

    if (gridEnabled && hasData)
        drawGrid (g, heatmapArea);

    drawFrequencyScale (g, frequencyLabelArea, heatmapArea);
    drawTimeAxis (g, timeAxisArea, heatmapArea, beatMarkers, gridEnabled);
}

void SpectrogramMeter::resized()
{
    auto bounds = getPanelContentBounds().toNearestInt();

    auto controlArea = bounds.removeFromTop (controlsHeight);
    auto combosRow = controlArea.removeFromTop (controlArea.getHeight() / 2);
    auto togglesRow = controlArea;

    const int comboSpacing = 6;
    const int comboWidth = juce::jmax (120, (combosRow.getWidth() - comboSpacing * 2) / 3);

    auto placeCombo = [&] (juce::ComboBox& box, int width, bool addSpacing)
    {
        auto area = combosRow.removeFromLeft (width);
        box.setBounds (area.reduced (0, 2));
        if (addSpacing)
            combosRow.removeFromLeft (comboSpacing);
    };

    placeCombo (scaleBox, comboWidth, true);
    placeCombo (paletteBox, comboWidth, true);
    placeCombo (timeSpanBox, combosRow.getWidth(), false);

    const int toggleSpacing = 6;
    const int toggleWidth = juce::jmax (100, (togglesRow.getWidth() - toggleSpacing * 2) / 3);

    auto placeToggle = [&] (juce::ToggleButton& button, bool addSpacing)
    {
        auto area = togglesRow.removeFromLeft (toggleWidth);
        button.setBounds (area.reduced (4, 4));
        if (addSpacing)
            togglesRow.removeFromLeft (toggleSpacing);
    };

    placeToggle (freezeButton, true);
    placeToggle (gridButton, true);
    placeToggle (beatGridButton, false);

    auto sliderRow = bounds.removeFromTop (slidersHeight);
    const int labelWidth = 64;
    const int sliderSpacing = 10;
    const int sliderWidth = juce::jmax (160, (sliderRow.getWidth() - (labelWidth + sliderSpacing) * 2) / 2);

    auto setSlider = [&] (juce::Slider& slider, juce::Label& label)
    {
        auto area = sliderRow.removeFromLeft (sliderWidth + labelWidth);
        label.setBounds (area.removeFromLeft (labelWidth));
        slider.setBounds (area);
        sliderRow.removeFromLeft (sliderSpacing);
    };

    setSlider (floorSlider, floorLabel);
    setSlider (intensitySlider, intensityLabel);

    bounds.removeFromBottom (axisHeight);
    auto statusArea = bounds.removeFromBottom (statusHeight);
    statusLabel.setBounds (statusArea.reduced (4, 2));
}

void SpectrogramMeter::refreshImage()
{
    spectrogramDirty = false;

    if (colourLutDirty)
        rebuildColourLut();

    hasData = false;
    visibleColumns = 0;
    visibleSeconds = 0.0;

    const int totalColumns = spectrogramData.getNumSamples();
    const int bins = spectrogramData.getNumChannels();

    if (bins <= 0 || totalColumns <= 0)
    {
        spectrogramImage = {};
        orderedColumns.setSize (0, 0);
        return;
    }

    const bool wrappedHistory = wrapped && totalColumns > 0;
    const int availableColumns = wrappedHistory ? totalColumns
                                                : juce::jlimit (0, totalColumns, writePosition);

    if (availableColumns <= 0)
    {
        spectrogramImage = {};
        orderedColumns.setSize (0, 0);
        return;
    }

    orderedColumns.setSize (bins, availableColumns, false, false, true);

    const int startIndex = wrappedHistory ? writePosition : juce::jmax (0, juce::jmin (writePosition, totalColumns));

    for (int bin = 0; bin < bins; ++bin)
    {
        const float* src = spectrogramData.getReadPointer (bin);
        float* dest = orderedColumns.getWritePointer (bin);

        if (wrappedHistory)
        {
            int destIndex = 0;
            for (int column = 0; column < availableColumns; ++column)
            {
                const int srcIndex = (startIndex + column) % totalColumns;
                dest[destIndex++] = src[srcIndex];
            }
        }
        else
        {
            std::copy (src, src + availableColumns, dest);
        }
    }

    const double nyquist = sampleRate * 0.5;
    secondsPerColumn = (bins > 0 && sampleRate > 0.0) ? ((double) bins / 2.0) / sampleRate : 0.0;
    visibleColumns = getVisibleColumnCount (availableColumns);
    visibleSeconds = secondsPerColumn * visibleColumns;

    if (visibleColumns <= 0)
    {
        spectrogramImage = {};
        orderedColumns.setSize (0, 0);
        return;
    }

    const int outputHeight = juce::jmax (1, bins);
    spectrogramImage = juce::Image (juce::Image::ARGB, visibleColumns, outputHeight, false);

    const int columnOffset = juce::jmax (0, availableColumns - visibleColumns);

    if ((int) binRemap.size() != outputHeight)
        binRemap.resize ((size_t) outputHeight);

    const auto scaleMode = (FrequencyScale) scaleBox.getSelectedId();
    const double minFreq = 20.0;
    const double maxFreq = juce::jmax (minFreq * 1.01, nyquist);
    const double logMin = std::log10 (minFreq);
    const double logMax = std::log10 (maxFreq);
    const double logRange = juce::jmax (1.0e-6, logMax - logMin);
    const double freqRange = juce::jmax (1.0, maxFreq - minFreq);
    const double denom = (double) juce::jmax (1, bins - 1);

    for (int y = 0; y < outputHeight; ++y)
    {
        const float ratio = outputHeight > 1 ? (float) y / (float) (outputHeight - 1) : 0.0f;
        const float inverted = 1.0f - ratio;

        double targetBin = 0.0;
        if (scaleMode == FrequencyScale::logarithmic)
        {
            const double logPos = logMin + logRange * inverted;
            const double frequency = std::pow (10.0, logPos);
            const double normalised = juce::jlimit (0.0, 1.0, (frequency - minFreq) / freqRange);
            targetBin = normalised * denom;
        }
        else
        {
            targetBin = inverted * denom;
        }

        binRemap[(size_t) y] = (float) juce::jlimit (0.0, denom, targetBin);
    }

    const auto* readPointers = orderedColumns.getArrayOfReadPointers();
    juce::Image::BitmapData pixels (spectrogramImage, juce::Image::BitmapData::writeOnly);

    constexpr float epsilon = 1.0e-6f;
    const float range = juce::jmax (0.01f, maxDb - minDb);

    for (int x = 0; x < visibleColumns; ++x)
    {
        for (int y = 0; y < outputHeight; ++y)
        {
            const int sourceColumn = columnOffset + x;
            const float remap = binRemap[(size_t) y];
            const int lower = (int) std::floor (remap);
            const int upper = juce::jmin (bins - 1, lower + 1);
            const float fraction = remap - (float) lower;

            float value = 0.0f;
            if (bins <= 1)
            {
                value = readPointers[0][sourceColumn];
            }
            else
            {
                const float lowerValue = readPointers[lower][sourceColumn];
                const float upperValue = readPointers[upper][sourceColumn];
                value = lowerValue + fraction * (upperValue - lowerValue);
            }

            float db = juce::Decibels::gainToDecibels (juce::jmax (value, epsilon));
            db = juce::jlimit (minDb, maxDb, db);
            float normalised = (db - minDb) / range;
            normalised = juce::jlimit (0.0f, 1.0f, normalised);

            juce::Colour colour = colourForMagnitude (normalised);
            auto* pixel = reinterpret_cast<juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            pixel->setARGB (255, colour.getRed(), colour.getGreen(), colour.getBlue());
        }
    }

    hasData = true;
}

void SpectrogramMeter::refreshStatusText()
{
    juce::String text;

    if (! hasData)
    {
        text = freezeEnabled ? "Frozen - waiting for new audio" : "Waiting for audio...";
    }
    else
    {
        text << juce::String (visibleSeconds, visibleSeconds < 10.0 ? 1 : 0) << " s span";
        text << "  |  Window " << juce::String (getTargetSpanSeconds(), 0) << " s";
        text << "  |  " << visibleColumns << " frames";
        text << "  |  Floor " << juce::String (minDb, 0) << " dB";
        text << "  |  Glow " << juce::String (intensitySlider.getValue(), 1) << "x";
        text << "  |  " << (scaleBox.getSelectedId() == (int) FrequencyScale::logarithmic ? "Log" : "Linear") << " freq";
        text << "  |  Grid " << (gridEnabled ? "On" : "Off");
        const bool beatActive = beatGridEnabled && shouldDrawBeatGrid();
        text << "  |  Beat " << (beatActive ? "On" : "Off");
        if (freezeEnabled)
            text << "  |  Frozen";
    }

    statusLabel.setText (text, juce::dontSendNotification);
}

void SpectrogramMeter::updateControlColours()
{
    auto setCombo = [this] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::textColourId, theme.text.withAlpha (0.82f));
        box.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.3f));
        box.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.18f));
        box.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
        box.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.9f));
    };

    setCombo (scaleBox);
    setCombo (paletteBox);
    setCombo (timeSpanBox);

    auto setToggle = [this] (juce::ToggleButton& button)
    {
        button.setColour (juce::ToggleButton::textColourId, theme.text.withAlpha (0.75f));
        button.setColour (juce::ToggleButton::tickColourId, theme.secondary.withAlpha (0.85f));
        button.setColour (juce::ToggleButton::tickDisabledColourId, theme.text.withAlpha (0.3f));
    };

    setToggle (freezeButton);
    setToggle (gridButton);
    setToggle (beatGridButton);

    auto setSlider = [this] (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.82f));
        slider.setColour (juce::Slider::thumbColourId, theme.secondary);
        slider.setColour (juce::Slider::trackColourId, theme.secondary.withAlpha (0.4f));
        slider.setColour (juce::Slider::backgroundColourId, theme.background.darker (0.25f));
    };

    setSlider (floorSlider);
    setSlider (intensitySlider);

    auto setLabel = [this] (juce::Label& label)
    {
        label.setColour (juce::Label::textColourId, theme.text.withAlpha (0.7f));
    };

    setLabel (floorLabel);
    setLabel (intensityLabel);

    statusLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.62f));
}

void SpectrogramMeter::rebuildColourLut()
{
    colourLutDirty = false;

    auto gradient = createPaletteGradient();
    const int size = (int) colourLut.size();

    for (int i = 0; i < size; ++i)
    {
        const float position = size > 1 ? (float) i / (float) (size - 1) : 0.0f;
        colourLut[(size_t) i] = gradient.getColourAtPosition (position);
    }
}

juce::ColourGradient SpectrogramMeter::createPaletteGradient() const
{
    juce::ColourGradient gradient;
    gradient.point1 = { 0.0f, 0.0f };
    gradient.point2 = { 1.0f, 0.0f };
    gradient.isRadial = false;

    switch (paletteBox.getSelectedId())
    {
        case 2: // Cosmic blues and magentas
        {
            gradient.addColour (0.0, juce::Colour::fromRGB (8, 18, 54));
            gradient.addColour (0.15, theme.background.brighter (0.12f));
            gradient.addColour (0.35, theme.primary);
            gradient.addColour (0.55, juce::Colour::fromRGB (70, 215, 255));
            gradient.addColour (0.75, theme.tertiary);
            gradient.addColour (0.95, juce::Colour::fromRGB (255, 180, 255));
            gradient.addColour (1.0, juce::Colours::white);
            break;
        }
        case 1: // Inferno inspired
        default:
        {
            gradient.addColour (0.0, juce::Colour::fromRGB (16, 5, 38));
            gradient.addColour (0.18, juce::Colour::fromRGB (92, 10, 99));
            gradient.addColour (0.36, juce::Colour::fromRGB (177, 34, 107));
            gradient.addColour (0.58, juce::Colour::fromRGB (241, 104, 57));
            gradient.addColour (0.78, juce::Colour::fromRGB (253, 191, 37));
            gradient.addColour (1.0, juce::Colour::fromRGB (255, 255, 224));
            break;
        }
    }

    return gradient;
}

juce::Colour SpectrogramMeter::colourForMagnitude (float magnitude)
{
    const int size = (int) colourLut.size();
    if (size <= 0)
        return theme.secondary;

    const float gamma = (float) intensitySlider.getValue();
    const float shaped = std::pow (juce::jlimit (0.0f, 1.0f, magnitude), 1.0f / juce::jmax (0.01f, gamma));
    const int index = juce::jlimit (0, size - 1, (int) std::round (shaped * (float) (size - 1)));
    return colourLut[(size_t) index];
}

void SpectrogramMeter::drawGrid (juce::Graphics& g, juce::Rectangle<float> plotBounds)
{
    g.setColour (theme.outline.withAlpha (0.2f));
    for (double freq : getGridFrequencies())
    {
        const float y = frequencyToY (freq, plotBounds);
        g.drawLine (plotBounds.getX(), y, plotBounds.getRight(), y, freq >= 1000.0 ? 1.0f : 0.6f);
    }
}

void SpectrogramMeter::drawFrequencyScale (juce::Graphics& g,
                                            juce::Rectangle<float> labelBounds,
                                            juce::Rectangle<float> plotBounds)
{
    if (! hasData)
        return;

    g.setColour (theme.text.withAlpha (0.58f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    for (double freq : getGridFrequencies())
    {
        const float y = frequencyToY (freq, plotBounds);
        juce::String label = freq >= 1000.0
                                ? juce::String (freq / 1000.0, freq >= 10000.0 ? 0 : 1) + " kHz"
                                : juce::String ((int) std::round (freq)) + " Hz";

        auto textArea = labelBounds.withHeight (16.0f).withY (y - 8.0f);
        g.drawFittedText (label, textArea.toNearestInt(), juce::Justification::centredRight, 1);
    }
}

void SpectrogramMeter::drawTimeAxis (juce::Graphics& g,
                                     juce::Rectangle<float> axisBounds,
                                     juce::Rectangle<float> plotBounds,
                                     const std::vector<BeatMarker>& beatMarkers,
                                     bool drawUniformGrid)
{
    if (! hasData || visibleColumns <= 0 || visibleSeconds <= 0.0)
        return;

    const float nowX = plotBounds.getRight();
    const double safeSpan = juce::jmax (1.0e-6, visibleSeconds);

    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    if (! beatMarkers.empty())
    {
        for (const auto& marker : beatMarkers)
        {
            if (marker.timeFromNow < 0.0 || marker.timeFromNow > visibleSeconds + secondsPerColumn)
                continue;

            const float ratio = (float) juce::jlimit (0.0, 1.0, marker.timeFromNow / safeSpan);
            const float x = plotBounds.getRight() - plotBounds.getWidth() * ratio;

            const float alpha = marker.isBarStart ? 0.32f : 0.22f;
            g.setColour ((marker.isBarStart ? theme.secondary : theme.text).withAlpha (alpha));
            g.drawLine (x, plotBounds.getY(), x, plotBounds.getBottom(), marker.isBarStart ? 1.2f : 0.7f);

            juce::String label = marker.isBarStart ? juce::String ("Bar ") + juce::String (marker.barNumber)
                                                   : juce::String ("Beat ") + juce::String (marker.beatInBar);
            const double seconds = marker.timeFromNow;
            juce::String timeLabel = seconds < 1.0
                                         ? juce::String ((int) std::round (seconds * 1000.0)) + " ms"
                                         : juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s";
            juce::String combined = label + "  •  " + timeLabel;

            g.setColour (theme.text.withAlpha (marker.isBarStart ? 0.75f : 0.58f));
            auto textArea = axisBounds.withWidth (120.0f).withX (x - 60.0f);
            g.drawFittedText (combined, textArea.toNearestInt(), juce::Justification::centred, 1);
        }

        return;
    }

    const int divisions = drawUniformGrid ? 6 : 0;
    if (divisions <= 0)
    {
        g.setColour (theme.outline.withAlpha (0.32f));
        g.drawLine (nowX, plotBounds.getY(), nowX, plotBounds.getBottom(), 1.0f);

        g.setColour (theme.text.withAlpha (0.5f));
        auto textArea = axisBounds.withWidth (60.0f).withX (nowX - 30.0f);
        g.drawFittedText ("0 s", textArea.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    for (int i = 0; i <= divisions; ++i)
    {
        const float ratio = (float) i / (float) divisions;
        const float x = plotBounds.getRight() - plotBounds.getWidth() * ratio;
        const double seconds = visibleSeconds * ratio;

        g.setColour (theme.outline.withAlpha (i == 0 ? 0.32f : 0.18f));
        g.drawLine (x, plotBounds.getY(), x, plotBounds.getBottom(), i == 0 ? 1.0f : 0.6f);

        g.setColour (theme.text.withAlpha (0.5f));
        juce::String label;
        if (seconds < 1.0)
            label = juce::String ((int) std::round (seconds * 1000.0)) + " ms";
        else
            label = juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s";

        auto textArea = axisBounds.withWidth (60.0f).withX (x - 30.0f);
        g.drawFittedText (label, textArea.toNearestInt(), juce::Justification::centred, 1);
    }
}

float SpectrogramMeter::frequencyToY (double frequency, juce::Rectangle<float> plotBounds) const
{
    const double nyquist = juce::jmax (20.0, sampleRate * 0.5);
    double ratio = 0.0;

    if ((FrequencyScale) scaleBox.getSelectedId() == FrequencyScale::logarithmic)
    {
        const double minFreq = 20.0;
        const double maxFreq = juce::jmax (minFreq * 1.01, nyquist);
        const double clamped = juce::jlimit (minFreq, maxFreq, frequency);
        const double logMin = std::log10 (minFreq);
        const double logMax = std::log10 (maxFreq);
        const double logRange = juce::jmax (1.0e-6, logMax - logMin);
        ratio = (std::log10 (clamped) - logMin) / logRange;
    }
    else
    {
        const double clamped = juce::jlimit (0.0, nyquist, frequency);
        ratio = clamped / juce::jmax (1.0, nyquist);
    }

    const float position = (float) juce::jlimit (0.0, 1.0, ratio);
    return plotBounds.getBottom() - position * plotBounds.getHeight();
}

std::vector<double> SpectrogramMeter::getGridFrequencies() const
{
    const double nyquist = juce::jmax (40.0, sampleRate * 0.5);
    const double marks[] = { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0,
                             300.0, 400.0, 500.0, 700.0, 1000.0, 1500.0, 2000.0, 3000.0,
                             4000.0, 5000.0, 7000.0, 10000.0, 15000.0, 20000.0, 24000.0 };
    std::vector<double> freqs;
    freqs.reserve (std::size (marks));

    for (double mark : marks)
    {
        if (mark <= nyquist)
            freqs.push_back (mark);
    }

    if (freqs.empty())
        freqs.push_back (nyquist);
    else if (freqs.back() < nyquist * 0.98)
        freqs.push_back (nyquist);

    return freqs;
}

double SpectrogramMeter::positiveModulo (double value, double modulus) noexcept
{
    if (modulus <= 0.0)
        return 0.0;

    double result = std::fmod (value, modulus);
    if (result < 0.0)
        result += modulus;
    return result;
}

std::vector<SpectrogramMeter::BeatMarker> SpectrogramMeter::createBeatMarkers (double spanSeconds) const
{
    std::vector<BeatMarker> markers;

    if (! shouldDrawBeatGrid() || spanSeconds <= 0.0)
        return markers;

    const double bpm = transport.bpm;
    if (bpm <= 0.0)
        return markers;

    const double secondsPerBeat = 60.0 / bpm;
    if (! std::isfinite (secondsPerBeat) || secondsPerBeat <= 0.0)
        return markers;

    const int denominator = juce::jmax (1, transport.timeSigDenominator);
    const int beatsPerBar = juce::jmax (1, transport.timeSigNumerator);
    const double beatUnit = 4.0 / (double) denominator;
    const double absoluteBeats = transport.ppqPosition / beatUnit;
    const double barStartBeats = transport.ppqPositionOfLastBarStart / beatUnit;

    if (! std::isfinite (absoluteBeats) || ! std::isfinite (barStartBeats))
        return markers;

    const double barLengthBeats = (double) beatsPerBar;
    if (barLengthBeats <= 0.0)
        return markers;

    double beatsIntoBar = absoluteBeats - barStartBeats;
    beatsIntoBar = positiveModulo (beatsIntoBar, barLengthBeats);

    const double fractionalBeat = beatsIntoBar - std::floor (beatsIntoBar);
    const double timeSinceLastBeat = fractionalBeat * secondsPerBeat;
    const int currentBeatIndex = (int) std::floor (absoluteBeats);
    int beatsAgo = 0;
    const double maxTime = spanSeconds + secondsPerBeat;

    while (beatsAgo < 4096)
    {
        const double offset = timeSinceLastBeat + secondsPerBeat * (double) beatsAgo;
        if (offset > maxTime)
            break;

        if (offset <= spanSeconds + 1.0e-6)
        {
            const int beatIndex = currentBeatIndex - beatsAgo;
            const double beatInBarMod = positiveModulo ((double) beatIndex, (double) beatsPerBar);
            const int beatInBar = (int) beatInBarMod + 1;
            const bool isBarStart = beatInBar == 1;
            const int barIndex = (int) std::floor ((double) beatIndex / (double) beatsPerBar);
            const int barNumber = barIndex + 1;

            markers.push_back ({ offset, beatInBar, barNumber, isBarStart });
        }

        ++beatsAgo;
    }

    return markers;
}

bool SpectrogramMeter::shouldDrawBeatGrid() const noexcept
{
    return transport.hasInfo && transport.isPlaying && ! freezeEnabled;
}

double SpectrogramMeter::getTargetSpanSeconds() const noexcept
{
    return targetSpanSeconds > 0.0 ? targetSpanSeconds : 3.0;
}

int SpectrogramMeter::getVisibleColumnCount (int availableColumns) const noexcept
{
    if (availableColumns <= 0)
        return 0;

    const double desiredSeconds = getTargetSpanSeconds();
    if (desiredSeconds <= 0.0 || secondsPerColumn <= 0.0)
        return availableColumns;

    const int desiredColumns = (int) std::round (desiredSeconds / secondsPerColumn);
    return juce::jlimit (1, availableColumns, desiredColumns);
}
