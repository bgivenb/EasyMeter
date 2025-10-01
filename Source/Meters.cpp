#include "Meters.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <limits>
#include <vector>
#include <numeric>

#include <functional>

namespace
{
double positiveModulo (double value, double modulus) noexcept
{
    if (modulus <= 0.0)
        return 0.0;

    double result = std::fmod (value, modulus);
    if (result < 0.0)
        result += modulus;
    return result;
}

struct BeatGridLine
{
    double timeFromNow = 0.0;
    int beatInBar = 1;
    int barNumber = 1;
    bool isBarStart = false;
};

std::vector<BeatGridLine> createBeatGridLines (const TransportInfo& transport, double spanSeconds)
{
    std::vector<BeatGridLine> lines;

    if (! transport.hasInfo || transport.bpm <= 0.0 || spanSeconds <= 0.0)
        return lines;

    const double secondsPerBeat = 60.0 / transport.bpm;
    if (! std::isfinite (secondsPerBeat) || secondsPerBeat <= 0.0)
        return lines;

    const int denominator = juce::jmax (1, transport.timeSigDenominator);
    const int beatsPerBar = juce::jmax (1, transport.timeSigNumerator);
    const double beatUnit = 4.0 / (double) denominator; // quarter notes per beat
    const double absoluteBeats = transport.ppqPosition / beatUnit;
    const double barStartBeats = transport.ppqPositionOfLastBarStart / beatUnit;

    if (! std::isfinite (absoluteBeats) || ! std::isfinite (barStartBeats))
        return lines;

    const double barLengthBeats = (double) beatsPerBar;
    if (barLengthBeats <= 0.0)
        return lines;

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

            lines.push_back ({ offset, beatInBar, barNumber, isBarStart });
        }

        ++beatsAgo;
    }

    return lines;
}

juce::String beatLabelForLine (const BeatGridLine& line)
{
    if (line.isBarStart)
        return "Bar " + juce::String (line.barNumber);

    return juce::String (line.beatInBar);
}

bool shouldDrawBeatGrid (const TransportInfo& transport, bool freezeActive = false) noexcept
{
    return transport.hasInfo && transport.isPlaying && ! freezeActive;
}

struct StereoMeterLayout
{
    juce::Rectangle<float> scopeBounds;
    juce::Rectangle<float> optionsBackground;
    juce::Rectangle<float> metricsBounds;
    juce::Rectangle<float> controlsBounds;
    juce::Rectangle<float> levelsBounds;
    juce::Rectangle<float> balanceBounds;
    juce::Rectangle<float> historyBounds;
};

StereoMeterLayout computeStereoMeterLayout (juce::Rectangle<float> area) noexcept
{
    StereoMeterLayout layout;

    if (area.isEmpty())
        return layout;

    auto working = area;

    const float historyHeight = juce::jlimit (60.0f, working.getHeight() * 0.26f, 140.0f);
    auto historySection = working.removeFromBottom (historyHeight);
    layout.historyBounds = historySection.reduced (10.0f, 6.0f);

    const float desiredRightWidth = juce::jlimit (220.0f, working.getWidth() * 0.38f, 320.0f);
    auto rightColumn = working.removeFromRight (juce::jmin (working.getWidth(), desiredRightWidth));
    layout.scopeBounds = working.reduced (10.0f, 8.0f);

    rightColumn = rightColumn.reduced (8.0f, 8.0f);
    const float columnHeight = juce::jmax (rightColumn.getHeight(), 1.0f);

    float optionsHeight = columnHeight * 0.5f;
    float balanceHeight = columnHeight * 0.2f;
    float levelsHeight = columnHeight - optionsHeight - balanceHeight;

    const float minOptions = 120.0f;
    const float minLevels = 120.0f;
    const float minBalance = 48.0f;

    if (optionsHeight < minOptions)
        optionsHeight = minOptions;
    if (balanceHeight < minBalance)
        balanceHeight = minBalance;

    levelsHeight = columnHeight - optionsHeight - balanceHeight;
    if (levelsHeight < minLevels)
    {
        const float deficit = minLevels - levelsHeight;
        levelsHeight = minLevels;
        const float reduceOptions = deficit * 0.6f;
        const float reduceBalance = deficit * 0.4f;
        optionsHeight = juce::jmax (minOptions, optionsHeight - reduceOptions);
        balanceHeight = juce::jmax (minBalance, balanceHeight - reduceBalance);
    }

    float total = optionsHeight + levelsHeight + balanceHeight;
    if (total > columnHeight && total > 1.0f)
    {
        const float scale = columnHeight / total;
        optionsHeight *= scale;
        levelsHeight *= scale;
        balanceHeight *= scale;
    }

    auto optionsArea = rightColumn.removeFromTop (optionsHeight);
    auto balanceArea = rightColumn.removeFromBottom (balanceHeight);
    auto levelsArea = rightColumn;

    layout.optionsBackground = optionsArea;
    layout.metricsBounds = optionsArea.removeFromTop (24.0f).reduced (4.0f, 2.0f);
    layout.controlsBounds = optionsArea.reduced (4.0f, 4.0f);

    layout.balanceBounds = balanceArea.reduced (6.0f, 6.0f);
    layout.levelsBounds = levelsArea.reduced (6.0f, 6.0f);

    return layout;
}
} // namespace

MeterComponent::MeterComponent (juce::String titleToUse)
    : title (std::move (titleToUse))
{
}

juce::Rectangle<float> MeterComponent::getFrameBounds() const noexcept
{
    constexpr float outerMargin = 18.0f;
    auto outerBounds = getLocalBounds().toFloat().reduced (outerMargin);
    auto innerBounds = outerBounds.reduced (1.5f);
    return innerBounds.reduced (4.0f);
}

juce::Rectangle<float> MeterComponent::getPanelHeaderBounds() const noexcept
{
    auto headerBounds = getFrameBounds().reduced (14.0f, 16.0f);
    const float headerHeight = juce::jlimit (0.0f, headerSectionHeight, headerBounds.getHeight());
    headerBounds.setHeight (headerHeight);
    return headerBounds;
}

juce::Rectangle<float> MeterComponent::getPanelContentBounds() const noexcept
{
    auto content = getFrameBounds().reduced (14.0f, 16.0f);
    const float headerHeight = juce::jlimit (0.0f, headerSectionHeight, content.getHeight());
    content.removeFromTop (headerHeight);
    content.removeFromTop (6.0f);
    return content;
}

juce::Rectangle<float> MeterComponent::drawPanelFrame (juce::Graphics& g) const
{
    constexpr float cornerRadius = 22.0f;

    auto glowBounds = getFrameBounds();
    auto innerBounds = glowBounds.expanded (4.0f);
    auto outerBounds = innerBounds.expanded (1.5f);

    auto baseGradient = juce::ColourGradient (theme.background.darker (0.38f), outerBounds.getBottomLeft(),
                                              theme.background.darker (0.12f), outerBounds.getTopRight(), false);
    baseGradient.addColour (0.35, theme.background.darker (0.05f));
    baseGradient.addColour (0.9, theme.background);
    g.setGradientFill (baseGradient);
    g.fillRoundedRectangle (outerBounds, cornerRadius + 4.0f);

    juce::ColourGradient innerGradient (theme.background.brighter (0.1f), innerBounds.getTopLeft(),
                                        theme.background.darker (0.18f), innerBounds.getBottomRight(), false);
    innerGradient.addColour (0.5, theme.background.darker (0.06f));
    g.setGradientFill (innerGradient);
    g.fillRoundedRectangle (innerBounds, cornerRadius);

    juce::Path innerOutline;
    innerOutline.addRoundedRectangle (innerBounds, cornerRadius);
    g.setColour (theme.primary.withAlpha (0.16f));
    g.strokePath (innerOutline, juce::PathStrokeType (1.6f));

    juce::ColourGradient glowGradient (theme.primary.withAlpha (0.06f), glowBounds.getTopLeft(),
                                       theme.secondary.withAlpha (0.09f), glowBounds.getBottomRight(), false);
    glowGradient.addColour (0.45, theme.background.withAlpha (0.9f));
    g.setGradientFill (glowGradient);
    g.fillRoundedRectangle (glowBounds, cornerRadius - 4.0f);

    auto sheen = glowBounds.withHeight (glowBounds.getHeight() * 0.35f);
    juce::ColourGradient sheenGradient (theme.background.brighter (0.18f).withAlpha (0.14f), sheen.getTopLeft(),
                                        theme.background.withAlpha (0.0f), sheen.getBottomRight(), false);
    g.setGradientFill (sheenGradient);
    g.fillRoundedRectangle (sheen, cornerRadius - 4.0f);

    return glowBounds.reduced (14.0f, 16.0f);
}

juce::Rectangle<float> MeterComponent::drawPanelHeader (juce::Graphics& g, juce::Rectangle<float> panelBounds) const
{
    auto header = panelBounds;
    const float height = juce::jmin (headerSectionHeight, header.getHeight());
    header.setHeight (height);

    auto background = header.expanded (0.0f, 3.0f);
    juce::ColourGradient gradient (theme.primary.withAlpha (0.18f), background.getTopLeft(),
                                   theme.secondary.withAlpha (0.14f), background.getTopRight(), false);
    gradient.addColour (0.8, theme.background.withAlpha (0.45f));
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (background, 10.0f);

    g.setColour (theme.primary.withAlpha (0.28f));
    g.drawRoundedRectangle (background, 10.0f, 1.1f);

    auto textArea = header.reduced (12.0f, 6.0f);
    g.setColour (theme.text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawFittedText (title.toUpperCase(), textArea.toNearestInt(), juce::Justification::centredLeft, 1);

    auto remainder = panelBounds;
    remainder.removeFromTop (height);
    remainder.removeFromTop (6.0f);
    return remainder;
}

bool MeterComponent::applyTheme (const MeterTheme& newTheme) noexcept
{
    if (theme == newTheme)
        return false;

    theme = newTheme;
    return true;
}

juce::Rectangle<float> MeterComponent::getPlotArea (juce::Rectangle<float> bounds, float headerHeight) const noexcept
{
    bounds.removeFromTop (headerHeight);
    return bounds.reduced (14.0f, 14.0f);
}

WaveformMeter::WaveformMeter()
    : MeterComponent ("Waveform")
{
    auto configureCombo = [this] (juce::ComboBox& box, const std::vector<std::pair<int, juce::String>>& items)
    {
        addAndMakeVisible (box);
        box.setJustificationType (juce::Justification::centredLeft);
        for (const auto& entry : items)
            box.addItem (entry.second, entry.first);
    };

    configureCombo (presetBox,
                    { { 1, "Classic Stereo" },
                      { 2, "Mastering Overlay" },
                      { 3, "Mid/Side Forensics" },
                      { 4, "Mono Utility" } });
    presetBox.onChange = [this]
    {
        handlePresetSelection();
        refreshStatusText();
        repaint();
    };
    presetBox.setSelectedId (1, juce::dontSendNotification);

    configureCombo (layoutBox,
                    { { (int) LayoutMode::dual,     "Dual" },
                      { (int) LayoutMode::overlay,  "Overlay" },
                      { (int) LayoutMode::mono,     "Mono" },
                      { (int) LayoutMode::midSide,  "Mid/Side" } });
    layoutBox.setSelectedId ((int) layoutMode, juce::dontSendNotification);
    layoutBox.onChange = [this]
    {
        layoutMode = static_cast<LayoutMode> (layoutBox.getSelectedId());
        refreshStatusText();
        repaint();
    };

    configureCombo (spanBox,
                    { { (int) SpanMode::full,         "Full" },
                      { (int) SpanMode::threeQuarter, "¾" },
                      { (int) SpanMode::half,         "½" },
                      { (int) SpanMode::quarter,      "¼" } });
    spanBox.setSelectedId ((int) spanMode, juce::dontSendNotification);
    spanBox.onChange = [this]
    {
        spanMode = static_cast<SpanMode> (spanBox.getSelectedId());
        refreshStatusText();
        repaint();
    };

    configureCombo (amplitudeBox,
                    { { (int) AmplitudeMode::natural, "Natural" },
                      { (int) AmplitudeMode::focused,  "Focused" } });
    amplitudeBox.setSelectedId ((int) amplitudeMode, juce::dontSendNotification);
    amplitudeBox.onChange = [this]
    {
        amplitudeMode = static_cast<AmplitudeMode> (amplitudeBox.getSelectedId());
        refreshStatusText();
        repaint();
    };

    auto configureToggle = [this] (juce::ToggleButton& button, bool initial)
    {
        addAndMakeVisible (button);
        button.setToggleState (initial, juce::dontSendNotification);
        button.onClick = [this, &button]
        {
            if (&button == &detailToggle)
                detailedMode = detailToggle.getToggleState();
            else if (&button == &freezeButton)
                freezeEnabled = freezeButton.getToggleState();
            else if (&button == &rmsButton)
                rmsOverlayEnabled = rmsButton.getToggleState();
            else if (&button == &normaliseButton)
                normaliseEnabled = normaliseButton.getToggleState();
            else if (&button == &gridButton)
                gridEnabled = gridButton.getToggleState();

            refreshStatusText();
            repaint();
        };
    };

    configureToggle (detailToggle, detailedMode);
    configureToggle (freezeButton, freezeEnabled);
    configureToggle (rmsButton, rmsOverlayEnabled);
    configureToggle (normaliseButton, normaliseEnabled);
    configureToggle (gridButton, gridEnabled);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    statusLabel.setInterceptsMouseClicks (false, false);
    statusLabel.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (statusLabel);

    handlePresetSelection();
    refreshStatusText();
    updateButtonColours();
}

void WaveformMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    detailedMode = detailToggle.getToggleState();
    freezeEnabled = freezeButton.getToggleState();
    rmsOverlayEnabled = rmsButton.getToggleState();
    normaliseEnabled = normaliseButton.getToggleState();
    gridEnabled = gridButton.getToggleState();
    layoutMode = static_cast<LayoutMode> (layoutBox.getSelectedId() > 0 ? layoutBox.getSelectedId() : (int) layoutMode);
    spanMode = static_cast<SpanMode> (spanBox.getSelectedId() > 0 ? spanBox.getSelectedId() : (int) spanMode);
    amplitudeMode = static_cast<AmplitudeMode> (amplitudeBox.getSelectedId() > 0 ? amplitudeBox.getSelectedId()
                                                                                 : (int) amplitudeMode);
    if (amplitudeMode == AmplitudeMode::expanded)
    {
        amplitudeMode = AmplitudeMode::natural;
        amplitudeBox.setSelectedId ((int) AmplitudeMode::natural, juce::dontSendNotification);
    }

    clipLeft = snapshot.clipLeft;
    clipRight = snapshot.clipRight;
    clipGlowLeft = clipLeft ? 1.0f : clipGlowLeft * 0.86f;
    clipGlowRight = clipRight ? 1.0f : clipGlowRight * 0.86f;
    peakLeftGain = snapshot.peakLeft;
    peakRightGain = snapshot.peakRight;
    peakLeftDb = juce::Decibels::gainToDecibels (peakLeftGain + 1.0e-6f, -60.0f);
    peakRightDb = juce::Decibels::gainToDecibels (peakRightGain + 1.0e-6f, -60.0f);
    rmsLeft = snapshot.leftRms;
    rmsRight = snapshot.rightRms;
    rmsMid = snapshot.midRms;
    rmsSide = snapshot.sideRms;
    waveformSampleRate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : waveformSampleRate;
    samplesPerBucket = snapshot.waveformSamplesPerBucket;
    transport = snapshot.transport;

    if (freezeEnabled && hasData)
    {
        refreshStatusText();
        if (themeChanged)
            updateButtonColours();
        repaint();
        return;
    }

    hasData = false;
    leftPath.clear();
    rightPath.clear();
    monoPath.clear();
    midPath.clear();
    sidePath.clear();
    leftUpperPath.clear();
    rightUpperPath.clear();
    leftLowerPath.clear();
    rightLowerPath.clear();
    leftCentrePath.clear();
    rightCentrePath.clear();
    for (auto* collection : { &leftBandPaths, &rightBandPaths, &monoBandPaths, &midBandPaths, &sideBandPaths })
        for (auto& path : *collection)
            path.clear();
    leftBandPeaks = { 0.0f, 0.0f, 0.0f };
    rightBandPeaks = { 0.0f, 0.0f, 0.0f };
    monoBandPeaks = { 0.0f, 0.0f, 0.0f };
    midBandPeaks = { 0.0f, 0.0f, 0.0f };
    sideBandPeaks = { 0.0f, 0.0f, 0.0f };

    const auto& leftMins = snapshot.waveformLeftMins;
    const auto& leftMaxs = snapshot.waveformLeftMaxs;
    const bool haveRight = snapshot.waveformRightMins.size() == leftMins.size()
                           && snapshot.waveformRightMaxs.size() == leftMaxs.size();
    const auto& rightMins = haveRight ? snapshot.waveformRightMins : leftMins;
    const auto& rightMaxs = haveRight ? snapshot.waveformRightMaxs : leftMaxs;

    if (leftMins.empty() || leftMaxs.size() != leftMins.size())
    {
        waveformResolution = 0;
        waveformSpanSeconds = 0.0;
        detailToggle.setEnabled (false);
        refreshStatusText();
        if (themeChanged)
            updateButtonColours();
        return;
    }

    waveformResolution = (int) leftMins.size();
    const double safeSampleRate = waveformSampleRate > 0.0 ? waveformSampleRate : 48000.0;
    const double totalSpanSeconds = (samplesPerBucket > 0 && waveformResolution > 0 && safeSampleRate > 0.0)
                                        ? (double) samplesPerBucket * (double) waveformResolution / safeSampleRate
                                        : 0.0;

    const auto spanFactor = [this]()
    {
        switch (spanMode)
        {
            case SpanMode::threeQuarter: return 0.75;
            case SpanMode::half:         return 0.5;
            case SpanMode::quarter:      return 0.25;
            case SpanMode::full:         break;
        }
        return 1.0;
    }();

    visibleResolution = juce::jlimit (2, waveformResolution, (int) std::round ((double) waveformResolution * spanFactor));
    visibleOffset = juce::jmax (0, waveformResolution - visibleResolution);
    waveformSpanSeconds = totalSpanSeconds * ((double) visibleResolution / juce::jmax (1, waveformResolution));

    const auto sliceRange = [this] (const std::vector<float>& source)
    {
        std::vector<float> result;
        if ((int) source.size() == waveformResolution && visibleResolution > 0)
            result.assign (source.begin() + visibleOffset, source.begin() + visibleOffset + visibleResolution);
        return result;
    };

    auto leftMinsView = sliceRange (leftMins);
    auto leftMaxsView = sliceRange (leftMaxs);
    auto rightMinsView = sliceRange (rightMins);
    auto rightMaxsView = sliceRange (rightMaxs);

    if (leftMinsView.empty() || leftMaxsView.empty())
    {
        detailToggle.setEnabled (false);
        refreshStatusText();
        if (themeChanged)
            updateButtonColours();
        return;
    }

    visibleResolution = (int) leftMinsView.size();

    const auto visibleSize = static_cast<size_t> (visibleResolution);
    std::vector<float> midMinsView (visibleSize);
    std::vector<float> midMaxsView (visibleSize);
    std::vector<float> sideMinsView (visibleSize);
    std::vector<float> sideMaxsView (visibleSize);
    std::vector<float> monoMinsView (visibleSize);
    std::vector<float> monoMaxsView (visibleSize);

    float globalMax = 1.0e-6f;
    for (int i = 0; i < visibleResolution; ++i)
    {
        const float lMin = leftMinsView[(size_t) i];
        const float lMax = leftMaxsView[(size_t) i];
        const float rMin = rightMinsView.empty() ? lMin : rightMinsView[(size_t) i];
        const float rMax = rightMaxsView.empty() ? lMax : rightMaxsView[(size_t) i];

        globalMax = juce::jmax (globalMax, std::abs (lMin));
        globalMax = juce::jmax (globalMax, std::abs (lMax));
        globalMax = juce::jmax (globalMax, std::abs (rMin));
        globalMax = juce::jmax (globalMax, std::abs (rMax));

        const float midMin = 0.5f * (lMin + rMin);
        const float midMax = 0.5f * (lMax + rMax);
        midMinsView[(size_t) i] = midMin;
        midMaxsView[(size_t) i] = midMax;
        monoMinsView[(size_t) i] = midMin;
        monoMaxsView[(size_t) i] = midMax;
        globalMax = juce::jmax (globalMax, std::abs (midMin));
        globalMax = juce::jmax (globalMax, std::abs (midMax));

        std::array<float, 4> sideCandidates { 0.5f * (lMax - rMin),
                                             0.5f * (lMax - rMax),
                                             0.5f * (lMin - rMin),
                                             0.5f * (lMin - rMax) };
        float sMin = sideCandidates.front();
        float sMax = sideCandidates.front();
        for (float candidate : sideCandidates)
        {
            sMin = juce::jmin (sMin, candidate);
            sMax = juce::jmax (sMax, candidate);
        }
        sideMinsView[(size_t) i] = sMin;
        sideMaxsView[(size_t) i] = sMax;
        globalMax = juce::jmax (globalMax, std::abs (sMin));
        globalMax = juce::jmax (globalMax, std::abs (sMax));
    }

    const float normalisation = normaliseEnabled ? (1.0f / juce::jmax (globalMax, 1.0e-5f)) : 1.0f;
    const float amplitudeScale = 0.45f * getAmplitudeScale() * normalisation;
    currentAmplitudeScale = amplitudeScale;
    const bool smooth = detailedMode;

    buildChannelPaths (leftPath, leftUpperPath, leftLowerPath, leftCentrePath, leftMinsView, leftMaxsView, amplitudeScale, smooth);
    buildChannelPaths (rightPath, rightUpperPath, rightLowerPath, rightCentrePath, rightMinsView, rightMaxsView, amplitudeScale, smooth);

    buildChannelPaths (monoPath, monoUpperPath, monoLowerPath, monoCentrePath, monoMinsView, monoMaxsView, amplitudeScale, smooth);
    buildChannelPaths (midPath, midUpperPath, midLowerPath, midCentrePath, midMinsView, midMaxsView, amplitudeScale, smooth);
    buildChannelPaths (sidePath, sideUpperPath, sideLowerPath, sideCentrePath, sideMinsView, sideMaxsView, amplitudeScale, smooth);

    const std::array<const std::vector<float>*, 3> leftBands { &snapshot.waveformLeftLowBand,
                                                               &snapshot.waveformLeftMidBand,
                                                               &snapshot.waveformLeftHighBand };
    const std::array<const std::vector<float>*, 3> rightBands { &snapshot.waveformRightLowBand,
                                                                &snapshot.waveformRightMidBand,
                                                                &snapshot.waveformRightHighBand };

    std::array<std::vector<float>, 3> leftBandViews;
    std::array<std::vector<float>, 3> rightBandViews;
    std::array<std::vector<float>, 3> monoBandViews;
    std::array<std::vector<float>, 3> midBandViews;
    std::array<std::vector<float>, 3> sideBandViews;
    std::array<float, 3> combinedBandMax { 0.0f, 0.0f, 0.0f };

    for (size_t band = 0; band < leftBands.size(); ++band)
    {
        if (leftBands[band] != nullptr && leftBands[band]->size() == (size_t) waveformResolution)
        {
            leftBandViews[band].assign (leftBands[band]->begin() + visibleOffset,
                                        leftBands[band]->begin() + visibleOffset + visibleResolution);
            if (! leftBandViews[band].empty())
                leftBandPeaks[band] = *std::max_element (leftBandViews[band].begin(), leftBandViews[band].end());
            combinedBandMax[band] = juce::jmax (combinedBandMax[band], leftBandPeaks[band]);
        }
        if (rightBands[band] != nullptr && rightBands[band]->size() == (size_t) waveformResolution)
        {
            rightBandViews[band].assign (rightBands[band]->begin() + visibleOffset,
                                         rightBands[band]->begin() + visibleOffset + visibleResolution);
            if (! rightBandViews[band].empty())
                rightBandPeaks[band] = *std::max_element (rightBandViews[band].begin(), rightBandViews[band].end());
            combinedBandMax[band] = juce::jmax (combinedBandMax[band], rightBandPeaks[band]);
        }

        if (monoBandViews[band].size() != (size_t) visibleResolution)
        {
            monoBandViews[band].resize ((size_t) visibleResolution);
            midBandViews[band].resize ((size_t) visibleResolution);
            sideBandViews[band].resize ((size_t) visibleResolution);
        }

        for (int i = 0; i < visibleResolution; ++i)
        {
            const float l = leftBandViews[band].empty() ? 0.0f : leftBandViews[band][(size_t) i];
            const float r = rightBandViews[band].empty() ? l : rightBandViews[band][(size_t) i];
            const float mono = 0.5f * (l + r);
            const float mid = mono;
            const float side = 0.5f * std::abs (l - r);
            monoBandViews[band][(size_t) i] = mono;
            midBandViews[band][(size_t) i] = mid;
            sideBandViews[band][(size_t) i] = side;
            monoBandPeaks[band] = juce::jmax (monoBandPeaks[band], mono);
            midBandPeaks[band] = juce::jmax (midBandPeaks[band], mid);
            sideBandPeaks[band] = juce::jmax (sideBandPeaks[band], side);
            combinedBandMax[band] = juce::jmax (combinedBandMax[band], juce::jmax (mono, side));
        }

        const float bandNorm = combinedBandMax[band] > 1.0e-6f ? 1.0f / combinedBandMax[band] : 0.0f;
        buildBandPath (leftBandPaths[band], leftBandViews[band], amplitudeScale, bandNorm, smooth);
        buildBandPath (rightBandPaths[band], rightBandViews[band], amplitudeScale, bandNorm, smooth);
        buildBandPath (monoBandPaths[band], monoBandViews[band], amplitudeScale, bandNorm, smooth);
        buildBandPath (midBandPaths[band], midBandViews[band], amplitudeScale, bandNorm, smooth);
        buildBandPath (sideBandPaths[band], sideBandViews[band], amplitudeScale, bandNorm, smooth);
    }

    hasData = true;
    detailToggle.setEnabled (true);
    refreshStatusText();
    if (themeChanged)
        updateButtonColours();
    repaint();
}

void WaveformMeter::handlePresetSelection()
{
    const int preset = presetBox.getSelectedId();

    switch (preset)
    {
        case 1: // Classic Stereo
            layoutBox.setSelectedId ((int) LayoutMode::dual, juce::dontSendNotification);
            spanBox.setSelectedId ((int) SpanMode::full, juce::dontSendNotification);
            amplitudeBox.setSelectedId ((int) AmplitudeMode::natural, juce::dontSendNotification);
            detailToggle.setToggleState (false, juce::dontSendNotification);
            rmsButton.setToggleState (true, juce::dontSendNotification);
            normaliseButton.setToggleState (false, juce::dontSendNotification);
            gridButton.setToggleState (true, juce::dontSendNotification);
            break;
        case 2: // Mastering Overlay
            layoutBox.setSelectedId ((int) LayoutMode::overlay, juce::dontSendNotification);
            spanBox.setSelectedId ((int) SpanMode::threeQuarter, juce::dontSendNotification);
            amplitudeBox.setSelectedId ((int) AmplitudeMode::natural, juce::dontSendNotification);
            detailToggle.setToggleState (true, juce::dontSendNotification);
            rmsButton.setToggleState (true, juce::dontSendNotification);
            normaliseButton.setToggleState (true, juce::dontSendNotification);
            gridButton.setToggleState (true, juce::dontSendNotification);
            break;
        case 3: // Mid/Side Forensics
            layoutBox.setSelectedId ((int) LayoutMode::midSide, juce::dontSendNotification);
            spanBox.setSelectedId ((int) SpanMode::half, juce::dontSendNotification);
            amplitudeBox.setSelectedId ((int) AmplitudeMode::natural, juce::dontSendNotification);
            detailToggle.setToggleState (true, juce::dontSendNotification);
            rmsButton.setToggleState (true, juce::dontSendNotification);
            normaliseButton.setToggleState (true, juce::dontSendNotification);
            gridButton.setToggleState (true, juce::dontSendNotification);
            break;
        case 4: // Mono Utility
            layoutBox.setSelectedId ((int) LayoutMode::mono, juce::dontSendNotification);
            spanBox.setSelectedId ((int) SpanMode::full, juce::dontSendNotification);
            amplitudeBox.setSelectedId ((int) AmplitudeMode::focused, juce::dontSendNotification);
            detailToggle.setToggleState (false, juce::dontSendNotification);
            rmsButton.setToggleState (false, juce::dontSendNotification);
            normaliseButton.setToggleState (false, juce::dontSendNotification);
            gridButton.setToggleState (true, juce::dontSendNotification);
            break;
        default:
            break;
    }

    layoutMode = static_cast<LayoutMode> (layoutBox.getSelectedId() > 0 ? layoutBox.getSelectedId() : (int) layoutMode);
    spanMode = static_cast<SpanMode> (spanBox.getSelectedId() > 0 ? spanBox.getSelectedId() : (int) spanMode);
    amplitudeMode = static_cast<AmplitudeMode> (amplitudeBox.getSelectedId() > 0 ? amplitudeBox.getSelectedId()
                                                                                 : (int) amplitudeMode);
    detailedMode = detailToggle.getToggleState();
    freezeEnabled = freezeButton.getToggleState();
    rmsOverlayEnabled = rmsButton.getToggleState();
    normaliseEnabled = normaliseButton.getToggleState();
    gridEnabled = gridButton.getToggleState();
}

float WaveformMeter::getAmplitudeScale() const noexcept
{
    switch (amplitudeMode)
    {
        case AmplitudeMode::expanded:
        case AmplitudeMode::natural:  return 1.0f;
        case AmplitudeMode::focused:  return 0.8f;
    }

    return 1.0f;
}

float WaveformMeter::getNormalisationGain (const std::vector<float>& mins, const std::vector<float>& maxs) const noexcept
{
    const size_t count = juce::jmin (mins.size(), maxs.size());
    float peak = 1.0e-6f;
    for (size_t i = 0; i < count; ++i)
    {
        peak = juce::jmax (peak, std::abs (mins[i]));
        peak = juce::jmax (peak, std::abs (maxs[i]));
    }
    return 1.0f / peak;
}

void WaveformMeter::buildChannelPaths (juce::Path& fillPath,
                                       juce::Path& upperPath,
                                       juce::Path& lowerPath,
                                       juce::Path& centrePath,
                                       const std::vector<float>& mins,
                                       const std::vector<float>& maxs,
                                       float amplitudeScale,
                                       bool applySmoothing)
{
    fillPath.clear();
    upperPath.clear();
    lowerPath.clear();
    centrePath.clear();

    if (mins.empty() || maxs.size() != mins.size())
        return;

    const int resolution = (int) mins.size();
    const int subdivisions = applySmoothing ? 3 : 1;
    const auto toY = [amplitudeScale] (float value)
    {
        return 0.5f - amplitudeScale * value;
    };

    const auto appendPoint = [&] (float normX, float minValue, float maxValue)
    {
        const float centre = 0.5f * (maxValue + minValue);
        fillPath.lineTo (normX, toY (maxValue));
        upperPath.lineTo (normX, toY (maxValue));
        lowerPath.lineTo (normX, toY (minValue));
        centrePath.lineTo (normX, toY (centre));
    };

    fillPath.startNewSubPath (0.0f, toY (maxs.front()));
    upperPath.startNewSubPath (0.0f, toY (maxs.front()));
    lowerPath.startNewSubPath (0.0f, toY (mins.front()));
    centrePath.startNewSubPath (0.0f, toY (0.5f * (maxs.front() + mins.front())));

    for (int i = 0; i < resolution - 1; ++i)
    {
        const float x0 = (float) i / (float) (resolution - 1);
        const float x1 = (float) (i + 1) / (float) (resolution - 1);
        const float min0 = mins[(size_t) i];
        const float min1 = mins[(size_t) (i + 1)];
        const float max0 = maxs[(size_t) i];
        const float max1 = maxs[(size_t) (i + 1)];

        for (int step = 1; step <= subdivisions; ++step)
        {
            const float t = (float) step / (float) subdivisions;
            const float normX = juce::jmap (t, x0, x1);
            const float minValue = juce::jmap (t, min0, min1);
            const float maxValue = juce::jmap (t, max0, max1);
            appendPoint (normX, minValue, maxValue);
        }
    }

    fillPath.lineTo (1.0f, toY (maxs.back()));
    upperPath.lineTo (1.0f, toY (maxs.back()));
    lowerPath.lineTo (1.0f, toY (mins.back()));
    centrePath.lineTo (1.0f, toY (0.5f * (maxs.back() + mins.back())));

    if (resolution > 1)
    {
        for (int i = resolution - 1; i > 0; --i)
        {
            const float x0 = (float) i / (float) (resolution - 1);
            const float x1 = (float) (i - 1) / (float) (resolution - 1);
            const float min0 = mins[(size_t) i];
            const float min1 = mins[(size_t) (i - 1)];

            for (int step = 1; step <= subdivisions; ++step)
            {
                const float t = (float) step / (float) subdivisions;
                const float normX = juce::jmap (t, x0, x1);
                const float minValue = juce::jmap (t, min0, min1);
                fillPath.lineTo (normX, toY (minValue));
            }
        }
    }

    fillPath.closeSubPath();
}

void WaveformMeter::buildBandPath (juce::Path& dest,
                                   const std::vector<float>& values,
                                   float amplitudeScale,
                                   float normalisation,
                                   bool applySmoothing)
{
    dest.clear();
    if (values.empty() || normalisation <= 0.0f)
        return;

    const int resolution = (int) values.size();
    const int subdivisions = applySmoothing ? 2 : 1;

    const auto toY = [amplitudeScale] (float magnitude, float sign)
    {
        return 0.5f - amplitudeScale * magnitude * sign;
    };

    const auto normaliseValue = [normalisation] (float value)
    {
        return juce::jlimit (0.0f, 1.0f, value * normalisation);
    };

    dest.startNewSubPath (0.0f, toY (normaliseValue (values.front()), 1.0f));

    for (int i = 0; i < resolution - 1; ++i)
    {
        const float x0 = (float) i / (float) (resolution - 1);
        const float x1 = (float) (i + 1) / (float) (resolution - 1);
        const float v0 = normaliseValue (values[(size_t) i]);
        const float v1 = normaliseValue (values[(size_t) (i + 1)]);

        for (int step = 1; step <= subdivisions; ++step)
        {
            const float t = (float) step / (float) subdivisions;
            const float normX = juce::jmap (t, x0, x1);
            const float value = juce::jmap (t, v0, v1);
            dest.lineTo (normX, toY (value, 1.0f));
        }
    }

    dest.lineTo (1.0f, toY (normaliseValue (values.back()), 1.0f));

    for (int i = resolution - 1; i > 0; --i)
    {
        const float x0 = (float) i / (float) (resolution - 1);
        const float x1 = (float) (i - 1) / (float) (resolution - 1);
        const float v0 = normaliseValue (values[(size_t) i]);
        const float v1 = normaliseValue (values[(size_t) (i - 1)]);

        for (int step = 1; step <= subdivisions; ++step)
        {
            const float t = (float) step / (float) subdivisions;
            const float normX = juce::jmap (t, x0, x1);
            const float value = juce::jmap (t, v0, v1);
            dest.lineTo (normX, toY (value, -1.0f));
        }
    }

    dest.closeSubPath();
}

void WaveformMeter::refreshStatusText()
{
    juce::StringArray bits;

    const auto layoutName = [this]() -> juce::String
    {
        switch (layoutMode)
        {
            case LayoutMode::dual:    return "Stereo";
            case LayoutMode::overlay: return "Overlay";
            case LayoutMode::mono:    return "Mono";
            case LayoutMode::midSide: return "Mid/Side";
        }
        return "Stereo";
    }();

    bits.add ("Layout " + layoutName);

    if (waveformSpanSeconds > 0.0)
        bits.add (juce::String::formatted ("Span %0.2fs", waveformSpanSeconds));

    if (samplesPerBucket > 0)
        bits.add (juce::String::formatted ("%d smp/bin", samplesPerBucket));

    if (normaliseEnabled)
        bits.add ("Normalized");

    if (rmsOverlayEnabled)
        bits.add ("RMS");

    if (! gridEnabled)
        bits.add ("Grid Off");

    if (freezeEnabled)
        bits.add ("Frozen");

    statusText = bits.joinIntoString ("  •  ");
    statusLabel.setText (statusText, juce::dontSendNotification);
}


void WaveformMeter::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto area = drawPanelHeader (g, panel);

    auto controlStrip = area.removeFromTop (34.0f);
    if (controlStrip.getHeight() > 4.0f)
    {
        auto strip = controlStrip.reduced (4.0f, 6.0f);
        if (strip.getWidth() > 0.0f && strip.getHeight() > 0.0f)
        {
            g.setColour (theme.background.withAlpha (0.14f));
            g.fillRoundedRectangle (strip, 8.0f);
            g.setColour (theme.outline.withAlpha (0.18f));
            g.drawRoundedRectangle (strip, 8.0f, 1.0f);
        }
    }

    if (! hasData)
    {
        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawFittedText ("Waiting for audio...", area.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    auto workingArea = area;
    const std::array<juce::Colour, 3> bandColours
    {
        juce::Colour (0xff2b8dff),
        juce::Colour (0xfff0a045),
        juce::Colour (0xfff8f8ff)
    };

    if (detailedMode && workingArea.getHeight() > 36.0f)
    {
        auto infoStrip = workingArea.removeFromTop (26.0f).reduced (4.0f, 0.0f);
        juce::String infoText;

        if (waveformSpanSeconds > 0.0)
            infoText << juce::String::formatted ("Span %0.2f s", waveformSpanSeconds);

        if (samplesPerBucket > 0)
        {
            if (! infoText.isEmpty())
                infoText << "  •  ";
            infoText << samplesPerBucket << " smp/bin";
        }

        if (waveformSampleRate > 0.0)
        {
            if (! infoText.isEmpty())
                infoText << "  •  ";
            infoText << juce::String (waveformSampleRate, 0) << " Hz";
        }

        if (transport.hasInfo && transport.bpm > 0.0)
        {
            if (! infoText.isEmpty())
                infoText << "  •  ";
            infoText << juce::String (transport.bpm, 1) << " BPM";
            if (transport.timeSigNumerator > 0 && transport.timeSigDenominator > 0)
                infoText << " (" << transport.timeSigNumerator << '/' << transport.timeSigDenominator << ')';
        }

        juce::Rectangle<float> legendArea;
        if (infoStrip.getWidth() > 140.0f)
            legendArea = infoStrip.removeFromRight (juce::jmin (180.0f, infoStrip.getWidth() * 0.46f));

        if (! infoText.isEmpty())
        {
            g.setColour (theme.text.withAlpha (0.62f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawFittedText (infoText, infoStrip.toNearestInt(), juce::Justification::centredLeft, 1);
        }

        if (! legendArea.isEmpty())
        {
            auto legend = legendArea.reduced (6.0f, 4.0f);
            const float itemWidth = legend.getWidth() / 3.0f;
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            const std::array<juce::String, 3> labels { "LOW", "MID", "HIGH" };
            for (size_t band = 0; band < labels.size(); ++band)
            {
                auto item = legend.removeFromLeft (band == labels.size() - 1 ? legend.getWidth() : itemWidth);
                auto swatch = item.removeFromLeft (14.0f).reduced (2.0f, 2.0f);
                g.setColour (bandColours[band]);
                g.fillRoundedRectangle (swatch, 3.0f);
                g.setColour (theme.text.withAlpha (0.7f));
                g.drawFittedText (labels[band], item.toNearestInt(), juce::Justification::centredLeft, 1);
            }
        }
    }

    auto findNiceInterval = [] (double spanSeconds)
    {
        if (spanSeconds <= 0.0)
            return 0.0;

        const double targetDivisions = 6.0;
        double raw = spanSeconds / targetDivisions;
        if (raw <= 0.0)
            return 0.0;

        const double pow10 = std::pow (10.0, std::floor (std::log10 (raw)));
        const double mantissa = raw / pow10;

        double nice = 1.0;
        if (mantissa < 1.5)
            nice = 1.0;
        else if (mantissa < 3.0)
            nice = 2.0;
        else if (mantissa < 7.0)
            nice = 5.0;
        else
            nice = 10.0;

        return nice * pow10;
    };

    auto drawWave = [&] (const juce::Path& fillPath,
                         const juce::Path& upperPath,
                         const juce::Path& lowerPath,
                         const juce::Path& centrePath,
                         const std::array<juce::Path, 3>& bandShapes,
                         const std::array<float, 3>& bandPeakValues,
                         juce::Rectangle<float> bounds,
                         juce::Colour colour,
                         float clipIntensity,
                         float rmsValue,
                         bool drawTimeLabels)
    {
        auto pathBounds = bounds.reduced (detailedMode ? 8.0f : 2.0f, detailedMode ? 6.0f : 4.0f);

        if (pathBounds.getWidth() <= 0.0f || pathBounds.getHeight() <= 0.0f)
            return;

        if (gridEnabled)
        {
            auto background = pathBounds.expanded (6.0f, 4.0f);
            g.setColour (theme.background.withAlpha (detailedMode ? 0.12f : 0.08f));
            g.fillRoundedRectangle (background, 6.0f);
            g.setColour (theme.outline.withAlpha (0.25f));
            g.drawRoundedRectangle (background, 6.0f, 1.0f);

            const std::array<float, 5> amplitudeMarks { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
            for (float amp : amplitudeMarks)
            {
                const float yNorm = 0.5f - currentAmplitudeScale * amp;
                const float y = pathBounds.getY() + pathBounds.getHeight() * juce::jlimit (0.0f, 1.0f, yNorm);
                const bool zeroLine = std::abs (amp) < 1.0e-4f;
                g.setColour (zeroLine ? theme.text.withAlpha (0.4f) : theme.text.withAlpha (0.18f));
                g.drawLine (pathBounds.getX(), y, pathBounds.getRight(), y, zeroLine ? 1.2f : 0.6f);
            }

            if (waveformSpanSeconds > 0.0)
            {
                bool drewBeatGrid = false;
                if (shouldDrawBeatGrid (transport, freezeEnabled))
                {
                    const auto beatLines = createBeatGridLines (transport, waveformSpanSeconds);
                    if (! beatLines.empty())
                    {
                        drewBeatGrid = true;
                        for (const auto& line : beatLines)
                        {
                            const double timeFromStart = waveformSpanSeconds - line.timeFromNow;
                            if (timeFromStart < 0.0 || timeFromStart > waveformSpanSeconds)
                                continue;

                            const float norm = (float) juce::jlimit (0.0, 1.0, timeFromStart / juce::jmax (1.0e-6, waveformSpanSeconds));
                            const float x = pathBounds.getX() + pathBounds.getWidth() * norm;
                            const float alpha = line.isBarStart ? 0.28f : 0.16f;
                            g.setColour (theme.text.withAlpha (alpha));
                            g.drawLine (x, pathBounds.getY(), x, pathBounds.getBottom(), line.isBarStart ? 1.0f : 0.6f);

                            if (drawTimeLabels)
                            {
                                g.setColour (theme.text.withAlpha (line.isBarStart ? 0.6f : 0.4f));
                                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                                const juce::String label = beatLabelForLine (line);
                                g.drawText (label,
                                            juce::Rectangle<float> (x + 2.0f, pathBounds.getBottom() - 18.0f, 72.0f, 16.0f),
                                            juce::Justification::centredLeft, false);
                            }
                        }
                    }
                }

                if (! drewBeatGrid)
                {
                    const double interval = findNiceInterval (waveformSpanSeconds);
                    if (interval > 0.0)
                    {
                        for (double t = interval; t < waveformSpanSeconds; t += interval)
                        {
                            const float norm = (float) (t / waveformSpanSeconds);
                            const float x = pathBounds.getX() + pathBounds.getWidth() * norm;
                            g.setColour (theme.text.withAlpha (0.16f));
                            g.drawLine (x, pathBounds.getY(), x, pathBounds.getBottom(), 0.6f);

                            if (drawTimeLabels)
                            {
                                g.setColour (theme.text.withAlpha (0.38f));
                                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                                const juce::String label = juce::String::formatted ("%0.2fs", (double) t);
                                g.drawText (label,
                                            juce::Rectangle<float> (x + 2.0f, pathBounds.getBottom() - 18.0f, 60.0f, 16.0f),
                                            juce::Justification::centredLeft, false);
                            }
                        }
                    }
                }
            }
        }

        juce::AffineTransform transform = juce::AffineTransform::scale (pathBounds.getWidth(), pathBounds.getHeight())
                                               .followedBy (juce::AffineTransform::translation (pathBounds.getX(), pathBounds.getY()));

        const float baseAlpha = colour.getFloatAlpha();
        auto scaledFill = fillPath;
        scaledFill.applyTransform (transform);
        g.setColour (colour.withAlpha ((detailedMode ? 0.26f : 0.35f) * baseAlpha));
        g.fillPath (scaledFill);

        if (detailedMode)
        {
            auto scaledUpper = upperPath;
            auto scaledLower = lowerPath;
            auto scaledCentre = centrePath;
            scaledUpper.applyTransform (transform);
            scaledLower.applyTransform (transform);
            scaledCentre.applyTransform (transform);

            g.setColour (colour.withAlpha (0.95f * baseAlpha));
            g.strokePath (scaledUpper, juce::PathStrokeType (1.35f));
            g.strokePath (scaledLower, juce::PathStrokeType (1.15f));
            g.setColour (colour.brighter (0.35f).withAlpha (0.85f * baseAlpha));
            g.strokePath (scaledCentre, juce::PathStrokeType (0.9f));
        }
        else
        {
            g.setColour (colour.withAlpha (0.9f * baseAlpha));
            g.strokePath (scaledFill, juce::PathStrokeType (1.6f));
        }

        if (detailedMode)
        {
            for (size_t band = 0; band < bandShapes.size(); ++band)
            {
                if (bandShapes[band].isEmpty())
                    continue;

                auto bandPath = bandShapes[band];
                bandPath.applyTransform (transform);
                const float strength = std::sqrt (juce::jlimit (0.0f, 1.0f, bandPeakValues[band]));
                const float alpha = juce::jlimit (0.18f, 0.55f, 0.22f + strength * 0.45f);
                g.setColour (bandColours[band].withAlpha (alpha));
                g.fillPath (bandPath);
                g.setColour (bandColours[band].withAlpha (alpha * 0.85f));
                g.strokePath (bandPath, juce::PathStrokeType (0.6f));
            }
        }

        if (rmsOverlayEnabled && rmsValue > 1.0e-4f)
        {
            const float yNorm = 0.5f - currentAmplitudeScale * rmsValue;
            const float y = pathBounds.getY() + pathBounds.getHeight() * juce::jlimit (0.0f, 1.0f, yNorm);
            g.setColour (colour.brighter (0.4f).withAlpha (0.8f));
            g.drawLine (pathBounds.getX(), y, pathBounds.getRight(), y, detailedMode ? 1.3f : 1.0f);
        }

        if (clipIntensity > 0.02f)
        {
            auto outline = pathBounds.expanded (4.0f, 3.0f);
            g.setColour (theme.warning.withAlpha (0.18f * juce::jlimit (0.2f, 1.0f, clipIntensity)));
            g.drawRoundedRectangle (outline, 6.0f, juce::jmax (1.1f, clipIntensity * 2.0f));
        }
    };

    auto drawOverlay = [&] (juce::Rectangle<float> bounds,
                            const juce::String& label,
                            float peakDb,
                            float rmsValue,
                            float clipIntensity)
    {
        auto header = bounds.removeFromTop (18.0f);
        g.setColour (theme.text.withAlpha (0.78f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (label, header.toNearestInt(), juce::Justification::centredLeft, false);

        auto infoRow = bounds.removeFromTop (18.0f);
        g.setColour (theme.text.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (juce::String::formatted ("Peak %0.1f dB", peakDb),
                    infoRow.removeFromLeft (infoRow.getWidth() * 0.6f).toNearestInt(),
                    juce::Justification::centredLeft, false);

        if (rmsOverlayEnabled)
        {
            const float rmsDb = juce::Decibels::gainToDecibels (juce::jmax (rmsValue, 1.0e-6f), -80.0f);
            g.drawText (juce::String::formatted ("RMS %0.1f dB", rmsDb), infoRow.toNearestInt(),
                        juce::Justification::centredRight, false);
        }

        if (clipIntensity > 0.05f)
        {
            auto badge = bounds.removeFromTop (18.0f).withWidth (60.0f).reduced (2.0f);
            g.setColour (theme.warning.withAlpha (juce::jlimit (0.35f, 0.9f, 0.35f + clipIntensity * 0.5f)));
            g.fillRoundedRectangle (badge, 6.0f);
            g.setColour (theme.background.brighter (0.4f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawFittedText ("CLIP", badge.toNearestInt(), juce::Justification::centred, 1);
        }
    };

    const float verticalGap = detailedMode ? 6.0f : 4.0f;
    const float overlayMargin = detailedMode ? 10.0f : 6.0f;

    const float peakMonoDb = juce::Decibels::gainToDecibels (0.5f * (peakLeftGain + peakRightGain) + 1.0e-6f, -60.0f);
    const float peakSideDb = juce::Decibels::gainToDecibels (0.5f * std::abs (peakLeftGain - peakRightGain) + 1.0e-6f, -60.0f);

    switch (layoutMode)
    {
        case LayoutMode::dual:
        {
            auto topBounds = workingArea.removeFromTop (workingArea.getHeight() * 0.5f).reduced (0.0f, verticalGap);
            auto bottomBounds = workingArea.reduced (0.0f, verticalGap);

            drawWave (leftPath, leftUpperPath, leftLowerPath, leftCentrePath, leftBandPaths, leftBandPeaks,
                      topBounds, theme.primary, clipGlowLeft, rmsLeft, true);
            drawWave (rightPath, rightUpperPath, rightLowerPath, rightCentrePath, rightBandPaths, rightBandPeaks,
                      bottomBounds, theme.secondary, clipGlowRight, rmsRight, false);

            drawOverlay (topBounds.reduced (overlayMargin, overlayMargin * 0.6f), "Left", peakLeftDb, rmsLeft, clipGlowLeft);
            drawOverlay (bottomBounds.reduced (overlayMargin, overlayMargin * 0.6f), "Right", peakRightDb, rmsRight, clipGlowRight);
            break;
        }

        case LayoutMode::overlay:
        {
            auto overlayBounds = workingArea.reduced (0.0f, verticalGap);

            drawWave (leftPath, leftUpperPath, leftLowerPath, leftCentrePath, leftBandPaths, leftBandPeaks,
                      overlayBounds, theme.primary.withAlpha (0.9f), clipGlowLeft, rmsLeft, true);
            drawWave (rightPath, rightUpperPath, rightLowerPath, rightCentrePath, rightBandPaths, rightBandPeaks,
                      overlayBounds, theme.secondary.withAlpha (0.85f), clipGlowRight, rmsRight, false);

            auto infoArea = overlayBounds.reduced (overlayMargin, overlayMargin);
            auto infoRow = infoArea.removeFromTop (28.0f);
            auto leftInfo = infoRow.removeFromLeft (infoRow.getWidth() * 0.5f);
            auto rightInfo = infoRow;
            drawOverlay (leftInfo, "Left", peakLeftDb, rmsLeft, clipGlowLeft);
            drawOverlay (rightInfo, "Right", peakRightDb, rmsRight, clipGlowRight);
            break;
        }

        case LayoutMode::mono:
        {
            auto monoBounds = workingArea.reduced (0.0f, verticalGap);

            const float clipMono = juce::jmax (clipGlowLeft, clipGlowRight);
            drawWave (monoPath, monoUpperPath, monoLowerPath, monoCentrePath, monoBandPaths, monoBandPeaks,
                      monoBounds, theme.primary.brighter (0.1f), clipMono, rmsMid, true);

            drawOverlay (monoBounds.reduced (overlayMargin, overlayMargin * 0.6f), "Mono", peakMonoDb, rmsMid, clipMono);
            break;
        }

        case LayoutMode::midSide:
        {
            auto midBounds = workingArea.removeFromTop (workingArea.getHeight() * 0.54f).reduced (0.0f, verticalGap);
            auto sideBounds = workingArea.reduced (0.0f, verticalGap);

            const float clipMid = juce::jmax (clipGlowLeft, clipGlowRight);
            const float clipSide = clipMid * 0.9f;

            drawWave (midPath, midUpperPath, midLowerPath, midCentrePath, midBandPaths, midBandPeaks,
                      midBounds, theme.primary, clipMid, rmsMid, true);
            drawWave (sidePath, sideUpperPath, sideLowerPath, sideCentrePath, sideBandPaths, sideBandPeaks,
                      sideBounds, theme.warning, clipSide, rmsSide, false);

            drawOverlay (midBounds.reduced (overlayMargin, overlayMargin * 0.6f), "Mid", peakMonoDb, rmsMid, clipMid);
            drawOverlay (sideBounds.reduced (overlayMargin, overlayMargin * 0.6f), "Side", peakSideDb, rmsSide, clipSide);
            break;
        }
    }
}

void WaveformMeter::resized()
{
    auto getTextWidth = [] (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement arrangement;
        arrangement.addLineOfText (font, text, 0.0f, 0.0f);
        return arrangement.getBoundingBox (0, -1, true).getWidth();
    };

    auto header = getPanelHeaderBounds().toNearestInt();
    if (! header.isEmpty())
    {
        auto inner = header.reduced (6, 4);
        const int spacing = 6;

        std::array<juce::ToggleButton*, 5> toggles { &detailToggle, &freezeButton, &rmsButton, &normaliseButton, &gridButton };
        const juce::Font toggleFont (juce::FontOptions (13.0f));
        int widestToggle = 0;
        for (auto* toggle : toggles)
        {
            const auto width = getTextWidth (toggleFont, toggle->getButtonText());
            widestToggle = juce::jmax (widestToggle, (int) std::ceil (width) + 18);
        }

        const int toggleCount = (int) toggles.size();
        const int minToggleWidth = juce::jmax (widestToggle, 76);
        const int desiredToggleRow = toggleCount > 0
                                        ? toggleCount * minToggleWidth + spacing * (toggleCount - 1)
                                        : inner.getWidth();
        const int toggleRowWidth = juce::jmin (inner.getWidth(), juce::jmax (desiredToggleRow, 280));
        auto toggleRow = inner.removeFromRight (toggleRowWidth);
        const int toggleWidth = toggleCount > 0
                                    ? juce::jmax (minToggleWidth,
                                                  (toggleRow.getWidth() - spacing * (toggleCount - 1)) / toggleCount)
                                    : toggleRow.getWidth();

        auto setToggleBounds = [&] (juce::ToggleButton& button)
        {
            const int width = juce::jmin (toggleWidth, toggleRow.getWidth());
            auto bounds = toggleRow.removeFromLeft (width);
            button.setBounds (bounds.reduced (4, 2));
            if (toggleRow.getWidth() > 0)
                toggleRow.removeFromLeft (spacing);
        };

        for (auto* toggle : toggles)
            setToggleBounds (*toggle);

        auto labelArea = inner;
        const juce::Font titleFont (juce::FontOptions (15.0f, juce::Font::bold));
        const int reservedForTitle = (int) std::ceil (getTextWidth (titleFont, title.toUpperCase())) + 18;
        labelArea.removeFromLeft (reservedForTitle);

        if (labelArea.getWidth() > 0)
            statusLabel.setBounds (labelArea);
        else
            statusLabel.setBounds ({});
    }

    auto content = getPanelContentBounds().toNearestInt();
    auto controlRow = content.removeFromTop (38);
    const int comboSpacing = 6;
    std::array<juce::ComboBox*, 4> combos { &presetBox, &layoutBox, &spanBox, &amplitudeBox };
    const juce::Font comboFont (juce::FontOptions (13.0f));
    int widestComboText = 0;
    for (auto* box : combos)
    {
        int longestItem = 0;
        for (int i = 0; i < box->getNumItems(); ++i)
        {
            const auto width = getTextWidth (comboFont, box->getItemText (i));
            longestItem = juce::jmax (longestItem, (int) std::ceil (width));
        }

        widestComboText = juce::jmax (widestComboText, longestItem);
    }

    const int minComboWidth = juce::jmax (widestComboText + 28, 104);
    const int comboCount = (int) combos.size();
    const int comboWidth = comboCount > 0
                               ? juce::jmax (minComboWidth,
                                             (controlRow.getWidth() - comboSpacing * (comboCount - 1)) / comboCount)
                               : controlRow.getWidth();

    auto setComboBounds = [&] (juce::ComboBox& box)
    {
        const int width = juce::jmin (comboWidth, controlRow.getWidth());
        auto bounds = controlRow.removeFromLeft (width);
        box.setBounds (bounds.reduced (0, 2));
        if (controlRow.getWidth() > 0)
            controlRow.removeFromLeft (comboSpacing);
    };

    for (auto* box : combos)
        setComboBounds (*box);
}

void WaveformMeter::updateButtonColours()
{
    auto setToggle = [this] (juce::ToggleButton& button)
    {
        const bool enabled = button.isEnabled();
        const float alpha = enabled ? 0.78f : 0.4f;
        button.setColour (juce::ToggleButton::textColourId, theme.text.withAlpha (alpha));
        button.setColour (juce::ToggleButton::tickColourId, theme.secondary.withAlpha (0.85f));
        button.setColour (juce::ToggleButton::tickDisabledColourId, theme.text.withAlpha (0.3f));
    };

    setToggle (detailToggle);
    setToggle (freezeButton);
    setToggle (rmsButton);
    setToggle (normaliseButton);
    setToggle (gridButton);

    auto setCombo = [this] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::textColourId, theme.text.withAlpha (0.82f));
        box.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.28f));
        box.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.2f));
        box.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
        box.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.9f));
    };

    setCombo (presetBox);
    setCombo (layoutBox);
    setCombo (spanBox);
    setCombo (amplitudeBox);

    statusLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.7f));
}

InfoPanel::InfoPanel()
    : MeterComponent ("Info")
{
    headlineLabel.setText ("EasyMeter", juce::dontSendNotification);
    headlineLabel.setJustificationType (juce::Justification::centredLeft);
    headlineLabel.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
    addAndMakeVisible (headlineLabel);

    taglineLabel.setText ("A minimal meter suite for fast mix decisions.", juce::dontSendNotification);
    taglineLabel.setJustificationType (juce::Justification::centredLeft);
    taglineLabel.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::italic)));
    addAndMakeVisible (taglineLabel);

    loveLabel.setText ("This plugin is created by Given Peace", juce::dontSendNotification);
    loveLabel.setJustificationType (juce::Justification::centredLeft);
    loveLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    addAndMakeVisible (loveLabel);

    connectLabel.setText ("Connect with Given Peace", juce::dontSendNotification);
    connectLabel.setJustificationType (juce::Justification::centredLeft);
    connectLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    addAndMakeVisible (connectLabel);

    auto configureLink = [this] (juce::HyperlinkButton& button, const juce::String& text, const juce::String& url)
    {
        button.setButtonText (text);
        button.setURL (juce::URL (url));
        button.setJustificationType (juce::Justification::centredLeft);
        button.setFont (juce::Font (juce::FontOptions (15.0f)), false);
        button.setTooltip (url);
        addAndMakeVisible (button);
    };

    configureLink (websiteButton, "www.givenpeace.com", "https://www.givenpeace.com");
    configureLink (instagramButton, "instagram.com/given_peace_", "https://instagram.com/given_peace_");
    configureLink (spotifyButton, "Spotify - Given Peace", "https://open.spotify.com/artist/7jdmctUwLw7Z2z7Z7jU6o7");
    configureLink (soundcloudButton, "soundcloud.com/givenpeace", "https://soundcloud.com/givenpeace");

    updateColours();
}

void InfoPanel::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    juce::ignoreUnused (snapshot);

    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
    {
        updateColours();
        repaint();
    }
}

void InfoPanel::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto content = drawPanelHeader (g, panel);

    auto infoArea = content.reduced (8.0f);
    if (! infoArea.isEmpty())
    {
        g.setColour (theme.background.withAlpha (0.18f));
        g.fillRoundedRectangle (infoArea, 16.0f);
        g.setColour (theme.primary.withAlpha (0.26f));
        g.drawRoundedRectangle (infoArea, 16.0f, 1.4f);

        if (infoArea.getWidth() >= 520.0f)
        {
            const float dividerX = infoArea.getX() + (infoArea.getWidth() * 0.5f);
            g.setColour (theme.outline.withAlpha (0.18f));
            g.drawLine (dividerX,
                        infoArea.getY() + 16.0f,
                        dividerX,
                        infoArea.getBottom() - 16.0f,
                        1.1f);
        }
    }
}

void InfoPanel::resized()
{
    auto area = getPanelContentBounds().toNearestInt().reduced (18, 18);

    if (area.getHeight() <= 0)
        return;

    auto header = area.removeFromTop (72);
    headlineLabel.setBounds (header.removeFromTop (42));

    if (! header.isEmpty())
    {
        header.removeFromTop (4);
        taglineLabel.setBounds (header.removeFromTop (juce::jmin (24, header.getHeight())));
    }

    if (header.isEmpty())
        taglineLabel.setBounds ({ });

    area.removeFromTop (12);

    auto layoutInfo = [this] (juce::Rectangle<int> column)
    {
        if (column.getHeight() <= 0)
            return;

        loveLabel.setBounds (column.removeFromTop (24));
    };

    auto layoutLinks = [this] (juce::Rectangle<int> column, bool multiColumn)
    {
        if (column.getHeight() <= 0)
            return;

        connectLabel.setBounds (column.removeFromTop (28));
        column.removeFromTop (6);

        const int linkHeight = 32;
        const int spacing = 6;

        if (multiColumn)
        {
            const int columnSpacing = 12;
            auto leftColumn = column.removeFromLeft ((column.getWidth() - columnSpacing) / 2);
            column.removeFromLeft (columnSpacing);
            auto rightColumn = column;

            const auto placeLink = [] (juce::Rectangle<int>& target, juce::Component& component, int linkHeightValue, int spacingValue)
            {
                auto bounds = target.removeFromTop (linkHeightValue);
                component.setBounds (bounds);
                target.removeFromTop (spacingValue);
            };

            placeLink (leftColumn, websiteButton, linkHeight, spacing);
            placeLink (leftColumn, instagramButton, linkHeight, spacing);

            placeLink (rightColumn, spotifyButton, linkHeight, spacing);
            placeLink (rightColumn, soundcloudButton, linkHeight, spacing);
        }
        else
        {
            const auto placeLink = [&column] (juce::Component& component, int linkHeightValue, int spacingValue)
            {
                auto bounds = column.removeFromTop (linkHeightValue);
                component.setBounds (bounds);
                column.removeFromTop (spacingValue);
            };

            placeLink (websiteButton, linkHeight, spacing);
            placeLink (instagramButton, linkHeight, spacing);
            placeLink (spotifyButton, linkHeight, spacing);
            placeLink (soundcloudButton, linkHeight, spacing);
        }
    };

    const bool multiColumn = area.getWidth() >= 520;

    if (multiColumn)
    {
        const int columnSpacing = 20;
        auto left = area.removeFromLeft ((area.getWidth() - columnSpacing) / 2).reduced (0, 4);
        area.removeFromLeft (columnSpacing);
        auto right = area.reduced (0, 4);

        layoutInfo (left);
        layoutLinks (right, true);
    }
    else
    {
        auto infoHeight = juce::jmax (area.getHeight() / 2, 120);
        auto infoArea = area.removeFromTop (infoHeight).reduced (0, 4);
        layoutInfo (infoArea);

        area.removeFromTop (12);
        layoutLinks (area.reduced (0, 4), false);
    }
}

void InfoPanel::updateColours()
{
    const auto baseText = theme.text.isTransparent() ? juce::Colours::white : theme.text;
    headlineLabel.setColour (juce::Label::textColourId, baseText);
    taglineLabel.setColour (juce::Label::textColourId, baseText.withAlpha (0.9f));
    loveLabel.setColour (juce::Label::textColourId, baseText.withAlpha (0.82f));
    connectLabel.setColour (juce::Label::textColourId, baseText.withAlpha (0.88f));

    const auto defaultLink = baseText.brighter (0.35f);
    const auto linkColour = theme.secondary.isTransparent() ? defaultLink : theme.secondary.withAlpha (0.92f);
    const auto hoverColour = theme.tertiary.isTransparent() ? defaultLink.brighter (0.25f)
                                                            : theme.tertiary.withAlpha (0.95f);

    auto setLinkColours = [linkColour, hoverColour] (juce::HyperlinkButton& button)
    {
        button.onStateChange = [linkColour, hoverColour, &button]
        {
            const auto colour = button.isOver() ? hoverColour : linkColour;
            button.setColour (juce::HyperlinkButton::textColourId, colour);
        };

        button.onStateChange();
    };

    setLinkColours (websiteButton);
    setLinkColours (instagramButton);
    setLinkColours (spotifyButton);
    setLinkColours (soundcloudButton);
}

SpectrumMeter::SpectrumMeter()
    : MeterComponent ("Spectrum")
{
    auto configureCombo = [this] (juce::ComboBox& box, const std::vector<std::pair<int, juce::String>>& items)
    {
        addAndMakeVisible (box);
        box.setJustificationType (juce::Justification::centredLeft);
        for (const auto& entry : items)
            box.addItem (entry.second, entry.first);
    };

    configureCombo (scaleBox,
                    { { (int) Scale::linear,      "Linear" },
                      { (int) Scale::logarithmic, "Log" },
                      { (int) Scale::mel,         "Mel" } });
    scaleBox.setSelectedId ((int) scale, juce::dontSendNotification);
    scaleBox.onChange = [this]
    {
        scale = static_cast<Scale> (scaleBox.getSelectedId());
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    configureCombo (modeBox,
                    { { (int) DisplayMode::line,       "Line" },
                      { (int) DisplayMode::filledLine, "Filled" },
                      { (int) DisplayMode::bars,       "Bars" },
                      { (int) DisplayMode::overlay,    "Overlay" } });
    modeBox.setSelectedId ((int) displayMode, juce::dontSendNotification);
    modeBox.onChange = [this]
    {
        displayMode = static_cast<DisplayMode> (modeBox.getSelectedId());
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    configureCombo (smoothingBox,
                    { { 0, "Off" },
                      { 1, "Gentle" },
                      { 2, "Heavy" } });
    smoothingBox.setSelectedId (1, juce::dontSendNotification);
    smoothingBox.onChange = [this]
    {
        applyProcessing();
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    addAndMakeVisible (gridButton);
    gridButton.setButtonText ("Grid");
    gridButton.setToggleState (true, juce::dontSendNotification);
    gridButton.onClick = [this] { repaint(); };

    addAndMakeVisible (legendButton);
    legendButton.setButtonText ("Legend");
    legendButton.setToggleState (legendVisible, juce::dontSendNotification);
    legendButton.onClick = [this]
    {
        legendVisible = legendButton.getToggleState();
        repaint();
    };

    addAndMakeVisible (peakHoldButton);
    peakHoldButton.setButtonText ("Peak Hold");
    peakHoldButton.setToggleState (false, juce::dontSendNotification);
    peakHoldButton.onClick = [this]
    {
        if (! peakHoldButton.getToggleState())
            peakHoldBands.clear();
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    auto configureSlider = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
    };

    configureSlider (tiltSlider, tiltLabel, "Tilt");
    tiltSlider.setRange (-12.0, 12.0, 0.1);
    tiltSlider.setValue (tiltDbPerOct, juce::dontSendNotification);
    tiltSlider.onValueChange = [this]
    {
        tiltDbPerOct = (float) tiltSlider.getValue();
        applyProcessing();
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    configureSlider (floorSlider, floorLabel, "Floor");
    floorSlider.setRange (-160.0, -20.0, 0.5);
    floorSlider.setValue (noiseFloorDb, juce::dontSendNotification);
    floorSlider.onValueChange = [this]
    {
        noiseFloorDb = (float) floorSlider.getValue();
        applyProcessing();
        rebuildPaths();
        updateLegendText();
        repaint();
    };

    configureSlider (decaySlider, decayLabel, "Hold Decay");
    decaySlider.setRange (50.0, 2000.0, 10.0);
    decaySlider.setValue (600.0, juce::dontSendNotification);
    decaySlider.onValueChange = [this]
    {
        decayPerSecondDb = (float) (600.0 / juce::jmax (50.0, decaySlider.getValue()));
    };

    updateControlColours();
    updateLegendText();
}

void SpectrumMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
        updateControlColours();

    bands = snapshot.spectrum;
    sampleRate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : sampleRate;
    hasData = ! bands.empty();

    if (! hasData)
    {
        processedBands.clear();
        peakHoldBands.clear();
        spectrumPath.clear();
        overlayPath.clear();
        legendText.clear();
        repaint();
        return;
    }

    applyProcessing();
    rebuildPaths();
    updateLegendText();
    repaint();
}

void SpectrumMeter::resized()
{
    auto content = getPanelContentBounds().toNearestInt();

    auto topRow = content.removeFromTop (34);
    const int spacing = 6;

    auto setComboBounds = [&] (juce::ComboBox& box, int width)
    {
        auto bounds = topRow.removeFromLeft (width);
        box.setBounds (bounds.reduced (0, 2));
        topRow.removeFromLeft (spacing);
    };

    const int comboWidth = juce::jmax (100, (topRow.getWidth() - spacing * 5) / 6);
    setComboBounds (scaleBox, comboWidth);
    setComboBounds (modeBox, comboWidth);
    setComboBounds (smoothingBox, comboWidth);

    auto setToggleBounds = [&] (juce::ToggleButton& button, int width)
    {
        auto bounds = topRow.removeFromLeft (width);
        button.setBounds (bounds.reduced (4, 2));
        topRow.removeFromLeft (spacing);
    };

    const int toggleWidth = juce::jmax (90, (topRow.getWidth() - spacing * 2) / 3);
    setToggleBounds (gridButton, toggleWidth);
    setToggleBounds (legendButton, toggleWidth);
    setToggleBounds (peakHoldButton, toggleWidth);

    auto sliderRow = content.removeFromTop (42);
    const int labelWidth = 64;
    const int sliderSpacing = 12;
    const int sliderWidth = juce::jmax (120, (sliderRow.getWidth() - (labelWidth + sliderSpacing) * 3) / 3);

    auto setSliderBounds = [&] (juce::Slider& slider, juce::Label& label)
    {
        auto bounds = sliderRow.removeFromLeft (sliderWidth + labelWidth);
        label.setBounds (bounds.removeFromLeft (labelWidth));
        slider.setBounds (bounds);
        sliderRow.removeFromLeft (sliderSpacing);
    };

    setSliderBounds (tiltSlider, tiltLabel);
    setSliderBounds (floorSlider, floorLabel);
    setSliderBounds (decaySlider, decayLabel);
}

void SpectrumMeter::updateControlColours()
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
    setCombo (modeBox);
    setCombo (smoothingBox);

    auto setToggle = [this] (juce::ToggleButton& button)
    {
        button.setColour (juce::ToggleButton::textColourId, theme.text.withAlpha (0.75f));
        button.setColour (juce::ToggleButton::tickColourId, theme.secondary.withAlpha (0.85f));
        button.setColour (juce::ToggleButton::tickDisabledColourId, theme.text.withAlpha (0.3f));
    };

    setToggle (gridButton);
    setToggle (legendButton);
    setToggle (peakHoldButton);

    auto setSlider = [this] (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.82f));
        slider.setColour (juce::Slider::thumbColourId, theme.secondary);
        slider.setColour (juce::Slider::trackColourId, theme.secondary.withAlpha (0.4f));
    };

    setSlider (tiltSlider);
    setSlider (floorSlider);
    setSlider (decaySlider);

    auto setLabel = [this] (juce::Label& label)
    {
        label.setColour (juce::Label::textColourId, theme.text.withAlpha (0.7f));
    };

    setLabel (tiltLabel);
    setLabel (floorLabel);
    setLabel (decayLabel);
}

void SpectrumMeter::applyProcessing()
{
    const int bins = (int) bands.size();
    if (bins <= 0)
    {
        processedBands.clear();
        return;
    }

    processedBands.resize ((size_t) bins);
    if (frequencyAxis.size() != (size_t) bins)
        frequencyAxis.resize ((size_t) bins);

    const double nyquist = sampleRate * 0.5;
    for (int i = 0; i < bins; ++i)
        frequencyAxis[(size_t) i] = (float) (nyquist * (double) i / juce::jmax (1, bins - 1));

    std::vector<float> dbValues ((size_t) bins);
    for (int i = 0; i < bins; ++i)
    {
        const float magnitude = bands[(size_t) i];
        float db = juce::Decibels::gainToDecibels (magnitude + 1.0e-9f, noiseFloorDb);
        dbValues[(size_t) i] = db;
    }

    const float smoothingAmount = getSmoothingAmount();
    if (smoothingAmount > 0.0f)
    {
        std::vector<float> smoothed ((size_t) bins);
        const int window = juce::jlimit (1, 9, (int) std::round (smoothingAmount * 6.0f) * 2 + 1);
        const int half = window / 2;
        for (int i = 0; i < bins; ++i)
        {
            float sum = 0.0f;
            int count = 0;
            for (int j = -half; j <= half; ++j)
            {
                const int idx = juce::jlimit (0, bins - 1, i + j);
                sum += dbValues[(size_t) idx];
                ++count;
            }
            smoothed[(size_t) i] = sum / (float) juce::jmax (1, count);
        }
        dbValues.swap (smoothed);
    }

    for (int i = 0; i < bins; ++i)
    {
        const float freq = juce::jmax (frequencyAxis[(size_t) i], 1.0f);
        const float octaves = std::log2 (freq / 1000.0f);
        dbValues[(size_t) i] += tiltDbPerOct * octaves;
        dbValues[(size_t) i] = juce::jmax (noiseFloorDb, dbValues[(size_t) i]);
    }

    const float range = -noiseFloorDb;
    for (int i = 0; i < bins; ++i)
    {
        const float normalised = juce::jlimit (0.0f, 1.0f, (dbValues[(size_t) i] - noiseFloorDb) / range);
        processedBands[(size_t) i] = normalised;
    }

    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double delta = lastUpdateSeconds > 0.0 ? nowSeconds - lastUpdateSeconds : 0.0;
    lastUpdateSeconds = nowSeconds;

    if (peakHoldButton.getToggleState())
    {
        if (peakHoldBands.size() != (size_t) bins)
            peakHoldBands.assign ((size_t) bins, noiseFloorDb);

        const float decayDb = (float) (decayPerSecondDb * juce::jmax (0.0, delta));
        for (int i = 0; i < bins; ++i)
        {
            const float currentDb = noiseFloorDb + processedBands[(size_t) i] * range;
            float& holdDb = peakHoldBands[(size_t) i];
            holdDb = juce::jmax (currentDb, holdDb - decayDb);
        }
    }
    else
    {
        peakHoldBands.clear();
    }
}

void SpectrumMeter::rebuildPaths()
{
    spectrumPath.clear();
    overlayPath.clear();

    if (processedBands.empty() || frequencyAxis.size() != processedBands.size())
        return;

    const int bins = (int) processedBands.size();
    auto makePoint = [this] (int index, float normalised)
    {
        const float freq = frequencyAxis.empty() ? 20.0f + (float) index : frequencyAxis[(size_t) index];
        const float x = frequencyToNorm (freq);
        const float y = 1.0f - juce::jlimit (0.0f, 1.0f, normalised);
        return juce::Point<float> (x, y);
    };

    if (displayMode == DisplayMode::line || displayMode == DisplayMode::filledLine || displayMode == DisplayMode::overlay)
    {
        spectrumPath.startNewSubPath (makePoint (0, processedBands[0]));
        for (int i = 1; i < bins; ++i)
            spectrumPath.lineTo (makePoint (i, processedBands[(size_t) i]));

        if (displayMode == DisplayMode::filledLine)
        {
            const float endX = frequencyToNorm (frequencyAxis.back());
            const float startX = frequencyToNorm (frequencyAxis.front());
            spectrumPath.lineTo (endX, 1.0f);
            spectrumPath.lineTo (startX, 1.0f);
            spectrumPath.closeSubPath();
        }
    }

    if (displayMode == DisplayMode::overlay && peakHoldBands.size() == (size_t) bins && ! frequencyAxis.empty())
    {
        overlayPath.startNewSubPath (makePoint (0, juce::jlimit (0.0f, 1.0f, (peakHoldBands[0] - noiseFloorDb) / (-noiseFloorDb))));
        for (int i = 1; i < bins; ++i)
        {
            const float norm = juce::jlimit (0.0f, 1.0f, (peakHoldBands[(size_t) i] - noiseFloorDb) / (-noiseFloorDb));
            overlayPath.lineTo (makePoint (i, norm));
        }
    }
}

void SpectrumMeter::updateLegendText()
{
    juce::StringArray items;
    items.add ("Scale " + scaleBox.getText().toUpperCase());
    items.add ("Mode " + modeBox.getText().toUpperCase());
    items.add ("Smooth " + smoothingBox.getText().toUpperCase());
    items.add (juce::String::formatted ("Tilt %+.1f dB/oct", tiltDbPerOct));
    items.add (juce::String::formatted ("Floor %0.0f dB", noiseFloorDb));
    if (peakHoldButton.getToggleState())
        items.add (juce::String::formatted ("Hold %0.1f dB/s", decayPerSecondDb));
    legendText = items.joinIntoString ("  •  ");
}

float SpectrumMeter::frequencyToNorm (float frequency) const noexcept
{
    const float minFreq = 20.0f;
    const float maxFreq = (float) juce::jmax (20000.0, sampleRate * 0.5);
    const float clamped = juce::jlimit (minFreq, maxFreq, frequency);

    switch (scale)
    {
        case Scale::linear:
            return (clamped - minFreq) / juce::jmax (1.0f, maxFreq - minFreq);
        case Scale::logarithmic:
            return (std::log10 (clamped) - std::log10 (minFreq)) / (std::log10 (maxFreq) - std::log10 (minFreq));
        case Scale::mel:
        {
            const auto hzToMel = [] (float hz) { return 2595.0f * std::log10 (1.0f + hz / 700.0f); };
            const float melMin = hzToMel (minFreq);
            const float melMax = hzToMel (maxFreq);
            return (hzToMel (clamped) - melMin) / juce::jmax (1.0f, melMax - melMin);
        }
    }

    return 0.0f;
}

float SpectrumMeter::getSmoothingAmount() const noexcept
{
    switch (smoothingBox.getSelectedId())
    {
        case 1: return 0.35f;
        case 2: return 0.75f;
        default: break;
    }

    return 0.0f;
}

void SpectrumMeter::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto headerBounds = getPanelHeaderBounds();
    auto area = drawPanelHeader (g, panel);

    if (! headerBounds.isEmpty())
    {
        auto infoArea = headerBounds.reduced (12.0f, 6.0f);
        if (infoArea.getWidth() > 40.0f)
        {
            g.setColour (theme.text.withAlpha (0.68f));
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            const int binCount = (int) bands.size();
            juce::String headerText;
            if (sampleRate > 0.0)
                headerText << juce::String (sampleRate, 0) << " Hz  ";
            headerText << binCount << " bins";
            g.drawFittedText (headerText, infoArea.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    auto content = area;

    const auto drawStripBackground = [this, &g] (juce::Rectangle<float> strip)
    {
        if (strip.getWidth() <= 0.0f || strip.getHeight() <= 3.0f)
            return;

        auto background = strip.reduced (4.0f, 4.0f);
        g.setColour (theme.background.withAlpha (0.12f));
        g.fillRoundedRectangle (background, 7.0f);
        g.setColour (theme.outline.withAlpha (0.22f));
        g.drawRoundedRectangle (background, 7.0f, 1.0f);
    };

    auto comboStrip = content.removeFromTop (34.0f);
    drawStripBackground (comboStrip);

    auto sliderStrip = content.removeFromTop (42.0f);
    drawStripBackground (sliderStrip);

    juce::Rectangle<float> statusStrip;
    if (content.getHeight() > 110.0f)
        statusStrip = content.removeFromTop (24.0f);

    juce::Rectangle<float> legendStrip;
    if (legendVisible && content.getHeight() > 60.0f)
        legendStrip = content.removeFromBottom (26.0f);

    auto plot = content.reduced (12.0f, 12.0f);
    if (plot.isEmpty())
        return;

    auto frame = plot.expanded (4.0f, 6.0f);
    juce::ColourGradient background (theme.background.darker (0.32f), frame.getBottomLeft(),
                                     theme.background.darker (0.12f), frame.getTopRight(), false);
    g.setGradientFill (background);
    g.fillRoundedRectangle (frame, 10.0f);
    g.setColour (theme.outline.withAlpha (0.35f));
    g.drawRoundedRectangle (frame, 10.0f, 1.2f);

    if (! statusStrip.isEmpty())
    {
        auto strip = statusStrip.reduced (4.0f, 2.0f);
        g.setColour (theme.background.withAlpha (0.1f));
        g.fillRoundedRectangle (strip, 6.0f);
        g.setColour (theme.outline.withAlpha (0.16f));
        g.drawRoundedRectangle (strip, 6.0f, 1.0f);

        juce::String statusText;
        statusText << (peakHoldButton.getToggleState() ? juce::String::formatted ("Peak Hold %.1f dB/s", decayPerSecondDb)
                                                       : juce::String ("Peak Hold Off"));
        statusText << "  •  ";
        statusText << (gridButton.getToggleState() ? "Grid On" : "Grid Off");
        statusText << "  •  " << juce::String::formatted ("Tilt %+.1f dB/oct", tiltDbPerOct);

        g.setColour (theme.text.withAlpha (0.72f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (statusText, strip.toNearestInt(), juce::Justification::centredLeft, 1);

        juce::String rangeText = juce::String::formatted ("Noise Floor %0.0f dB", noiseFloorDb);
        g.drawFittedText (rangeText, strip.toNearestInt(), juce::Justification::centredRight, 1);
    }

    const bool drawGrid = gridButton.getToggleState();
    const bool drawLegend = legendVisible && ! legendStrip.isEmpty() && ! legendText.isEmpty();

    if (! hasData || processedBands.empty())
    {
        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawFittedText ("Waiting for analysis...", plot.toNearestInt(), juce::Justification::centred, 1);
    }
    else
    {
        if (drawGrid)
        {
            const float bottom = plot.getBottom();
            const float height = plot.getHeight();
            const float left = plot.getX();
            const float right = plot.getRight();

            std::vector<float> dbLines;
            dbLines.push_back (0.0f);
            float step = 6.0f;
            for (float db = -6.0f; db >= noiseFloorDb && dbLines.size() < 10; db -= step)
            {
                dbLines.push_back (db);
                if (db <= -24.0f)
                    step = 12.0f;
            }
            if (dbLines.empty() || std::abs (dbLines.back() - noiseFloorDb) > 1.5f)
                dbLines.push_back (noiseFloorDb);

            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            for (float db : dbLines)
            {
                const float norm = juce::jlimit (0.0f, 1.0f, (db - noiseFloorDb) / (-noiseFloorDb));
                const float y = bottom - height * norm;
                const bool emphasise = std::abs (db) < 0.5f;
                g.setColour ((emphasise ? theme.text.withAlpha (0.4f) : theme.text.withAlpha (0.22f)));
                g.drawLine (left, y, right, y, emphasise ? 1.2f : 0.8f);

                auto labelBounds = juce::Rectangle<float> (left - 58.0f, y - 8.0f, 54.0f, 16.0f);
                g.setColour (theme.text.withAlpha (0.52f));
                g.drawFittedText (juce::String::formatted ("%0.0f dB", db), labelBounds.toNearestInt(), juce::Justification::centredRight, 1);
            }

            std::vector<double> freqMarks;
            const double maxFreq = juce::jmax (20000.0, sampleRate * 0.5);
            const double minFreq = 20.0;

            if (scale == Scale::linear)
            {
                const int divisions = 8;
                for (int i = 1; i < divisions; ++i)
                    freqMarks.push_back (minFreq + (maxFreq - minFreq) * (double) i / (double) divisions);
            }
            else if (scale == Scale::logarithmic)
            {
                const std::array<double, 18> candidates { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0,
                                                          400.0, 600.0, 800.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };
                for (auto value : candidates)
                    if (value > minFreq && value < maxFreq)
                        freqMarks.push_back (value);
            }
            else
            {
                const auto hzFromMel = [] (double mel)
                {
                    return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0);
                };
                const double melMin = 2595.0 * std::log10 (1.0 + minFreq / 700.0);
                const double melMax = 2595.0 * std::log10 (1.0 + maxFreq / 700.0);
                for (int i = 1; i < 8; ++i)
                {
                    const double mel = melMin + (melMax - melMin) * (double) i / 8.0;
                    freqMarks.push_back (hzFromMel (mel));
                }
            }

            for (auto freq : freqMarks)
            {
                const float norm = frequencyToNorm ((float) freq);
                const float x = left + plot.getWidth() * norm;
                g.setColour (theme.text.withAlpha (0.18f));
                g.drawLine (x, plot.getY(), x, bottom, 0.8f);

                juce::String label = freq >= 1000.0 ? juce::String (freq / 1000.0, 1) + " k" : juce::String ((int) freq);
                auto labelBounds = juce::Rectangle<float> (x - 32.0f, bottom + 4.0f, 64.0f, 16.0f);
                g.setColour (theme.text.withAlpha (0.48f));
                g.drawFittedText (label, labelBounds.toNearestInt(), juce::Justification::centred, 1);
            }
        }

        auto transform = juce::AffineTransform::scale (plot.getWidth(), plot.getHeight())
                                                    .followedBy (juce::AffineTransform::translation (plot.getX(), plot.getY()));

        const int bins = (int) processedBands.size();

        if (displayMode == DisplayMode::bars)
        {
            const bool haveHold = peakHoldButton.getToggleState() && peakHoldBands.size() == (size_t) bins;
            for (int i = 0; i < bins; ++i)
            {
                const float freq = frequencyAxis[(size_t) i];
                const float centre = juce::jlimit (0.0f, 1.0f, frequencyToNorm (freq));
                const float prev = (i > 0) ? juce::jlimit (0.0f, 1.0f, frequencyToNorm (frequencyAxis[(size_t) (i - 1)])) : centre;
                const float next = (i < bins - 1) ? juce::jlimit (0.0f, 1.0f, frequencyToNorm (frequencyAxis[(size_t) (i + 1)])) : centre;
                const float leftNorm = (i == 0) ? centre - (next - centre) * 0.5f : (centre + prev) * 0.5f;
                const float rightNorm = (i == bins - 1) ? centre + (centre - prev) * 0.5f : (centre + next) * 0.5f;

                const float x = plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, leftNorm);
                const float width = plot.getWidth() * juce::jlimit (0.002f, 1.0f, rightNorm - leftNorm);
                const float value = juce::jlimit (0.0f, 1.0f, processedBands[(size_t) i]);
                const float y = plot.getBottom() - plot.getHeight() * value;

                juce::Rectangle<float> bar (x, y, width, plot.getBottom() - y);
                const float radius = juce::jmin (3.0f, width * 0.45f);
                g.setColour (theme.primary.withAlpha (0.28f));
                g.fillRoundedRectangle (bar, radius);
                g.setColour (theme.secondary.withAlpha (0.92f));
                g.drawRoundedRectangle (bar, radius, juce::jmax (0.8f, width * 0.08f));

                if (haveHold)
                {
                    const float holdNorm = juce::jlimit (0.0f, 1.0f, (peakHoldBands[(size_t) i] - noiseFloorDb) / (-noiseFloorDb));
                    const float holdY = plot.getBottom() - plot.getHeight() * holdNorm;
                    g.setColour (theme.tertiary.withAlpha (0.85f));
                    g.drawLine (bar.getX(), holdY, bar.getRight(), holdY, 1.2f);
                }
            }
        }
        else
        {
            auto drawSpectrumPath = [&g, transform] (const juce::Path& source, juce::Colour fill, juce::Colour stroke, bool fillClosed)
            {
                if (source.isEmpty())
                    return;

                juce::Path path = source;
                path.applyTransform (transform);

                if (fillClosed)
                {
                    g.setColour (fill);
                    g.fillPath (path);
                }

                g.setColour (stroke);
                g.strokePath (path, juce::PathStrokeType (1.9f, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
            };

            const bool filled = displayMode == DisplayMode::filledLine;
            const bool overlay = displayMode == DisplayMode::overlay;

            juce::Colour baseFill = theme.primary.withAlpha (filled ? 0.24f : 0.08f);
            juce::Colour baseStroke = theme.secondary.withAlpha (0.95f);

            drawSpectrumPath (spectrumPath, baseFill, baseStroke, filled);

            if (overlay && ! overlayPath.isEmpty())
            {
                juce::Colour overlayColour = theme.tertiary.withAlpha (0.9f);
                juce::Path hold = overlayPath;
                hold.applyTransform (transform);
                g.setColour (overlayColour);
                g.strokePath (hold, juce::PathStrokeType (1.5f));
            }
            else if (peakHoldButton.getToggleState() && peakHoldBands.size() == (size_t) bins)
            {
                for (int i = 0; i < bins; ++i)
                {
                    const float norm = juce::jlimit (0.0f, 1.0f, (peakHoldBands[(size_t) i] - noiseFloorDb) / (-noiseFloorDb));
                    const float x = plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, frequencyToNorm (frequencyAxis[(size_t) i]));
                    const float y = plot.getBottom() - plot.getHeight() * norm;
                    g.setColour (theme.tertiary.withAlpha (0.5f));
                    g.drawVerticalLine ((int) std::round (x), y - 6.0f, y + 6.0f);
                }
            }
        }
    }

    if (drawLegend)
    {
        auto legendArea = legendStrip.reduced (6.0f, 3.0f);
        g.setColour (theme.background.withAlpha (0.12f));
        g.fillRoundedRectangle (legendArea, 6.0f);
        g.setColour (theme.outline.withAlpha (0.2f));
        g.drawRoundedRectangle (legendArea, 6.0f, 1.0f);

        auto textArea = legendArea;
        auto axisArea = textArea.removeFromRight (juce::jmin (120.0f, legendArea.getWidth() * 0.32f));

        g.setColour (theme.text.withAlpha (0.72f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (legendText, textArea.toNearestInt(), juce::Justification::centredLeft, 2);

        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText ("FREQUENCY", axisArea.toNearestInt(), juce::Justification::centredRight, 1);
    }
}

LoudnessMeter::LoudnessMeter()
    : MeterComponent ("Loudness & Peak")
{
    targetLabel.setText ("Target", juce::dontSendNotification);
    targetLabel.setJustificationType (juce::Justification::centred);
    targetLabel.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    targetLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (targetLabel);

    targetSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    targetSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    targetSlider.setRange (-36.0, -6.0, 0.1);
    targetSlider.setNumDecimalPlacesToDisplay (1);
    targetSlider.setValue (state.targetLufs, juce::dontSendNotification);
    targetSlider.setTextValueSuffix (" LUFS");
    targetSlider.onValueChange = [this]
    {
        const float value = (float) targetSlider.getValue();
        if (std::abs (state.targetLufs - value) <= 0.001f)
            return;

        state.targetLufs = value;
        rebuildHistoryPath();
        notifyStateChanged();
        repaint();
    };
    addAndMakeVisible (targetSlider);

    targetPresetButton.setButtonText ("Presets");
    targetPresetButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Streaming  −14 LUFS");
        menu.addItem (2, "Broadcast  −23 LUFS");
        menu.addItem (3, "Cinema     −27 LUFS");

        menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int choice)
        {
            if (choice == 0)
                return;

            float value = state.targetLufs;
            switch (choice)
            {
                case 1: value = -14.0f; break;
                case 2: value = -23.0f; break;
                case 3: value = -27.0f; break;
                default: break;
            }

            targetSlider.setValue (value, juce::sendNotificationSync);
        });
    };
    addAndMakeVisible (targetPresetButton);

    targetResetButton.setButtonText ("Reset −14");
    targetResetButton.onClick = [this]
    {
        targetSlider.setValue (-14.0, juce::sendNotificationSync);
    };
    addAndMakeVisible (targetResetButton);

    rmsToggle.setButtonText ("Show RMS");
    rmsToggle.setClickingTogglesState (true);
    rmsToggle.onClick = [this]
    {
        const bool enabled = rmsToggle.getToggleState();
        if (state.showRms == enabled)
            return;

        state.showRms = enabled;
        notifyStateChanged();
        repaint();
    };
    addAndMakeVisible (rmsToggle);

    historyBox.setJustificationType (juce::Justification::centredLeft);
    for (int seconds : { 20, 60, 120 })
        historyBox.addItem (juce::String (seconds) + " s", seconds);
    historyBox.onChange = [this]
    {
        const int seconds = historyBox.getSelectedId();
        if (seconds <= 0 || seconds == state.historySeconds)
            return;

        updateHistorySelection (seconds);
        rebuildHistoryPath();
        notifyStateChanged();
        repaint();
    };
    addAndMakeVisible (historyBox);

    historyLengthLabel.setJustificationType (juce::Justification::centred);
    historyLengthLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    historyLengthLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (historyLengthLabel);

    clearHistoryButton.setButtonText ("Reset Stats");
    clearHistoryButton.onClick = [this]
    {
        if (onResetRequested != nullptr)
            onResetRequested();
    };
    addAndMakeVisible (clearHistoryButton);

    setState (state, true);
    updateControlColours();
}

void LoudnessMeter::setState (const State& newState, bool force)
{
    const bool changed = force
                         || std::abs (state.targetLufs - newState.targetLufs) > 0.001f
                         || state.showRms != newState.showRms
                         || state.historySeconds != newState.historySeconds;

    state = newState;

    targetSlider.setValue (state.targetLufs, juce::dontSendNotification);
    rmsToggle.setToggleState (state.showRms, juce::dontSendNotification);
    updateHistorySelection (state.historySeconds);
    rebuildHistoryPath();

    if (changed)
        repaint();
}

void LoudnessMeter::setOnStateChanged (std::function<void (const State&)> callback)
{
    onStateChanged = std::move (callback);
}

void LoudnessMeter::setOnResetRequested (std::function<void()> callback)
{
    onResetRequested = std::move (callback);
}

void LoudnessMeter::notifyStateChanged()
{
    if (onStateChanged != nullptr)
        onStateChanged (state);
}

void LoudnessMeter::updateHistorySelection (int seconds)
{
    state.historySeconds = juce::jlimit (20, 120, seconds);
    if (historyBox.getSelectedId() != state.historySeconds)
        historyBox.setSelectedId (state.historySeconds, juce::dontSendNotification);

    historyLengthLabel.setText ("History " + juce::String (state.historySeconds) + " s",
                                juce::dontSendNotification);
}

void LoudnessMeter::updateControlColours()
{
    const auto primary = theme.secondary;
    targetSlider.setColour (juce::Slider::thumbColourId, primary);
    targetSlider.setColour (juce::Slider::trackColourId, primary.withAlpha (0.35f));
    targetSlider.setColour (juce::Slider::rotarySliderFillColourId, primary);
    targetSlider.setColour (juce::Slider::rotarySliderOutlineColourId, theme.outline.withAlpha (0.4f));
    targetSlider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.9f));
    targetSlider.setColour (juce::Slider::textBoxOutlineColourId, theme.outline.withAlpha (0.25f));
    targetSlider.setColour (juce::Slider::textBoxBackgroundColourId, theme.background.darker (0.3f));

    targetLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.8f));
    historyLengthLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.6f));

    auto configureButton = [this] (juce::Button& button)
    {
        button.setColour (juce::TextButton::buttonColourId, theme.background.darker (0.25f));
        button.setColour (juce::TextButton::buttonOnColourId, theme.secondary.withAlpha (0.5f));
        button.setColour (juce::TextButton::textColourOnId, theme.text.withAlpha (0.9f));
        button.setColour (juce::TextButton::textColourOffId, theme.text.withAlpha (0.82f));
    };

    configureButton (targetPresetButton);
    configureButton (targetResetButton);
    configureButton (clearHistoryButton);

    historyBox.setColour (juce::ComboBox::textColourId, theme.text.withAlpha (0.82f));
    historyBox.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.3f));
    historyBox.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.22f));
    historyBox.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
    historyBox.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.9f));

    rmsToggle.setColour (juce::ToggleButton::textColourId, theme.text.withAlpha (0.78f));
    rmsToggle.setColour (juce::ToggleButton::tickColourId, theme.secondary);
    rmsToggle.setColour (juce::ToggleButton::tickDisabledColourId, theme.outline.withAlpha (0.3f));
}

void LoudnessMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
        updateControlColours();

    transport = snapshot.transport;

    momentary = snapshot.momentaryLufs;
    shortTerm = snapshot.shortTermLufs;
    integrated = snapshot.integratedLufs;
    loudnessRange = snapshot.loudnessRange;
    maxMomentary = snapshot.maxMomentaryLufs;
    maxShortTerm = snapshot.maxShortTermLufs;
    peakL = snapshot.peakLeft;
    peakR = snapshot.peakRight;
    clipL = snapshot.clipLeft;
    clipR = snapshot.clipRight;
    rmsFast = snapshot.rmsFast;
    rmsSlow = snapshot.rmsSlow;

    momentaryOverTarget = momentary > state.targetLufs;
    shortTermOverTarget = shortTerm > state.targetLufs;
    integratedOverTarget = integrated > state.targetLufs;

    const bool historyIntervalChanged = std::abs (historyInterval - snapshot.loudnessHistoryInterval) > 1.0e-6f;
    const bool historySizeChanged = historyValues.size() != snapshot.loudnessHistory.size();

    bool historyDataChanged = historyIntervalChanged || historySizeChanged;
    if (! historyDataChanged && historyValues.size() == snapshot.loudnessHistory.size())
    {
        historyDataChanged = ! std::equal (historyValues.begin(), historyValues.end(),
                                           snapshot.loudnessHistory.begin(),
                                           [] (float a, float b)
                                           {
                                               return std::abs (a - b) <= 0.001f;
                                           });
    }

    historyInterval = snapshot.loudnessHistoryInterval;

    if (historyDataChanged)
    {
        historyValues = snapshot.loudnessHistory;
        rebuildHistoryPath();
    }

    repaint();
}

void LoudnessMeter::resized()
{
    targetLabel.setBounds ({});
    targetSlider.setBounds ({});
    targetPresetButton.setBounds ({});
    targetResetButton.setBounds ({});
    rmsToggle.setBounds ({});
    historyBox.setBounds ({});
    historyLengthLabel.setBounds ({});
    clearHistoryButton.setBounds ({});

    auto content = getPanelContentBounds().toNearestInt();
    if (content.isEmpty())
        return;

    auto area = content;
    area.removeFromTop (juce::roundToInt (area.getHeight() * 0.58f)); // history graph

    auto lowerOuter = area;
    lowerOuter.removeFromLeft (juce::roundToInt (lowerOuter.getWidth() * 0.46f)); // loudness bars

    auto statsOuter = lowerOuter;
    auto statsBounds = statsOuter.reduced (6);
    if (statsBounds.isEmpty())
        return;

    const int controlWidth = (int) std::round (juce::jlimit (110.0f, 160.0f, statsBounds.getWidth() * 0.38f));
    auto controlColumn = statsBounds.removeFromRight (controlWidth);

    auto controls = controlColumn.reduced (6, 8);
    if (controls.isEmpty())
        return;

    auto remaining = controls;

    auto targetLabelBounds = remaining.removeFromTop (18);
    targetLabel.setBounds (targetLabelBounds);
    remaining.removeFromTop (2);

    const int knobDiameter = juce::jmin (remaining.getWidth(), 72);
    const int desiredKnobHeight = knobDiameter + targetSlider.getTextBoxHeight() + 6;
    const int knobHeight = juce::jmin (remaining.getHeight(), desiredKnobHeight);
    auto knobBounds = juce::Rectangle<int> (remaining.getCentreX() - knobDiameter / 2, remaining.getY(),
                                            knobDiameter, knobHeight);
    targetSlider.setBounds (knobBounds);
    remaining.removeFromTop (knobHeight);
    if (remaining.getHeight() > 0)
        remaining.removeFromTop (juce::jmin (8, remaining.getHeight()));

    if (remaining.getHeight() > 0)
    {
        auto buttonRow = remaining.removeFromTop (juce::jmin (24, remaining.getHeight()));
        auto presetBounds = buttonRow.removeFromLeft (buttonRow.getWidth() / 2);
        targetPresetButton.setBounds (presetBounds.reduced (2, 2));
        buttonRow.removeFromLeft (4);
        targetResetButton.setBounds (buttonRow.reduced (2, 2));
    }

    if (remaining.getHeight() > 0)
        remaining.removeFromTop (juce::jmin (6, remaining.getHeight()));

    if (remaining.getHeight() > 0)
    {
        auto rmsBounds = remaining.removeFromTop (juce::jmin (24, remaining.getHeight()));
        rmsToggle.setBounds (rmsBounds.reduced (2, 2));
    }

    if (remaining.getHeight() > 0)
        remaining.removeFromBottom (juce::jmin (4, remaining.getHeight()));

    auto clearBounds = remaining.removeFromBottom (juce::jmin (24, remaining.getHeight()));
    clearHistoryButton.setBounds (clearBounds.reduced (2, 2));

    if (remaining.getHeight() > 0)
        remaining.removeFromBottom (juce::jmin (6, remaining.getHeight()));

    auto historyBoxBounds = remaining.removeFromBottom (juce::jmin (24, remaining.getHeight()));
    historyBox.setBounds (historyBoxBounds.reduced (2, 2));

    if (remaining.getHeight() > 0)
        remaining.removeFromBottom (juce::jmin (4, remaining.getHeight()));

    auto historyLabelBounds = remaining.removeFromBottom (juce::jmin (16, remaining.getHeight()));
    historyLengthLabel.setBounds (historyLabelBounds);
}

void LoudnessMeter::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto headerBounds = getPanelHeaderBounds();
    auto area = drawPanelHeader (g, panel);

    auto findNiceInterval = [] (double spanSeconds)
    {
        if (spanSeconds <= 0.0)
            return 0.0;

        const double targetDivisions = 6.0;
        double raw = spanSeconds / targetDivisions;
        if (raw <= 0.0)
            return 0.0;

        const double pow10 = std::pow (10.0, std::floor (std::log10 (raw)));
        const double mantissa = raw / pow10;

        double nice = 1.0;
        if (mantissa < 1.5)
            nice = 1.0;
        else if (mantissa < 3.0)
            nice = 2.0;
        else if (mantissa < 7.0)
            nice = 5.0;
        else
            nice = 10.0;

        return nice * pow10;
    };

    if (! headerBounds.isEmpty())
    {
        auto infoArea = headerBounds.reduced (12.0f, 6.0f);
        if (infoArea.getWidth() > 60.0f)
        {
            g.setColour (theme.text.withAlpha (0.68f));
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            juce::String headerText;
            headerText << "M " << juce::String (momentary, 1) << " LU   S "
                       << juce::String (shortTerm, 1) << " LU   I "
                       << juce::String (integrated, 1) << " LUFS";
            g.drawFittedText (headerText, infoArea.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    float overAmount = 0.0f;
    if (momentary > state.targetLufs)
        overAmount = juce::jmax (overAmount, momentary - state.targetLufs);
    if (shortTerm > state.targetLufs)
        overAmount = juce::jmax (overAmount, shortTerm - state.targetLufs);
    if (integrated > state.targetLufs)
        overAmount = juce::jmax (overAmount, integrated - state.targetLufs);

    auto graphOuter = area.removeFromTop (area.getHeight() * 0.58f).reduced (10.0f, 8.0f);
    auto lowerOuter = area.reduced (10.0f, 8.0f);

    g.setColour (theme.background.darker (0.3f));
    g.fillRoundedRectangle (graphOuter, 12.0f);
    g.setColour (theme.outline.withAlpha (0.65f));
    g.drawRoundedRectangle (graphOuter, 12.0f, 1.2f);

    auto graphHeaderBounds = graphOuter.reduced (14.0f, 10.0f);
    auto headerStrip = graphHeaderBounds.removeFromTop (20.0f);
    auto leftHeader = headerStrip.removeFromLeft (headerStrip.getWidth() * 0.5f);
    auto rightHeader = headerStrip;

    g.setColour (theme.text.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (juce::String::formatted ("Target %0.0f LUFS", state.targetLufs), leftHeader.toNearestInt(),
                juce::Justification::centredLeft, true);

    if (overAmount > 0.1f)
    {
        g.setColour (theme.warning.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText ("Over target by +" + juce::String (overAmount, 1) + " LU", rightHeader.toNearestInt(),
                    juce::Justification::centredRight, true);
    }

    auto plotBounds = graphOuter.reduced (16.0f, 14.0f);
    auto scaleArea = plotBounds.removeFromLeft (36.0f);
    auto axisArea = plotBounds.removeFromBottom (18.0f);
    auto historyArea = plotBounds;

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (int i = 0; i <= 6; ++i)
    {
        const float ratio = (float) i / 6.0f;
        const float y = historyArea.getY() + historyArea.getHeight() * ratio;
        const float lufs = juce::jmap (ratio, 0.0f, 1.0f, 0.0f, -60.0f);

        g.setColour (theme.outline.withAlpha (i == 0 ? 0.35f : 0.18f));
        g.drawLine (historyArea.getX(), y, historyArea.getRight(), y, i == 0 ? 1.1f : 0.6f);

        auto labelArea = scaleArea.withHeight (16.0f).withY (y - 8.0f);
        g.setColour (theme.text.withAlpha (0.45f));
        g.drawFittedText (juce::String::formatted ("%0.0f", lufs), labelArea.toNearestInt(), juce::Justification::centredRight, 1);
    }

    if (historyHasData)
    {
        const auto historyTransform = juce::AffineTransform::scale (historyArea.getWidth(), historyArea.getHeight())
                                                    .followedBy (juce::AffineTransform::translation (historyArea.getX(), historyArea.getY()));

        if (! historyFillPath.isEmpty())
        {
            juce::ColourGradient gradient (theme.primary.withAlpha (0.16f), historyArea.getBottomLeft().toFloat(),
                                           theme.secondary.withAlpha (0.32f), historyArea.getTopRight().toFloat(), false);
            g.setGradientFill (gradient);
            g.fillPath (historyFillPath, historyTransform);
        }

        if (historyOverHasData && ! historyOverFillPath.isEmpty())
        {
            g.setColour (theme.warning.withAlpha (0.22f));
            g.fillPath (historyOverFillPath, historyTransform);
        }

        if (targetLinePosition >= 0.0f && targetLinePosition <= 1.0f)
        {
            const float targetY = juce::jmap (targetLinePosition, 0.0f, 1.0f, historyArea.getY(), historyArea.getBottom());
            const float dashes[] = { 5.0f, 4.0f };
            g.setColour (theme.warning.withAlpha (0.4f));
            g.drawDashedLine (juce::Line<float> (historyArea.getX(), targetY, historyArea.getRight(), targetY), dashes, 2, 1.2f);
        }

        if (! historyPath.isEmpty())
        {
            g.setColour (theme.secondary.withAlpha (0.9f));
            g.strokePath (historyPath, juce::PathStrokeType (1.6f), historyTransform);
        }

        if (historyOverHasData && ! historyOverPath.isEmpty())
        {
            g.setColour (theme.warning.withAlpha (0.85f));
            g.strokePath (historyOverPath, juce::PathStrokeType (1.35f), historyTransform);
        }

        if (integrated > -95.0f)
        {
            const float norm = juce::jlimit (0.0f, 1.0f, 1.0f + juce::jlimit (-60.0f, 0.0f, integrated) / 60.0f);
            const float y = historyArea.getBottom() - historyArea.getHeight() * norm;
            g.setColour ((integratedOverTarget ? theme.warning : theme.tertiary).withAlpha (0.65f));
            g.drawLine (historyArea.getX(), y, historyArea.getRight(), y, 1.2f);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::italic)));
            g.drawFittedText ("Integrated", historyArea.withHeight (18.0f).withY (y - 18.0f).toNearestInt(), juce::Justification::centredRight, 1);
        }

        if (integrated > -95.0f)
        {
            const auto drawGateLine = [&, this] (float offset, const juce::String& label, float alpha)
            {
                const float gateValue = integrated - offset;
                if (gateValue <= -95.0f)
                    return;

                const float norm = juce::jlimit (0.0f, 1.0f, 1.0f + clampDisplayLoudness (gateValue) / 60.0f);
                const float y = historyArea.getBottom() - historyArea.getHeight() * norm;
                const float dashes[] = { 3.0f, 3.0f };
                g.setColour (theme.secondary.withAlpha (alpha));
                g.drawDashedLine ({ historyArea.getX(), y, historyArea.getRight(), y }, dashes, 2, 1.0f);
                g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::italic)));
                g.drawFittedText (label, juce::Rectangle<int> ((int) historyArea.getX() + 4, (int) y - 14, 80, 12), juce::Justification::centredLeft, 1);
            };

            drawGateLine (10.0f, "Gate −10", 0.35f);
            drawGateLine (20.0f, "Gate −20", 0.28f);
        }

        if (! historyValues.empty())
        {
            const float latest = historyValues.back();
            if (latest > -95.0f)
            {
                const float norm = juce::jlimit (0.0f, 1.0f, 1.0f + juce::jlimit (-60.0f, 0.0f, latest) / 60.0f);
                const float markerY = historyArea.getBottom() - historyArea.getHeight() * norm;
                g.setColour ((latest > state.targetLufs) ? theme.warning : theme.primary);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ historyArea.getRight(), markerY }));
            }
        }

        auto axisLine = axisArea;
        if (axisLine.getHeight() > 4.0f)
        {
            g.setColour (theme.outline.withAlpha (0.35f));
            g.drawLine (axisLine.getX(), axisLine.getCentreY(), axisLine.getRight(), axisLine.getCentreY(), 1.0f);

            if (visibleHistorySeconds > 0.0f)
            {
                bool drewBeatGrid = false;
                if (shouldDrawBeatGrid (transport))
                {
                    const auto beatLines = createBeatGridLines (transport, visibleHistorySeconds);
                    if (! beatLines.empty())
                    {
                        drewBeatGrid = true;
                        for (const auto& line : beatLines)
                        {
                            const float norm = (float) juce::jlimit (0.0, 1.0, line.timeFromNow / juce::jmax (1.0e-6f, visibleHistorySeconds));
                            const float x = historyArea.getRight() - historyArea.getWidth() * norm;
                            const float halfHeight = line.isBarStart ? 5.0f : 3.0f;
                            g.drawLine (x, axisLine.getCentreY() - halfHeight, x, axisLine.getCentreY() + halfHeight, line.isBarStart ? 1.1f : 1.0f);
                            g.setColour (theme.text.withAlpha (line.isBarStart ? 0.6f : 0.45f));
                            g.setFont (juce::Font (juce::FontOptions (10.0f)));
                            g.drawFittedText (beatLabelForLine (line),
                                              juce::Rectangle<int> ((int) x - 20, (int) axisLine.getBottom() - 12, 40, 12),
                                              juce::Justification::centred, 1);
                            g.setColour (theme.outline.withAlpha (0.35f));
                        }
                    }
                }

                if (! drewBeatGrid)
                {
                    const double interval = findNiceInterval (visibleHistorySeconds);
                    if (interval > 0.0)
                    {
                        for (double t = interval; t < visibleHistorySeconds - interval * 0.25; t += interval)
                        {
                            const float norm = (float) (t / visibleHistorySeconds);
                            const float x = historyArea.getRight() - historyArea.getWidth() * norm;
                            g.drawLine (x, axisLine.getCentreY() - 3.0f, x, axisLine.getCentreY() + 3.0f, 1.0f);
                            g.setColour (theme.text.withAlpha (0.45f));
                            g.setFont (juce::Font (juce::FontOptions (10.0f)));
                            g.drawFittedText (juce::String::formatted ("-%0.0f", t), juce::Rectangle<int> ((int) x - 18, (int) axisLine.getBottom() - 12, 36, 12), juce::Justification::centred, 1);
                            g.setColour (theme.outline.withAlpha (0.35f));
                        }
                    }
                }

                g.setColour (theme.text.withAlpha (0.45f));
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                auto zeroLabelArea = axisLine.removeFromRight (40).toNearestInt();
                g.drawFittedText ("now", zeroLabelArea, juce::Justification::centredRight, 1);
            }
        }
    }
    else
    {
        g.setColour (theme.text.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawFittedText ("Collecting history...", historyArea.toNearestInt(), juce::Justification::centred, 1);
    }

    const double historySeconds = visibleHistorySeconds;
    g.setColour (theme.text.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    juce::String historyLabel = juce::String::formatted ("History %.0f s", historySeconds);
    if (state.historySeconds > 0)
        historyLabel << " of " << state.historySeconds << " s";
    if (overTargetSeconds > 0.0)
        historyLabel += juce::String::formatted ("  -  Over %0.1f s", overTargetSeconds);
    g.drawText (historyLabel, graphOuter.reduced (18.0f).removeFromBottom (16.0f).toNearestInt(), juce::Justification::centredRight, true);

    auto meterOuter = lowerOuter.removeFromLeft (lowerOuter.getWidth() * 0.46f);
    auto statsOuter = lowerOuter;

    auto meterBounds = meterOuter.reduced (8.0f);
    const float gap = 18.0f;
    const float barWidth = (meterBounds.getWidth() - gap) * 0.5f;
    juce::Rectangle<float> momentaryBounds { meterBounds.getX(), meterBounds.getY(), barWidth, meterBounds.getHeight() };
    juce::Rectangle<float> shortTermBounds { momentaryBounds.getRight() + gap, meterBounds.getY(), barWidth, meterBounds.getHeight() };

    auto drawBar = [&] (juce::Rectangle<float> bounds, const juce::String& label, float value, float maxValue, juce::Colour colour)
    {
        auto header = bounds.removeFromTop (18.0f);
        auto footer = bounds.removeFromBottom (22.0f);
        auto body = bounds.reduced (4.0f);

        g.setColour (theme.text.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawFittedText (label, header.toNearestInt(), juce::Justification::centred, 1);

        g.setColour (theme.background.darker (0.22f));
        g.fillRoundedRectangle (body, 6.0f);
        g.setColour (theme.outline.withAlpha (0.55f));
        g.drawRoundedRectangle (body, 6.0f, 1.0f);

        for (int i = 1; i < 6; ++i)
        {
            const float ratio = (float) i / 6.0f;
            const float y = body.getY() + body.getHeight() * ratio;
            g.setColour (theme.outline.withAlpha (0.18f));
            g.drawLine (body.getX(), y, body.getRight(), y, 0.6f);
        }

        auto dangerZone = body;
        const float clampedTarget = clampDisplayLoudness (state.targetLufs);
        const float targetNorm = juce::jlimit (0.0f, 1.0f, 1.0f + clampedTarget / 60.0f);
        const float targetY = body.getBottom() - body.getHeight() * targetNorm;
        if (targetNorm > 0.0f && targetNorm < 1.0f)
        {
            dangerZone = body.withY (targetY).withHeight (body.getBottom() - targetY);
            g.setColour (theme.warning.withAlpha (0.12f));
            g.fillRect (dangerZone);
            g.setColour (theme.warning.withAlpha (0.5f));
            g.drawLine (body.getX(), targetY, body.getRight(), targetY, 1.0f);
        }

        const bool overNow = value > state.targetLufs;
        const float clampedValue = value > -95.0f ? clampDisplayLoudness (value) : -95.0f;
        const float valueNorm = juce::jlimit (0.0f, 1.0f, 1.0f + clampedValue / 60.0f);
        auto fill = body;
        fill.setHeight (body.getHeight() * valueNorm);
        fill.setY (body.getBottom() - fill.getHeight());
        if (value > -95.0f)
        {
            g.setColour ((overNow ? theme.warning : colour).withAlpha (0.85f));
            g.fillRect (fill);
        }

        if (maxValue > -95.0f)
        {
            const float maxNorm = juce::jlimit (0.0f, 1.0f, 1.0f + clampDisplayLoudness (maxValue) / 60.0f);
            const float maxY = body.getBottom() - body.getHeight() * maxNorm;
            g.setColour ((maxValue > state.targetLufs ? theme.warning : colour.brighter (0.15f)).withAlpha (0.9f));
            g.drawLine (body.getX(), maxY, body.getRight(), maxY, 1.2f);
        }

        g.setColour (overNow ? theme.warning : theme.text.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        juce::String valueText = (value <= -95.0f) ? juce::String ("--.- LUFS") : juce::String::formatted ("%0.1f LUFS", value);
        if (value > -95.0f)
        {
            const float diff = value - state.targetLufs;
            if (std::abs (diff) >= 0.05f)
            {
                juce::String diffText = juce::String (diff, 1);
                if (diff >= 0.0f)
                    diffText = "+" + diffText;
                valueText += " (" + diffText + ")";
            }
        }
        g.drawFittedText (valueText, footer.toNearestInt(), juce::Justification::centred, 1);
    };

    drawBar (momentaryBounds, "Momentary", momentary, maxMomentary, theme.primary);
    drawBar (shortTermBounds, "Short-Term", shortTerm, maxShortTerm, theme.secondary);

    auto statsBounds = statsOuter.reduced (6.0f);
    const float controlColumnWidth = juce::jlimit (110.0f, 160.0f, statsBounds.getWidth() * 0.38f);
    auto controlColumn = statsBounds.removeFromRight (controlColumnWidth);
    juce::ignoreUnused (controlColumn);

    auto integratedBox = statsBounds.removeFromTop (statsBounds.getHeight() * 0.52f);
    auto infoBounds = statsBounds;

    g.setColour (theme.text.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("Integrated", integratedBox.removeFromTop (20.0f), juce::Justification::centredLeft, false);

    g.setColour ((integratedOverTarget ? theme.warning : theme.primary).brighter (0.15f));
    g.setFont (juce::Font (juce::FontOptions (38.0f, juce::Font::bold)));
    const juce::String integratedText = (integrated <= -95.0f) ? juce::String ("--.-") : juce::String::formatted ("%0.1f", integrated);
    auto integratedValueBounds = integratedBox.removeFromTop (46.0f);
    g.drawFittedText (integratedText, integratedValueBounds.toNearestInt(), juce::Justification::centredLeft, 1);

    g.setColour (theme.text.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (16.0f)));
    g.drawText ("LUFS", integratedBox.removeFromTop (24.0f), juce::Justification::centredLeft, false);

    auto formatLufs = [] (float value)
    {
        return (value <= -95.0f) ? juce::String ("--.- LUFS") : juce::String::formatted ("%0.1f LUFS", value);
    };

    auto formatDb = [] (float peak)
    {
        const float db = juce::Decibels::gainToDecibels (peak + 1.0e-6f, -60.0f);
        return juce::String::formatted ("%0.1f dB", db);
    };

    auto drawStat = [&] (const juce::String& label, const juce::String& value, juce::Colour valueColour, bool clipped = false)
    {
        auto row = infoBounds.removeFromTop (22.0f);
        auto clipArea = clipped ? row.removeFromRight (18.0f) : juce::Rectangle<float>();
        auto valueArea = row.removeFromRight (row.getWidth() * 0.45f);
        auto labelArea = row;

        g.setColour (theme.text.withAlpha (0.62f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (label, labelArea.toNearestInt(), juce::Justification::centredLeft, true);

        g.setColour (valueColour);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (value, valueArea.toNearestInt(), juce::Justification::centredRight, true);

        if (clipped)
        {
            g.setColour (theme.warning);
            g.fillEllipse (clipArea.reduced (4.0f));
        }
    };

    drawStat ("Loudness Range", juce::String::formatted ("%0.1f LU", loudnessRange), theme.text.withAlpha (0.85f));
    drawStat ("Momentary Max", formatLufs (maxMomentary), momentaryOverTarget ? theme.warning : theme.text.withAlpha (0.85f));
    drawStat ("Short-Term Max", formatLufs (maxShortTerm), shortTermOverTarget ? theme.warning : theme.text.withAlpha (0.85f));
    juce::String overTimeText = juce::String::formatted ("%0.1f s", overTargetSeconds);
    if (historySeconds > 0.0)
    {
        const double percent = juce::jlimit (0.0, 100.0, (overTargetSeconds / historySeconds) * 100.0);
        overTimeText += juce::String::formatted (" (%0.0f%%)", percent);
    }
    drawStat ("Time Over Target", overTimeText, overTargetSeconds > 0.0 ? theme.warning : theme.text.withAlpha (0.65f), overTargetSeconds > 0.01);

    if (state.showRms)
    {
        drawStat ("RMS Fast", formatDb (rmsFast), theme.text.withAlpha (0.78f));
        drawStat ("RMS Slow", formatDb (rmsSlow), theme.text.withAlpha (0.78f));
    }

    drawStat ("Peak L", formatDb (peakL), theme.text.withAlpha (0.85f), clipL);
    drawStat ("Peak R", formatDb (peakR), theme.text.withAlpha (0.85f), clipR);
    drawStat ("History Span", juce::String::formatted ("%0.0f s", historySeconds), theme.text.withAlpha (0.7f));
}

void LoudnessMeter::rebuildHistoryPath()
{
    historyPath.clear();
    historyFillPath.clear();
    historyOverPath.clear();
    historyOverFillPath.clear();
    historyHasData = false;
    historyOverHasData = false;
    overTargetSeconds = 0.0;
    historyPoints.clear();

    if (historyValues.size() < 2 || historyInterval <= 0.0f)
    {
        visibleHistoryPoints = 0;
        visibleHistorySeconds = 0.0f;
        historyLengthLabel.setText ("History " + juce::String (state.historySeconds) + " s",
                                    juce::dontSendNotification);
        return;
    }

    const int totalPoints = (int) historyValues.size();
    const int requestedPoints = juce::jlimit (2, totalPoints,
                                              state.historySeconds > 0
                                                  ? (int) std::round ((double) state.historySeconds / historyInterval)
                                                  : totalPoints);
    const int startIndex = totalPoints - requestedPoints;

    visibleHistoryPoints = requestedPoints;
    visibleHistorySeconds = requestedPoints > 1 ? historyInterval * (float) (requestedPoints - 1) : 0.0f;

    const float clampedTarget = clampDisplayLoudness (state.targetLufs);
    const float targetNorm = juce::jlimit (0.0f, 1.0f, 1.0f + clampedTarget / 60.0f);
    targetLinePosition = 1.0f - targetNorm;

    historyPoints.reserve ((size_t) requestedPoints);

    historyHasData = false;
    double overSeconds = 0.0;

    for (int i = 0; i < requestedPoints; ++i)
    {
        const float value = historyValues[(size_t) (startIndex + i)];
        if (value > -95.0f)
            historyHasData = true;
        if (value > state.targetLufs)
            overSeconds += historyInterval;

        const float clamped = clampDisplayLoudness (value);
        const float norm = juce::jlimit (0.0f, 1.0f, 1.0f + clamped / 60.0f);
        const float x = requestedPoints > 1 ? (float) i / (float) (requestedPoints - 1) : 0.0f;
        historyPoints.push_back ({ { x, 1.0f - norm }, value, norm });
    }

    overTargetSeconds = juce::jmin ((double) visibleHistorySeconds, overSeconds);

    if (historyPoints.size() < 2)
    {
        historyLengthLabel.setText ("History " + juce::String (state.historySeconds) + " s",
                                    juce::dontSendNotification);
        return;
    }

    historyLengthLabel.setText (juce::String::formatted ("Viewing %0.0f s of %d s", visibleHistorySeconds, state.historySeconds),
                                juce::dontSendNotification);

    historyPath.preallocateSpace ((int) historyPoints.size() + 3);
    historyFillPath.preallocateSpace ((int) historyPoints.size() + 5);

    const auto& startPoint = historyPoints.front();
    historyPath.startNewSubPath (startPoint.position);
    historyFillPath.startNewSubPath (startPoint.position.x, 1.0f);
    historyFillPath.lineTo (startPoint.position);

    float lastX = startPoint.position.x;

    for (size_t i = 1; i < historyPoints.size(); ++i)
    {
        const auto& point = historyPoints[i].position;
        historyPath.lineTo (point);
        historyFillPath.lineTo (point);
        lastX = point.x;
    }

    historyFillPath.lineTo (lastX, 1.0f);
    historyFillPath.closeSubPath();

    for (size_t i = 1; i < historyPoints.size(); ++i)
    {
        const auto& prev = historyPoints[i - 1];
        const auto& curr = historyPoints[i];
        const bool prevAbove = prev.value > state.targetLufs;
        const bool currAbove = curr.value > state.targetLufs;

        if (! (prevAbove || currAbove))
            continue;

        juce::Point<float> start = prev.position;
        juce::Point<float> end = curr.position;

        if (! prevAbove)
        {
            double denom = (double) (curr.value - prev.value);
            if (std::abs (denom) < 1.0e-6)
                denom = denom >= 0.0 ? 1.0e-6 : -1.0e-6;
            const double t = juce::jlimit (0.0, 1.0, (state.targetLufs - prev.value) / denom);
            const float interpX = juce::jlimit (0.0f, 1.0f, prev.position.x + (curr.position.x - prev.position.x) * (float) t);
            const float interpNorm = prev.norm + (curr.norm - prev.norm) * (float) t;
            start = { interpX, 1.0f - juce::jlimit (0.0f, 1.0f, interpNorm) };
        }

        if (! currAbove)
        {
            double denom = (double) (curr.value - prev.value);
            if (std::abs (denom) < 1.0e-6)
                denom = denom >= 0.0 ? 1.0e-6 : -1.0e-6;
            const double t = juce::jlimit (0.0, 1.0, (state.targetLufs - prev.value) / denom);
            const float interpX = juce::jlimit (0.0f, 1.0f, prev.position.x + (curr.position.x - prev.position.x) * (float) t);
            const float interpNorm = prev.norm + (curr.norm - prev.norm) * (float) t;
            end = { interpX, 1.0f - juce::jlimit (0.0f, 1.0f, interpNorm) };
        }

        historyOverPath.startNewSubPath (start);
        historyOverPath.lineTo (end);

        juce::Path region;
        region.startNewSubPath ({ start.x, targetLinePosition });
        region.lineTo (start);
        region.lineTo (end);
        region.lineTo ({ end.x, targetLinePosition });
        region.closeSubPath();
        historyOverFillPath.addPath (region);
    }

    historyOverHasData = ! historyOverPath.isEmpty();
}

float LoudnessMeter::clampDisplayLoudness (float value) noexcept
{
    if (value <= -95.0f)
        return -95.0f;
    return juce::jlimit (-60.0f, 3.0f, value);
}

StereoMeter::StereoMeter()
    : MeterComponent ("Stereo Field")
{
    viewModeBox.addItem ("Mid / Side", (int) PlotMode::midSide);
    viewModeBox.addItem ("Left / Right", (int) PlotMode::leftRight);
    viewModeBox.setJustificationType (juce::Justification::centredLeft);
    viewModeBox.onChange = [this]
    {
        state.viewMode = viewModeBox.getSelectedId();
        plotMode = static_cast<PlotMode> (state.viewMode);
        notifyStateChanged();
        repaint();
    };
    viewModeBox.setSelectedId (state.viewMode, juce::dontSendNotification);

    freezeButton.setClickingTogglesState (true);
    freezeButton.onClick = [this]
    {
        freezeDisplay = freezeButton.getToggleState();
        state.freeze = freezeDisplay;
        notifyStateChanged();
        updateControlColours();
    };

    dotsButton.setClickingTogglesState (true);
    dotsButton.onClick = [this]
    {
        const bool enabled = dotsButton.getToggleState();
        const auto mode = enabled ? DisplayMode::dots : DisplayMode::lines;
        applyDisplayMode (mode, true, true, true);
    };

    persistenceButton.setClickingTogglesState (true);
    persistenceButton.onClick = [this]
    {
        const bool enabled = persistenceButton.getToggleState();
        const auto mode = enabled ? DisplayMode::persistence : DisplayMode::lines;
        applyDisplayMode (mode, true, true, true);
    };

    scopeScaleLabel.setText ("Scale", juce::dontSendNotification);
    scopeScaleLabel.setJustificationType (juce::Justification::centredLeft);
    scopeScaleLabel.setInterceptsMouseClicks (false, false);
    scopeScaleLabel.setMinimumHorizontalScale (0.8f);

    scopeScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    scopeScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
    scopeScaleSlider.setRange (0.5, 2.0, 0.01);
    scopeScaleSlider.setSkewFactorFromMidPoint (1.0);
    scopeScaleSlider.setNumDecimalPlacesToDisplay (2);
    scopeScaleSlider.setTextValueSuffix ("x");
    scopeScaleSlider.setValue (state.scopeScale, juce::dontSendNotification);
    scopeScaleSlider.onValueChange = [this]
    {
        const float value = (float) scopeScaleSlider.getValue();
        if (std::abs (scopeScale - value) <= 0.001f)
            return;

        scopeScale = value;
        state.scopeScale = value;
        handleScopeScaleChanged();
    };

    trailDecayLabel.setText ("Trail", juce::dontSendNotification);
    trailDecayLabel.setJustificationType (juce::Justification::centredLeft);
    trailDecayLabel.setInterceptsMouseClicks (false, false);
    trailDecayLabel.setMinimumHorizontalScale (0.8f);

    trailDecaySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    trailDecaySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    trailDecaySlider.setRange (0.2, 3.0, 0.05);
    trailDecaySlider.setValue (state.trailSeconds, juce::dontSendNotification);
    trailDecaySlider.setTextValueSuffix (" s");
    trailDecaySlider.onValueChange = [this]
    {
        const float seconds = (float) trailDecaySlider.getValue();
        if (std::abs (trailSeconds - seconds) <= 0.01f)
            return;

        trailSeconds = seconds;
        state.trailSeconds = seconds;
        updateTrailSettings();
        notifyStateChanged();
    };

    for (int seconds : { 3, 6, 12, 24 })
        historyBox.addItem (juce::String (seconds) + " s", seconds);
    historyBox.setJustificationType (juce::Justification::centredLeft);
    historyBox.onChange = [this]
    {
        handleHistorySelectionChanged();
        repaint();
    };
    historyBox.setSelectedId (state.historySeconds, juce::dontSendNotification);

    addAndMakeVisible (viewModeBox);
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (dotsButton);
    addAndMakeVisible (persistenceButton);
    addAndMakeVisible (scopeScaleLabel);
    addAndMakeVisible (scopeScaleSlider);
    addAndMakeVisible (trailDecayLabel);
    addAndMakeVisible (trailDecaySlider);
    addAndMakeVisible (historyBox);

    refreshHistoryCapacity();
    updateTrailSettings();
    updateControlColours();
    updateTrailComponents();
}

void StereoMeter::setState (const State& newState, bool force)
{
    const auto floatsDiffer = [] (float lhs, float rhs, float tolerance)
    {
        return std::abs (lhs - rhs) > tolerance;
    };

    const bool changed = force
                         || state.viewMode != newState.viewMode
                         || floatsDiffer (state.scopeScale, newState.scopeScale, 1.0e-3f)
                         || state.historySeconds != newState.historySeconds
                         || state.freeze != newState.freeze
                         || state.showDots != newState.showDots
                         || state.persistence != newState.persistence
                         || state.displayMode != newState.displayMode
                         || floatsDiffer (state.trailSeconds, newState.trailSeconds, 0.01f);

    state = newState;

    viewModeBox.setSelectedId (state.viewMode, juce::dontSendNotification);
    plotMode = static_cast<PlotMode> (state.viewMode);
    freezeButton.setToggleState (state.freeze, juce::dontSendNotification);
    freezeDisplay = state.freeze;
    trailDecaySlider.setValue (state.trailSeconds, juce::dontSendNotification);
    trailSeconds = state.trailSeconds;
    applyDisplayMode (static_cast<DisplayMode> (juce::jlimit (1, 3, state.displayMode)), false, changed, true);
    scopeScaleSlider.setValue (state.scopeScale, juce::dontSendNotification);
    scopeScale = state.scopeScale;
    historyBox.setSelectedId (state.historySeconds, juce::dontSendNotification);
    historySeconds = state.historySeconds;

    updateTrailSettings();
    refreshHistoryCapacity();
    rebuildPaths();

    if (changed)
        repaint();
}

void StereoMeter::setOnStateChanged (std::function<void (const State&)> callback)
{
    onStateChanged = std::move (callback);
}

void StereoMeter::notifyStateChanged()
{
    if (onStateChanged != nullptr)
        onStateChanged (state);
}

void StereoMeter::refreshHistoryCapacity()
{
    historySeconds = juce::jlimit (1, 60, historySeconds);
    const int newCapacity = juce::jmax (2, (int) std::round ((double) historySeconds * 30.0));

    if ((int) correlationHistory.size() < newCapacity)
        correlationHistory.resize ((size_t) newCapacity, 0.0f);

    correlationHistoryCapacity = newCapacity;
    correlationHistoryWrite = correlationHistoryCapacity > 0 ? correlationHistoryWrite % correlationHistoryCapacity : 0;
    correlationHistoryFilled = juce::jmin (correlationHistoryFilled, correlationHistoryCapacity);
}

void StereoMeter::handleHistorySelectionChanged()
{
    const int selection = historyBox.getSelectedId();
    if (selection <= 0)
        return;

    state.historySeconds = selection;
    historySeconds = selection;
    refreshHistoryCapacity();
    notifyStateChanged();
}

void StereoMeter::handleScopeScaleChanged()
{
    trailFrames.clear();
    rebuildPaths();
    notifyStateChanged();
    repaint();
}

void StereoMeter::pushTrailFrame()
{
    if (displayMode != DisplayMode::persistence || ! hasData)
        return;

    TrailFrame frame;
    frame.midSide = lissajousMidSide;
    frame.leftRight = lissajousLeftRight;
    frame.alpha = 1.0f;
    trailFrames.push_back (std::move (frame));

    const int limit = juce::jmax (2, maxTrailFrames);
    while ((int) trailFrames.size() > limit)
        trailFrames.pop_front();
}

void StereoMeter::decayTrailFrames()
{
    if (trailDecayFrames <= 0)
        return;

    const float decrement = 1.0f / (float) trailDecayFrames;
    for (auto& frame : trailFrames)
        frame.alpha = juce::jmax (0.0f, frame.alpha - decrement);

    while (! trailFrames.empty() && trailFrames.front().alpha <= 0.02f)
        trailFrames.pop_front();
}

void StereoMeter::applyDisplayMode (DisplayMode mode, bool notifyState, bool forceRepaint, bool updateButtons)
{
    const auto clamped = (mode == DisplayMode::dots || mode == DisplayMode::persistence) ? mode : DisplayMode::lines;
    const bool modeChanged = (clamped != displayMode);

    displayMode = clamped;
    showDots = (displayMode == DisplayMode::dots);
    persistenceEnabled = (displayMode == DisplayMode::persistence);
    state.displayMode = (int) displayMode;
    state.showDots = showDots;
    state.persistence = persistenceEnabled;

    if (modeChanged && ! persistenceEnabled)
        trailFrames.clear();

    if (updateButtons)
    {
        dotsButton.setToggleState (displayMode == DisplayMode::dots, juce::dontSendNotification);
        persistenceButton.setToggleState (displayMode == DisplayMode::persistence, juce::dontSendNotification);
    }

    updateTrailComponents();
    updateControlColours();

    if (notifyState && modeChanged)
        notifyStateChanged();

    if (forceRepaint || modeChanged)
        repaint();
}

void StereoMeter::updateTrailSettings()
{
    trailSeconds = juce::jlimit (0.2f, 3.0f, trailSeconds);
    trailDecayFrames = juce::jmax (1, (int) std::round (trailSeconds * 30.0f));
    maxTrailFrames = juce::jlimit (trailDecayFrames + 2, trailDecayFrames + 12, 120);
    while ((int) trailFrames.size() > maxTrailFrames)
        trailFrames.pop_front();
}

void StereoMeter::updateTrailComponents()
{
    const bool enabled = (displayMode == DisplayMode::persistence);
    trailDecayLabel.setEnabled (enabled);
    trailDecaySlider.setEnabled (enabled);
    trailDecayLabel.setAlpha (enabled ? 1.0f : 0.45f);
    trailDecaySlider.setAlpha (enabled ? 1.0f : 0.55f);
}

juce::Colour StereoMeter::getCorrelationColour() const noexcept
{
    const float amount = juce::jlimit (0.0f, 1.0f, std::abs (correlation));
    if (correlation < 0.0f)
        return theme.warning.interpolatedWith (theme.text, 1.0f - amount);

    return theme.secondary.interpolatedWith (theme.text, 1.0f - amount);
}

void StereoMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
        updateControlColours();

    transport = snapshot.transport;

    plotMode = static_cast<PlotMode> (viewModeBox.getSelectedId() > 0 ? viewModeBox.getSelectedId() : (int) plotMode);
    freezeDisplay = freezeButton.getToggleState();
    scopeScale = (float) scopeScaleSlider.getValue();
    historySeconds = historyBox.getSelectedId() > 0 ? historyBox.getSelectedId() : historySeconds;
    displayMode = static_cast<DisplayMode> (juce::jlimit (1, 3, state.displayMode));
    showDots = (displayMode == DisplayMode::dots);
    persistenceEnabled = (displayMode == DisplayMode::persistence);
    trailSeconds = state.trailSeconds;
    updateTrailComponents();

    if (freezeDisplay && hasData)
    {
        decayTrailFrames();
        repaint();
        return;
    }

    correlation = snapshot.correlation;
    width = snapshot.stereoWidth;
    leftLevel = snapshot.leftRms;
    rightLevel = snapshot.rightRms;
    midLevel = snapshot.midRms;
    sideLevel = snapshot.sideRms;
    balanceDb = snapshot.balanceDb;
    sideToMidRatio = (midLevel > 1.0e-6f) ? juce::jlimit (0.0f, 3.0f, sideLevel / juce::jmax (midLevel, 1.0e-6f)) : 0.0f;

    rawLissajous.clear();
    rawLissajous.reserve ((size_t) snapshot.lissajousCount);
    for (int i = 0; i < snapshot.lissajousCount; ++i)
        rawLissajous.push_back (snapshot.lissajous[(size_t) i]);

    hasData = ! rawLissajous.empty();
    rebuildPaths();
    pushCorrelationHistory (correlation);

    if (displayMode == DisplayMode::persistence && hasData)
        pushTrailFrame();
    else
        trailFrames.clear();

    if (! trailFrames.empty())
        decayTrailFrames();

    repaint();
}

void StereoMeter::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto content = drawPanelHeader (g, panel);
    auto layout = computeStereoMeterLayout (content);

    const auto drawSection = [&g, this] (juce::Rectangle<float> bounds, float radius)
    {
        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        g.setColour (theme.background.darker (0.28f));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (theme.outline.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    };

    if (! layout.optionsBackground.isEmpty())
        drawSection (layout.optionsBackground, 10.0f);

    const auto levelsContainer = layout.levelsBounds.expanded (6.0f, 6.0f);
    if (levelsContainer.getWidth() > 0.0f && levelsContainer.getHeight() > 0.0f)
        drawSection (levelsContainer, 10.0f);

    const auto balanceContainer = layout.balanceBounds.expanded (6.0f, 6.0f);
    if (balanceContainer.getWidth() > 0.0f && balanceContainer.getHeight() > 0.0f)
        drawSection (balanceContainer, 8.0f);

    const auto historyContainer = layout.historyBounds.expanded (10.0f, 6.0f);
    if (historyContainer.getWidth() > 0.0f && historyContainer.getHeight() > 0.0f)
        drawSection (historyContainer, 10.0f);

    if (! layout.scopeBounds.isEmpty())
    {
        auto plotArea = layout.scopeBounds;
        g.setColour (theme.background.darker (0.25f));
        g.fillRoundedRectangle (plotArea, 12.0f);
        g.setColour (theme.outline.withAlpha (0.7f));
        g.drawRoundedRectangle (plotArea, 12.0f, 1.2f);

        auto scopeBounds = plotArea.reduced (14.0f);
        const float size = juce::jmin (scopeBounds.getWidth(), scopeBounds.getHeight());
        scopeBounds = scopeBounds.withSizeKeepingCentre (size, size);

        g.setColour (theme.outline.withAlpha (0.35f));
        g.drawRoundedRectangle (scopeBounds, 8.0f, 1.0f);

        const auto centre = scopeBounds.getCentre();
        g.setColour (theme.outline.withAlpha (0.3f));
        g.drawLine (scopeBounds.getX(), centre.y, scopeBounds.getRight(), centre.y, 1.0f);
        g.drawLine (centre.x, scopeBounds.getY(), centre.x, scopeBounds.getBottom(), 1.0f);

        juce::Path diagonals;
        diagonals.addLineSegment ({ scopeBounds.getTopLeft(), scopeBounds.getBottomRight() }, 1.0f);
        diagonals.addLineSegment ({ scopeBounds.getBottomLeft(), scopeBounds.getTopRight() }, 1.0f);
        g.setColour (theme.outline.withAlpha (0.18f));
        g.strokePath (diagonals, juce::PathStrokeType (0.9f));

        auto circleBounds = scopeBounds.withSizeKeepingCentre (juce::jmin (scopeBounds.getWidth(), scopeBounds.getHeight()),
                                                               juce::jmin (scopeBounds.getWidth(), scopeBounds.getHeight()));
        g.setColour (theme.outline.withAlpha (0.24f));
        g.drawEllipse (circleBounds, 0.8f);

        const float widthNorm = juce::jlimit (0.0f, 1.0f, width);
        if (widthNorm > 0.0f)
        {
            auto shade = scopeBounds;
            const float halfWidth = scopeBounds.getWidth() * 0.5f * widthNorm;
            shade.setLeft (centre.x - halfWidth);
            shade.setRight (centre.x + halfWidth);
            g.setColour (theme.secondary.withAlpha (0.08f));
            g.fillRect (shade);
        }

        if (! trailFrames.empty() && displayMode == DisplayMode::persistence)
        {
            const auto trailColour = getCorrelationColour();
            for (const auto& frame : trailFrames)
            {
                if (frame.alpha <= 0.0f)
                    continue;

                auto path = createTransformedPath (plotMode == PlotMode::midSide ? frame.midSide : frame.leftRight, scopeBounds);
                g.setColour (trailColour.withAlpha (0.25f * frame.alpha));
                g.strokePath (path, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        if (hasData)
        {
            const auto baseColour = getCorrelationColour();
            auto path = createTransformedPath (plotMode == PlotMode::midSide ? lissajousMidSide : lissajousLeftRight,
                                              scopeBounds);
            if (displayMode != DisplayMode::dots)
            {
                g.setColour (baseColour.withAlpha (displayMode == DisplayMode::persistence ? 0.7f : 0.85f));
                g.strokePath (path, juce::PathStrokeType (displayMode == DisplayMode::persistence ? 1.4f : 1.6f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }

            if (showDots || displayMode == DisplayMode::persistence)
            {
                const auto& points = (plotMode == PlotMode::midSide) ? pointsMidSide : pointsLeftRight;
                const float dotSize = (displayMode == DisplayMode::dots) ? 3.4f : 2.4f;
                const float radius = dotSize * 0.5f;
                const auto dotColour = baseColour.withAlpha (displayMode == DisplayMode::dots ? 0.85f : 0.55f);
                g.setColour (dotColour);
                for (const auto& p : points)
                {
                    const float x = scopeBounds.getX() + scopeBounds.getWidth() * p.x;
                    const float y = scopeBounds.getY() + scopeBounds.getHeight() * p.y;
                    g.fillEllipse (x - radius, y - radius, dotSize, dotSize);
                }
            }
        }
        else
        {
            g.setColour (theme.text.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawFittedText (freezeDisplay ? "Frozen" : "Waiting for stereo data...", scopeBounds.toNearestInt(),
                              juce::Justification::centred, 1);
        }
    }

    auto metricsArea = layout.metricsBounds;
    if (metricsArea.getWidth() > 0.0f)
    {
        juce::String metricsText;
        metricsText << juce::String::formatted ("Correlation %0.2f    Width %0.0f%%    Side/Mid %0.2f",
                                                correlation, juce::jlimit (0.0f, 1.0f, width) * 100.0f, sideToMidRatio);
        metricsText << "    Mode " << (plotMode == PlotMode::midSide ? "Mid/Side" : "Left/Right");
        const char* displayModeName = "Lines";
        if (displayMode == DisplayMode::dots)
            displayModeName = "Dots";
        else if (displayMode == DisplayMode::persistence)
            displayModeName = "Trail";
        metricsText << "    Display " << displayModeName;
        if (displayMode == DisplayMode::persistence)
            metricsText << juce::String::formatted (" (%0.1f s)", trailSeconds);

        g.setColour (getCorrelationColour().withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText (metricsText, metricsArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    auto historyArea = layout.historyBounds;
    if (historyArea.getHeight() > 4.0f)
    {
        const auto toY = [historyArea] (float value)
        {
            const float clamped = juce::jlimit (-1.0f, 1.0f, value);
            return juce::jmap (clamped, 1.0f, -1.0f, historyArea.getY(), historyArea.getBottom());
        };

        const int framesAvailable = juce::jmin (correlationHistoryFilled, correlationHistoryCapacity);
        const float filledSeconds = framesAvailable > 0 ? (float) framesAvailable / 30.0f : 0.0f;
        const float percent = historySeconds > 0 ? juce::jlimit (0.0f, 100.0f, (filledSeconds / (float) historySeconds) * 100.0f)
                                                 : 0.0f;

        if (filledSeconds > 0.0f && shouldDrawBeatGrid (transport, freezeDisplay))
        {
            const auto beatLines = createBeatGridLines (transport, filledSeconds);
            if (! beatLines.empty())
            {
                for (const auto& line : beatLines)
                {
                    const double timeFromStart = (double) filledSeconds - line.timeFromNow;
                    if (timeFromStart < 0.0 || timeFromStart > (double) filledSeconds)
                        continue;

                    const float norm = (float) juce::jlimit (0.0, 1.0, timeFromStart / juce::jmax (1.0e-6, (double) filledSeconds));
                    const float x = historyArea.getX() + historyArea.getWidth() * norm;
                    const float alpha = line.isBarStart ? 0.45f : 0.28f;
                    g.setColour (theme.outline.withAlpha (alpha));
                    g.drawLine (x, historyArea.getY(), x, historyArea.getBottom(), line.isBarStart ? 1.0f : 0.6f);

                    if (line.isBarStart)
                    {
                        g.setColour (theme.text.withAlpha (0.6f));
                        g.setFont (juce::Font (juce::FontOptions (10.0f)));
                        g.drawFittedText (beatLabelForLine (line),
                                          juce::Rectangle<int> ((int) x + 4, (int) historyArea.getY() + 2, 48, 12),
                                          juce::Justification::centredLeft, 1);
                    }
                }

                g.setColour (theme.outline.withAlpha (0.6f));
            }
        }

        if (framesAvailable > 1)
        {
            juce::Path historyPath;
            juce::Path negativeFill;
            const int start = (correlationHistoryWrite - framesAvailable + correlationHistoryCapacity + correlationHistoryCapacity)
                              % correlationHistoryCapacity;
            const float zeroY = toY (0.0f);
            bool inNegative = false;
            float previousValue = 0.0f;
            float previousX = historyArea.getX();

            for (int i = 0; i < framesAvailable; ++i)
            {
                const int idx = (start + i) % correlationHistoryCapacity;
                const float value = correlationHistory[(size_t) idx];
                const float normX = framesAvailable > 1 ? (float) i / (float) (framesAvailable - 1) : 0.0f;
                const float x = historyArea.getX() + historyArea.getWidth() * normX;
                const float y = toY (value);

                if (i == 0)
                    historyPath.startNewSubPath (x, y);
                else
                    historyPath.lineTo (x, y);

                const bool isNegative = value < 0.0f;
                if (isNegative)
                {
                    if (! inNegative)
                    {
                        negativeFill.startNewSubPath (x, zeroY);
                        negativeFill.lineTo (x, y);
                        inNegative = true;
                    }
                    else
                    {
                        negativeFill.lineTo (x, y);
                    }
                }
                else if (inNegative)
                {
                    const float denom = value - previousValue;
                    float crossX = x;
                    if (std::abs (denom) > 1.0e-6f)
                    {
                        const float t = juce::jlimit (0.0f, 1.0f, -previousValue / denom);
                        crossX = previousX + (x - previousX) * t;
                    }
                    negativeFill.lineTo (crossX, zeroY);
                    negativeFill.closeSubPath();
                    inNegative = false;
                }

                previousValue = value;
                previousX = x;
            }

            if (inNegative)
            {
                negativeFill.lineTo (previousX, zeroY);
                negativeFill.closeSubPath();
            }

            if (! negativeFill.isEmpty())
            {
                g.setColour (theme.warning.withAlpha (0.25f));
                g.fillPath (negativeFill);
            }

            g.setColour (getCorrelationColour().withAlpha (0.85f));
            g.strokePath (historyPath, juce::PathStrokeType (1.3f));
        }

        g.setColour (theme.outline.withAlpha (0.4f));
        const float zeroY = toY (0.0f);
        g.drawLine (historyArea.getX(), zeroY, historyArea.getRight(), zeroY, 1.0f);

        g.setColour (theme.text.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("+1", historyArea.withWidth (28.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.drawText ("0", historyArea.withTrimmedLeft (historyArea.getWidth() * 0.45f)
                                       .withTrimmedRight (historyArea.getWidth() * 0.45f)
                                       .toNearestInt(), juce::Justification::centred, 1);
        g.drawText ("-1", historyArea.withWidth (28.0f).withX (historyArea.getRight() - 28.0f).toNearestInt(),
                    juce::Justification::centredRight, 1);

        g.setColour (theme.text.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        juce::String historyText = juce::String::formatted ("History %0.1f s / %d s", filledSeconds, historySeconds);
        historyText << juce::String::formatted (" (%0.0f%%)", percent);
        g.drawFittedText (historyText, historyArea.toNearestInt(), juce::Justification::centred, 1);
    }

    auto levelsArea = layout.levelsBounds;
    if (levelsArea.getWidth() > 0.0f && levelsArea.getHeight() > 0.0f)
    {
        struct LevelInfo
        {
            float value = 0.0f;
            juce::Colour colour;
            const char* label = nullptr;
        };

        const LevelInfo infos[]
        {
            { leftLevel,  theme.primary,   "LEFT" },
            { rightLevel, theme.tertiary,  "RIGHT" },
            { midLevel,   theme.secondary, "MID" },
            { sideLevel,  theme.warning,   "SIDE" }
        };

        const float columnWidth = levelsArea.getWidth() / 4.0f;
        const float barWidth = juce::jmin (22.0f, columnWidth * 0.45f);
        const float radius = juce::jmin (barWidth * 0.5f, 6.0f);

        for (int i = 0; i < 4; ++i)
        {
            auto column = juce::Rectangle<float> (levelsArea.getX() + columnWidth * (float) i,
                                                  levelsArea.getY(), columnWidth, levelsArea.getHeight());
            const float centreX = column.getCentreX();
            auto valueArea = column.removeFromTop (18.0f);
            auto labelArea = column.removeFromBottom (18.0f);
            auto barArea = column.reduced ((column.getWidth() - barWidth) * 0.5f, 6.0f);
            barArea.setWidth (barWidth);
            barArea.setCentre ({ centreX, barArea.getCentreY() });

            const auto& info = infos[i];
            const float clamped = juce::jlimit (0.0f, 1.0f, info.value);
            auto fill = barArea;
            fill.setHeight (barArea.getHeight() * clamped);
            fill.setY (barArea.getBottom() - fill.getHeight());

            g.setColour (info.colour.withAlpha (0.7f));
            g.fillRoundedRectangle (fill, radius);

            g.setColour (theme.outline.withAlpha (0.45f));
            g.drawRoundedRectangle (barArea, radius, 1.0f);

            g.setColour (theme.text.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (juce::String::formatted ("%0.1f",
                                                       juce::Decibels::gainToDecibels (juce::jmax (info.value, 1.0e-6f), -80.0f)),
                              valueArea.toNearestInt(), juce::Justification::centred, 1);

            g.setColour (theme.text.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawFittedText (info.label, labelArea.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    auto balanceArea = layout.balanceBounds;
    if (balanceArea.getWidth() > 0.0f && balanceArea.getHeight() > 0.0f)
    {
        g.setColour (theme.outline.withAlpha (0.45f));
        g.drawRoundedRectangle (balanceArea, 4.0f, 1.0f);

        const float norm = juce::jlimit (-1.0f, 1.0f, balanceDb / 24.0f);
        if (std::abs (norm) > 0.01f)
        {
            auto fill = balanceArea;
            const float halfWidth = balanceArea.getWidth() * 0.5f * std::abs (norm);
            fill.setWidth (halfWidth);
            if (norm >= 0.0f)
                fill.setX (balanceArea.getCentreX());
            else
                fill.setX (balanceArea.getCentreX() - halfWidth);

            g.setColour ((norm >= 0.0f ? theme.secondary : theme.warning).withAlpha (0.65f));
            g.fillRect (fill);
        }

        g.setColour (theme.outline.withAlpha (0.45f));
        g.drawLine (balanceArea.getCentreX(), balanceArea.getY(), balanceArea.getCentreX(), balanceArea.getBottom(), 1.0f);

        auto scaleArea = balanceArea.reduced (6.0f, 0.0f);
        g.setColour (theme.text.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("-24 dB", scaleArea.removeFromLeft (52).toNearestInt(), juce::Justification::centredLeft, true);
        g.drawText ("+24 dB", scaleArea.removeFromRight (52).toNearestInt(), juce::Justification::centredRight, true);

        g.setColour (theme.text.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (juce::String::formatted ("Balance %+.1f dB", balanceDb), balanceArea.toNearestInt(),
                    juce::Justification::centred, 1);
    }
}
void StereoMeter::resized()
{
    auto layout = computeStereoMeterLayout (getPanelContentBounds());
    auto controls = layout.controlsBounds.toNearestInt();

    auto clearBounds = [this]()
    {
        const auto empty = juce::Rectangle<int>();
        viewModeBox.setBounds (empty);
        freezeButton.setBounds (empty);
        dotsButton.setBounds (empty);
        persistenceButton.setBounds (empty);
        scopeScaleLabel.setBounds (empty);
        scopeScaleSlider.setBounds (empty);
        trailDecayLabel.setBounds (empty);
        trailDecaySlider.setBounds (empty);
        historyBox.setBounds (empty);
    };

    if (controls.isEmpty())
    {
        clearBounds();
        return;
    }

    auto area = controls;
    const int rowSpacing = 6;

    const auto takeRow = [&area] (int height, int spacing)
    {
        height = juce::jmax (0, juce::jmin (height, area.getHeight()));
        auto row = area.removeFromTop (height);
        if (area.getHeight() > 0)
            area.removeFromTop (spacing);
        return row;
    };

    viewModeBox.setBounds (takeRow (28, rowSpacing).reduced (0, 2));

    auto toggleRow = takeRow (28, rowSpacing).reduced (0, 2);
    std::array<juce::ToggleButton*, 3> toggles { &freezeButton, &dotsButton, &persistenceButton };
    const int toggleCount = (int) toggles.size();
    const int toggleSpacing = 6;
    const int toggleWidth = toggleCount > 0
                                 ? juce::jmax (70, (toggleRow.getWidth() - toggleSpacing * (toggleCount - 1))
                                                       / juce::jmax (1, toggleCount))
                                 : toggleRow.getWidth();
    auto toggleBounds = toggleRow;
    for (auto* toggle : toggles)
    {
        const int buttonWidth = juce::jmin (toggleWidth, toggleBounds.getWidth());
        auto bounds = toggleBounds.removeFromLeft (buttonWidth);
        toggle->setBounds (bounds);
        if (toggleBounds.getWidth() > 0)
            toggleBounds.removeFromLeft (toggleSpacing);
    }

    auto scaleRow = takeRow (36, rowSpacing).reduced (0, 2);
    const int scaleLabelWidth = juce::jmin (80, scaleRow.getWidth() / 3);
    scopeScaleLabel.setBounds (scaleRow.removeFromLeft (scaleLabelWidth));
    scopeScaleSlider.setBounds (scaleRow);

    auto trailRow = takeRow (36, rowSpacing).reduced (0, 2);
    const int trailLabelWidth = juce::jmin (80, trailRow.getWidth() / 3);
    trailDecayLabel.setBounds (trailRow.removeFromLeft (trailLabelWidth));
    trailDecaySlider.setBounds (trailRow);

    historyBox.setBounds (takeRow (28, rowSpacing).reduced (0, 2));
}
void StereoMeter::rebuildPaths()
{
    lissajousMidSide.clear();
    lissajousLeftRight.clear();
    pointsMidSide.clear();
    pointsLeftRight.clear();

    if (rawLissajous.empty())
        return;

    const auto build = [this] (PlotMode mode, juce::Path& destPath, std::vector<juce::Point<float>>& destPoints)
    {
        destPath.clear();
        destPoints.clear();
        destPoints.reserve (rawLissajous.size());

        const auto convert = [mode, scale = scopeScale] (juce::Point<float> sample)
        {
            float x = sample.x;
            float y = sample.y;

            if (mode == PlotMode::midSide)
            {
                constexpr float invSqrt2 = juce::MathConstants<float>::sqrt2 * 0.5f;
                const float mid = (sample.x + sample.y) * invSqrt2;
                const float side = (sample.x - sample.y) * invSqrt2;

                x = side;
                y = mid;
            }

            x = juce::jlimit (-1.1f, 1.1f, x * scale);
            y = juce::jlimit (-1.1f, 1.1f, y * scale);

            const float normX = juce::jlimit (0.0f, 1.0f, (x + 1.0f) * 0.5f);
            const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - ((y + 1.0f) * 0.5f));
            return juce::Point<float> { normX, normY };
        };

        auto first = convert (rawLissajous.front());
        destPath.startNewSubPath (first);
        destPoints.push_back (first);

        for (size_t i = 1; i < rawLissajous.size(); ++i)
        {
            auto point = convert (rawLissajous[i]);
            destPath.lineTo (point);
            destPoints.push_back (point);
        }
    };

    build (PlotMode::midSide, lissajousMidSide, pointsMidSide);
    build (PlotMode::leftRight, lissajousLeftRight, pointsLeftRight);
}

void StereoMeter::updateControlColours()
{
    const auto textColour = theme.text.withAlpha (0.75f);
    viewModeBox.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.25f));
    viewModeBox.setColour (juce::ComboBox::textColourId, textColour);
    viewModeBox.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.7f));
    viewModeBox.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
    viewModeBox.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.85f));

    historyBox.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.25f));
    historyBox.setColour (juce::ComboBox::textColourId, textColour);
    historyBox.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.6f));
    historyBox.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
    historyBox.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.85f));

    const auto setToggleColours = [textColour, this] (juce::ToggleButton& button)
    {
        button.setColour (juce::ToggleButton::textColourId, textColour);
        button.setColour (juce::ToggleButton::tickColourId, theme.secondary);
        button.setColour (juce::ToggleButton::tickDisabledColourId, theme.outline);
    };

    setToggleColours (freezeButton);
    setToggleColours (dotsButton);
    setToggleColours (persistenceButton);

    scopeScaleLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.7f));
    scopeScaleSlider.setColour (juce::Slider::thumbColourId, theme.secondary);
    scopeScaleSlider.setColour (juce::Slider::trackColourId, theme.secondary.withAlpha (0.35f));
    scopeScaleSlider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.85f));
    scopeScaleSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    scopeScaleSlider.setColour (juce::Slider::textBoxBackgroundColourId, theme.background.darker (0.32f));

    const auto highlight = theme.secondary.brighter (0.18f);
    freezeButton.setColour (juce::ToggleButton::textColourId,
                            freezeButton.getToggleState() ? highlight : textColour);
    dotsButton.setColour (juce::ToggleButton::textColourId,
                          (displayMode == DisplayMode::dots) ? highlight : textColour);
    persistenceButton.setColour (juce::ToggleButton::textColourId,
                                 (displayMode == DisplayMode::persistence) ? highlight : textColour);

    trailDecayLabel.setColour (juce::Label::textColourId,
                               displayMode == DisplayMode::persistence ? theme.text.withAlpha (0.7f)
                                                                        : theme.text.withAlpha (0.45f));
    trailDecaySlider.setColour (juce::Slider::thumbColourId, theme.secondary);
    trailDecaySlider.setColour (juce::Slider::trackColourId, theme.secondary.withAlpha (0.3f));
    trailDecaySlider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.85f));
    trailDecaySlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    trailDecaySlider.setColour (juce::Slider::textBoxBackgroundColourId, theme.background.darker (0.32f));
}

void StereoMeter::pushCorrelationHistory (float value) noexcept
{
    if (correlationHistoryCapacity <= 0)
        return;

    if ((int) correlationHistory.size() < correlationHistoryCapacity)
        correlationHistory.resize ((size_t) correlationHistoryCapacity, 0.0f);

    correlationHistory[(size_t) correlationHistoryWrite] = juce::jlimit (-1.0f, 1.0f, value);
    correlationHistoryWrite = (correlationHistoryWrite + 1) % correlationHistoryCapacity;
    correlationHistoryFilled = juce::jmin (correlationHistoryCapacity, correlationHistoryFilled + 1);
}

juce::Path StereoMeter::createTransformedPath (const juce::Path& source, juce::Rectangle<float> bounds) const
{
    juce::Path transformed = source;
    transformed.applyTransform (juce::AffineTransform::scale (bounds.getWidth(), bounds.getHeight())
                                                   .followedBy (juce::AffineTransform::translation (bounds.getX(), bounds.getY())));
    return transformed;
}

OscilloscopeMeter::OscilloscopeMeter()
    : MeterComponent ("Oscilloscope")
{
    auto configureCombo = [this] (juce::ComboBox& box, const std::vector<std::pair<int, juce::String>>& items)
    {
        addAndMakeVisible (box);
        box.setJustificationType (juce::Justification::centredLeft);
        for (const auto& entry : items)
            box.addItem (entry.second, entry.first);
    };

    configureCombo (triggerBox,
                    { { (int) TriggerMode::free,    "Free" },
                      { (int) TriggerMode::rising,  "Rising" },
                      { (int) TriggerMode::falling, "Falling" } });
    triggerBox.setSelectedId ((int) triggerMode, juce::dontSendNotification);
    triggerBox.onChange = [this]
    {
        const int selected = triggerBox.getSelectedId();
        if (selected > 0)
            triggerMode = static_cast<TriggerMode> (selected);
        rebuildPath();
        updateStatus();
        repaint();
    };

    configureCombo (timeBaseBox,
                    { { 1, "Wide" },
                      { 2, "Normal" },
                      { 3, "Zoom 2x" },
                      { 4, "Zoom 4x" },
                      { 5, "Zoom 8x" } });
    timeBaseBox.setSelectedId (2, juce::dontSendNotification);
    timeBaseRatio = 1.0f;
    timeBaseBox.onChange = [this]
    {
        switch (timeBaseBox.getSelectedId())
        {
            case 1: timeBaseRatio = 0.5f; break;
            case 2: timeBaseRatio = 1.0f; break;
            case 3: timeBaseRatio = 2.0f; break;
            case 4: timeBaseRatio = 4.0f; break;
            case 5: timeBaseRatio = 8.0f; break;
            default: timeBaseRatio = 1.0f; break;
        }

        rebuildPath();
        updateStatus();
        repaint();
    };

    configureCombo (windowBox,
                    { { (int) WindowMode::rectangular, "Rect" },
                      { (int) WindowMode::hann,        "Hann" },
                      { (int) WindowMode::blackman,    "Blackman" } });
    windowBox.setSelectedId ((int) windowMode, juce::dontSendNotification);
    windowBox.onChange = [this]
    {
        const int selected = windowBox.getSelectedId();
        if (selected > 0)
            windowMode = static_cast<WindowMode> (selected);
        rebuildPath();
        repaint();
    };

    auto configureToggle = [this] (juce::ToggleButton& button, bool initial, std::function<void()> callback)
    {
        addAndMakeVisible (button);
        button.setToggleState (initial, juce::dontSendNotification);
        button.onClick = [this, callback]
        {
            callback();
            updateStatus();
            repaint();
        };
    };

    configureToggle (freezeButton, freezeEnabled, [this]
    {
        freezeEnabled = freezeButton.getToggleState();
    });

    configureToggle (persistenceButton, persistenceEnabled, [this]
    {
        persistenceEnabled = persistenceButton.getToggleState();
        if (! persistenceEnabled)
        {
            persistencePath.clear();
            persistenceSamples.clear();
        }
        else
        {
            rebuildPath();
        }
    });

    configureToggle (gridButton, gridEnabled, [this]
    {
        gridEnabled = gridButton.getToggleState();
    });

    configureToggle (fillButton, fillEnabled, [this]
    {
        fillEnabled = fillButton.getToggleState();
    });

    configureToggle (smoothButton, smoothingEnabled, [this]
    {
        smoothingEnabled = smoothButton.getToggleState();
        rebuildPath();
    });

    auto configureSlider = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 20);
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
    };

    configureSlider (triggerSlider, triggerLabel, "Trigger");
    triggerSlider.setRange (-1.0, 1.0, 0.001);
    triggerSlider.onValueChange = [this]
    {
        triggerLevel = (float) triggerSlider.getValue();
        rebuildPath();
        updateStatus();
        repaint();
    };

    configureSlider (gainSlider, gainLabel, "Gain");
    gainSlider.setRange (0.25, 8.0, 0.01);
    gainSlider.setSkewFactorFromMidPoint (1.0);
    gainSlider.setValue (verticalGain, juce::dontSendNotification);
    triggerSlider.setValue (triggerLevel, juce::dontSendNotification);
    gainSlider.onValueChange = [this]
    {
        verticalGain = (float) gainSlider.getValue();
        rebuildPath();
        updateStatus();
        repaint();
    };

    refreshControlAppearance();
    updateStatus();
}

void OscilloscopeMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    const bool themeChanged = applyTheme (newTheme);
    if (themeChanged)
        refreshControlAppearance();

    oscSampleRate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : oscSampleRate;

    if (freezeEnabled && hasData)
    {
        updateStatus();
        repaint();
        return;
    }

    const auto& mono = snapshot.oscilloscope;
    oscSampleCount = (int) mono.size();

    if (mono.empty())
    {
        cachedSamples.clear();
        scratchBuffer.clear();
        persistenceSamples.clear();
        monoPath.clear();
        if (! persistenceEnabled)
            persistencePath.clear();
        hasData = false;
        visibleSampleCount = 0;
        visibleSpanSeconds = 0.0;
        peakValue = 0.0f;
        rmsValue = 0.0f;
        updateStatus();
        repaint();
        return;
    }

    cachedSamples = mono;
    rebuildPath();
    updateStatus();
    repaint();
}

void OscilloscopeMeter::resized()
{
    auto content = getPanelContentBounds().toNearestInt();

    auto comboRow = content.removeFromTop (34);
    const int spacing = 6;
    const int comboWidth = juce::jmax (110, (comboRow.getWidth() - spacing * 2) / 3);

    auto setComboBounds = [&] (juce::ComboBox& box)
    {
        auto bounds = comboRow.removeFromLeft (comboWidth);
        box.setBounds (bounds.reduced (0, 2));
        comboRow.removeFromLeft (spacing);
    };

    setComboBounds (triggerBox);
    setComboBounds (timeBaseBox);
    setComboBounds (windowBox);

    auto toggleRow = content.removeFromTop (28);
    const int toggleWidth = juce::jmax (76, (toggleRow.getWidth() - spacing * 4) / 5);

    auto setToggleBounds = [&] (juce::ToggleButton& button)
    {
        auto bounds = toggleRow.removeFromLeft (toggleWidth);
        button.setBounds (bounds.reduced (4, 2));
        toggleRow.removeFromLeft (spacing);
    };

    setToggleBounds (freezeButton);
    setToggleBounds (persistenceButton);
    setToggleBounds (gridButton);
    setToggleBounds (fillButton);
    setToggleBounds (smoothButton);

    auto sliderRow = content.removeFromTop (46);
    const int labelWidth = 60;
    const int sliderSpacing = 14;
    const int sliderWidth = juce::jmax (140, (sliderRow.getWidth() - (labelWidth + sliderSpacing) * 2) / 2);

    auto setSliderBounds = [&] (juce::Slider& slider, juce::Label& label)
    {
        auto block = sliderRow.removeFromLeft (labelWidth + sliderWidth);
        label.setBounds (block.removeFromLeft (labelWidth));
        slider.setBounds (block.reduced (0, 6));
        sliderRow.removeFromLeft (sliderSpacing);
    };

    setSliderBounds (triggerSlider, triggerLabel);
    setSliderBounds (gainSlider, gainLabel);
}

void OscilloscopeMeter::refreshControlAppearance()
{
    auto setCombo = [this] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::textColourId, theme.text.withAlpha (0.82f));
        box.setColour (juce::ComboBox::outlineColourId, theme.outline.withAlpha (0.28f));
        box.setColour (juce::ComboBox::backgroundColourId, theme.background.darker (0.2f));
        box.setColour (juce::ComboBox::arrowColourId, theme.secondary.withAlpha (0.85f));
        box.setColour (juce::ComboBox::focusedOutlineColourId, theme.secondary.withAlpha (0.9f));
    };

    setCombo (triggerBox);
    setCombo (timeBaseBox);
    setCombo (windowBox);

    auto setToggle = [this] (juce::ToggleButton& button)
    {
        button.setColour (juce::ToggleButton::textColourId, theme.text.withAlpha (0.75f));
        button.setColour (juce::ToggleButton::tickColourId, theme.secondary.withAlpha (0.9f));
        button.setColour (juce::ToggleButton::tickDisabledColourId, theme.text.withAlpha (0.35f));
    };

    setToggle (freezeButton);
    setToggle (persistenceButton);
    setToggle (gridButton);
    setToggle (fillButton);
    setToggle (smoothButton);

    auto setSlider = [this] (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::thumbColourId, theme.secondary);
        slider.setColour (juce::Slider::trackColourId, theme.secondary.withAlpha (0.35f));
        slider.setColour (juce::Slider::textBoxTextColourId, theme.text.withAlpha (0.82f));
    };

    setSlider (triggerSlider);
    setSlider (gainSlider);

    auto setLabel = [this] (juce::Label& label)
    {
        label.setColour (juce::Label::textColourId, theme.text.withAlpha (0.72f));
    };

    setLabel (triggerLabel);
    setLabel (gainLabel);
}

void OscilloscopeMeter::paint (juce::Graphics& g)
{
    auto panel = drawPanelFrame (g);
    auto headerBounds = getPanelHeaderBounds();
    auto area = drawPanelHeader (g, panel);

    if (! headerBounds.isEmpty())
    {
        auto infoArea = headerBounds.reduced (12.0f, 6.0f);
        if (infoArea.getWidth() > 40.0f)
        {
            g.setColour (theme.text.withAlpha (0.68f));
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            juce::String headerText;
            if (oscSampleRate > 0.0)
                headerText << juce::String (oscSampleRate, 0) << " Hz  ";
            headerText << oscSampleCount << " samples";
            g.drawFittedText (headerText, infoArea.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    auto content = area;

    const auto drawStrip = [this, &g] (juce::Rectangle<float> strip)
    {
        if (strip.getWidth() <= 2.0f || strip.getHeight() <= 4.0f)
            return;

        auto background = strip.reduced (4.0f, 4.0f);
        g.setColour (theme.background.withAlpha (0.12f));
        g.fillRoundedRectangle (background, 7.0f);
        g.setColour (theme.outline.withAlpha (0.2f));
        g.drawRoundedRectangle (background, 7.0f, 1.0f);
    };

    auto comboStrip = content.removeFromTop (34.0f);
    drawStrip (comboStrip);

    auto toggleStrip = content.removeFromTop (28.0f);
    drawStrip (toggleStrip);

    auto sliderStrip = content.removeFromTop (46.0f);
    drawStrip (sliderStrip);

    juce::Rectangle<float> statusStrip;
    if (content.getHeight() > 90.0f)
        statusStrip = content.removeFromTop (26.0f);

    auto plot = content.reduced (12.0f, 12.0f);
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    auto frame = plot.expanded (6.0f, 6.0f);
    juce::ColourGradient background (theme.background.darker (0.32f), frame.getBottomLeft(),
                                     theme.background.darker (0.08f), frame.getTopRight(), false);
    g.setGradientFill (background);
    g.fillRoundedRectangle (frame, 9.0f);
    g.setColour (theme.outline.withAlpha (0.34f));
    g.drawRoundedRectangle (frame, 9.0f, 1.1f);

    const bool displayHasData = hasData && ! scratchBuffer.empty();

    if (! statusStrip.isEmpty())
    {
        auto strip = statusStrip.reduced (4.0f, 2.0f);
        g.setColour (theme.background.withAlpha (0.1f));
        g.fillRoundedRectangle (strip, 6.0f);
        g.setColour (theme.outline.withAlpha (0.16f));
        g.drawRoundedRectangle (strip, 6.0f, 1.0f);

        auto metricsArea = strip.removeFromRight (juce::jmin (170.0f, strip.getWidth() * 0.38f));
        auto textArea = strip;

        g.setColour (theme.text.withAlpha (0.72f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const juce::String infoText = statusText.isNotEmpty() ? statusText : juce::String (displayHasData ? "Live" : "Idle");
        g.drawFittedText (infoText, textArea.toNearestInt(), juce::Justification::centredLeft, 2);

        const float peakDb = juce::Decibels::gainToDecibels (peakValue + 1.0e-6f, -80.0f);
        const float rmsDb = juce::Decibels::gainToDecibels (rmsValue + 1.0e-6f, -80.0f);
        juce::StringArray stats;
        stats.add (juce::String::formatted ("Peak %0.1f dB", peakDb));
        stats.add (juce::String::formatted ("RMS %0.1f dB", rmsDb));
        g.setColour (theme.text.withAlpha (0.82f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawFittedText (stats.joinIntoString ("   "), metricsArea.toNearestInt(), juce::Justification::centredRight, 1);
    }

    if (! displayHasData)
    {
        g.setColour (theme.text.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        juce::String message = freezeEnabled ? juce::String ("Frozen") : juce::String ("Waiting for audio...");
        g.drawFittedText (message, plot.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    const auto transform = juce::AffineTransform::scale (plot.getWidth(), plot.getHeight())
                                                        .followedBy (juce::AffineTransform::translation (plot.getX(), plot.getY()));

    if (gridEnabled)
    {
        for (int i = 0; i <= 8; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / 8.0f;
            const bool major = (i % 2) == 0;
            g.setColour (theme.text.withAlpha (major ? 0.22f : 0.14f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), major ? 1.1f : 0.7f);
        }

        const std::array<float, 5> amps { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (float amp : amps)
        {
            const float norm = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * amp);
            const float y = plot.getY() + plot.getHeight() * norm;
            const bool zero = std::abs (amp) < 1.0e-4f;
            g.setColour (zero ? theme.text.withAlpha (0.45f) : theme.text.withAlpha (0.2f));
            g.drawLine (plot.getX(), y, plot.getRight(), y, zero ? 1.2f : 0.7f);

            auto label = juce::Rectangle<float> (plot.getX() - 48.0f, y - 8.0f, 44.0f, 16.0f);
            g.setColour (theme.text.withAlpha (0.48f));
            g.drawFittedText (juce::String::formatted ("%+.1f", amp), label.toNearestInt(), juce::Justification::centredRight, 1);
        }

        if (visibleSpanSeconds > 0.0)
        {
            const double totalMs = visibleSpanSeconds * 1000.0;
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            for (int i = 0; i <= 8; i += 2)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / 8.0f;
                const double t = totalMs * (double) i / 8.0;
                juce::String labelText = juce::String::formatted ("%0.1f ms", t);
                auto labelBounds = juce::Rectangle<float> (x - 34.0f, plot.getBottom() + 4.0f, 68.0f, 16.0f);
                g.setColour (theme.text.withAlpha (0.46f));
                g.drawFittedText (labelText, labelBounds.toNearestInt(), juce::Justification::centred, 1);
            }
        }
    }
    else
    {
        const float centreY = plot.getY() + plot.getHeight() * 0.5f;
        g.setColour (theme.text.withAlpha (0.18f));
        g.drawLine (plot.getX(), centreY, plot.getRight(), centreY, 0.9f);
    }

    if (persistenceEnabled && ! persistencePath.isEmpty())
    {
        juce::Path trail = persistencePath;
        trail.applyTransform (transform);
        g.setColour (theme.secondary.withAlpha (0.32f));
        g.strokePath (trail, juce::PathStrokeType (1.6f));
    }

    if (fillEnabled)
    {
        juce::Path fill = monoPath;
        fill.applyTransform (transform);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (theme.primary.withAlpha (0.16f));
        g.fillPath (fill);
    }

    juce::Path waveform = monoPath;
    waveform.applyTransform (transform);
    g.setColour (theme.tertiary.withAlpha (0.92f));
    g.strokePath (waveform, juce::PathStrokeType (2.2f));

    const float displayTrigger = juce::jlimit (-1.5f, 1.5f, triggerLevel * verticalGain);
    const float triggerNorm = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * displayTrigger);
    const float triggerY = plot.getY() + plot.getHeight() * triggerNorm;
    {
        juce::Path triggerPath;
        triggerPath.startNewSubPath (plot.getX(), triggerY);
        triggerPath.lineTo (plot.getRight(), triggerY);
        const float dashes[] { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType stroke (1.0f);
        stroke.createDashedStroke (dashed, triggerPath, dashes, 2);
        g.setColour (theme.warning.withAlpha (0.65f));
        g.strokePath (dashed, juce::PathStrokeType (1.0f));
    }

    auto triggerLabelArea = juce::Rectangle<float> (plot.getRight() - 92.0f, triggerY - 11.0f, 88.0f, 20.0f);
    g.setColour (theme.warning.withAlpha (0.78f));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawFittedText (juce::String::formatted ("Trig %+.2f", triggerLevel), triggerLabelArea.toNearestInt(), juce::Justification::centredRight, 1);

    juce::StringArray tags;
    if (freezeEnabled)
        tags.add ("FROZEN");
    if (persistenceEnabled)
        tags.add ("PERSIST");

    if (tags.size() > 0)
    {
        auto badge = juce::Rectangle<float> (plot.getX() + 10.0f, plot.getY() + 10.0f,
                                             80.0f, 20.0f * (float) tags.size() + 6.0f);
        g.setColour (theme.background.withAlpha (0.3f));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (theme.outline.withAlpha (0.25f));
        g.drawRoundedRectangle (badge, 6.0f, 1.0f);

        auto text = badge.reduced (6.0f, 4.0f);
        g.setColour (theme.text.withAlpha (0.82f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        for (const auto& tag : tags)
        {
            auto row = text.removeFromTop (18.0f);
            g.drawFittedText (tag, row.toNearestInt(), juce::Justification::centred, 1);
        }
    }
}

void OscilloscopeMeter::rebuildPath()
{
    monoPath.clear();

    if (cachedSamples.empty())
    {
        scratchBuffer.clear();
        persistenceSamples.clear();
        visibleSampleCount = 0;
        visibleSpanSeconds = 0.0;
        peakValue = 0.0f;
        rmsValue = 0.0f;
        hasData = false;
        return;
    }

    auto visible = buildVisibleSamples (cachedSamples);
    scratchBuffer = std::move (visible);
    visibleSampleCount = (int) scratchBuffer.size();

    if (visibleSampleCount < 2)
    {
        scratchBuffer.clear();
        persistenceSamples.clear();
        visibleSpanSeconds = 0.0;
        peakValue = 0.0f;
        rmsValue = 0.0f;
        hasData = false;
        return;
    }

    visibleSpanSeconds = (oscSampleRate > 0.0) ? (double) visibleSampleCount / oscSampleRate : 0.0;

    applyWindow (scratchBuffer);
    if (smoothingEnabled)
        applySmoothing (scratchBuffer);

    peakValue = 0.0f;
    double sumSquares = 0.0;
    for (float& sample : scratchBuffer)
    {
        float scaled = juce::jlimit (-1.5f, 1.5f, sample * verticalGain);
        peakValue = juce::jmax (peakValue, std::abs (scaled));
        sumSquares += (double) scaled * (double) scaled;
        sample = scaled;
    }

    rmsValue = scratchBuffer.empty() ? 0.0f : (float) std::sqrt (sumSquares / (double) scratchBuffer.size());

    monoPath.startNewSubPath (0.0f, juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * scratchBuffer.front()));
    for (int i = 1; i < visibleSampleCount; ++i)
    {
        const float x = (float) i / (float) (visibleSampleCount - 1);
        const float y = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * scratchBuffer[(size_t) i]);
        monoPath.lineTo (x, y);
    }

    hasData = true;
    updatePersistencePath();
}

void OscilloscopeMeter::updateStatus()
{
    juce::StringArray parts;

    switch (triggerMode)
    {
        case TriggerMode::free:    parts.add ("Free Run"); break;
        case TriggerMode::rising:  parts.add ("Trigger Rising"); break;
        case TriggerMode::falling: parts.add ("Trigger Falling"); break;
    }

    const double divisionSeconds = visibleSpanSeconds > 0.0 ? visibleSpanSeconds / 8.0 : 0.0;
    if (divisionSeconds > 0.0)
        parts.add (juce::String::formatted ("%0.2f ms/div", divisionSeconds * 1000.0));
    else if (oscSampleRate > 0.0 && visibleSampleCount > 0)
        parts.add (juce::String::formatted ("%d samples", visibleSampleCount));

    if (oscSampleRate > 0.0)
        parts.add (juce::String::formatted ("%0.0f Hz", oscSampleRate));

    parts.add (juce::String::formatted ("Gain x%0.2f", verticalGain));

    parts.add (freezeEnabled ? "Frozen" : "Live");

    if (persistenceEnabled)
        parts.add ("Persist");

    if (! hasData)
        parts.add ("Idle");

    statusText = parts.joinIntoString ("  •  ");
}

std::vector<float> OscilloscopeMeter::buildVisibleSamples (const std::vector<float>& source) const
{
    std::vector<float> result;
    const int total = (int) source.size();
    if (total <= 1)
        return result;

    const float clampedRatio = juce::jlimit (0.25f, 8.0f, timeBaseRatio);
    int desired = juce::jlimit (32, total, (int) std::round ((double) total / clampedRatio));
    desired = juce::jmax (2, desired);

    int start = juce::jmax (0, total - desired);

    if (triggerMode != TriggerMode::free && desired < total)
    {
        const float threshold = triggerLevel;
        for (int i = total - 2; i >= 1; --i)
        {
            const float prev = source[(size_t) i];
            const float next = source[(size_t) (i + 1)];
            const bool triggered = (triggerMode == TriggerMode::rising && prev < threshold && next >= threshold)
                                   || (triggerMode == TriggerMode::falling && prev > threshold && next <= threshold);
            if (triggered)
            {
                start = juce::jlimit (0, total - desired, i - desired / 8);
                break;
            }
        }
    }

    result.resize ((size_t) desired);
    for (int i = 0; i < desired; ++i)
        result[(size_t) i] = source[(size_t) juce::jmin (total - 1, start + i)];

    return result;
}

void OscilloscopeMeter::applyWindow (std::vector<float>& data) const
{
    if (windowMode == WindowMode::rectangular || data.size() < 2)
        return;

    const int size = (int) data.size();
    for (int i = 0; i < size; ++i)
    {
        const float phase = (float) i / (float) (size - 1);
        float window = 1.0f;

        if (windowMode == WindowMode::hann)
            window = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * phase));
        else if (windowMode == WindowMode::blackman)
            window = 0.42f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase)
                              + 0.08f * std::cos (2.0f * juce::MathConstants<float>::twoPi * phase);

        data[(size_t) i] *= window;
    }
}

void OscilloscopeMeter::applySmoothing (std::vector<float>& data) const
{
    if (data.size() < 3)
        return;

    std::vector<float> copy = data;
    for (size_t i = 1; i < data.size() - 1; ++i)
        data[i] = (copy[i - 1] + copy[i] * 2.0f + copy[i + 1]) * 0.25f;
}

void OscilloscopeMeter::updatePersistencePath()
{
    if (! persistenceEnabled)
    {
        persistenceSamples.clear();
        persistencePath.clear();
        return;
    }

    if (scratchBuffer.empty())
        return;

    if (persistenceSamples.size() != scratchBuffer.size())
        persistenceSamples.assign (scratchBuffer.size(), 0.0f);

    const float decay = 0.82f;
    const float mix = 1.0f - decay;

    for (size_t i = 0; i < scratchBuffer.size(); ++i)
        persistenceSamples[i] = persistenceSamples[i] * decay + scratchBuffer[i] * mix;

    persistencePath.clear();
    persistencePath.startNewSubPath (0.0f, juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * persistenceSamples.front()));
    for (size_t i = 1; i < persistenceSamples.size(); ++i)
    {
        const float x = (float) i / (float) (persistenceSamples.size() - 1);
        const float y = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * persistenceSamples[i]);
        persistencePath.lineTo (x, y);
    }
}

VuNeedleMeter::VuNeedleMeter()
    : MeterComponent ("VU Meter")
{
}

void VuNeedleMeter::update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme)
{
    applyTheme (newTheme);
    leftNeedle = snapshot.vuNeedleL;
    rightNeedle = snapshot.vuNeedleR;
    clipL = snapshot.clipLeft;
    clipR = snapshot.clipRight;
    repaint();
}

void VuNeedleMeter::paint (juce::Graphics& g)
{
    auto area = drawPanelFrame (g);
    auto meterArea = area.reduced (12.0f);

    const auto drawNeedle = [&] (juce::Rectangle<float> bounds, float gain, juce::Colour colour, bool clipped)
    {
        g.setColour (theme.background.darker (0.15f));
        g.fillEllipse (bounds);
        g.setColour (theme.outline.withAlpha (0.8f));
        g.drawEllipse (bounds, 1.2f);

        const float dB = juce::Decibels::gainToDecibels (gain + 1.0e-6f, -40.0f);
        const float clamped = juce::jlimit (-20.0f, 6.0f, dB);
        const float angle = juce::jmap (clamped, -20.0f, 6.0f, juce::degreesToRadians (220.0f), juce::degreesToRadians (-40.0f));
        const juce::Point<float> centre = bounds.getCentre();
        const float radius = bounds.getWidth() * 0.45f;
        const juce::Point<float> end = centre.getPointOnCircumference (radius, angle);

        g.setColour (colour.withAlpha (0.8f));
        g.drawLine (centre.x, centre.y, end.x, end.y, 2.4f);
        g.setColour (theme.text.withAlpha (0.7f));
        g.drawText (juce::String::formatted ("%4.1f dB", dB), bounds, juce::Justification::centred);

        if (clipped)
        {
            g.setColour (theme.warning);
            g.fillEllipse (bounds.removeFromBottom (10.0f).withX (bounds.getCentreX() - 5.0f));
        }
    };

    auto leftArea = meterArea.removeFromLeft (meterArea.getWidth() / 2.0f).reduced (6.0f);
    auto rightArea = meterArea.reduced (6.0f);

    drawNeedle (leftArea, leftNeedle, theme.primary, clipL);
    drawNeedle (rightArea, rightNeedle, theme.secondary, clipR);
}

