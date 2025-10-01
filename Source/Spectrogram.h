#pragma once

#include <array>
#include <vector>

#include "Meters.h"

class SpectrogramMeter : public MeterComponent
{
public:
    SpectrogramMeter();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    enum class FrequencyScale
    {
        linear = 1,
        logarithmic
    };

    void refreshImage();
    void refreshStatusText();
    void updateControlColours();
    void rebuildColourLut();
    juce::ColourGradient createPaletteGradient() const;
    juce::Colour colourForMagnitude (float magnitude);

    struct BeatMarker
    {
        double timeFromNow = 0.0;
        int beatInBar = 1;
        int barNumber = 1;
        bool isBarStart = false;
    };

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plotBounds);
    void drawFrequencyScale (juce::Graphics& g,
                             juce::Rectangle<float> labelBounds,
                             juce::Rectangle<float> plotBounds);
    void drawTimeAxis (juce::Graphics& g,
                       juce::Rectangle<float> axisBounds,
                       juce::Rectangle<float> plotBounds,
                       const std::vector<BeatMarker>& beatMarkers,
                       bool drawUniformGrid);
    std::vector<BeatMarker> createBeatMarkers (double spanSeconds) const;
    static double positiveModulo (double value, double modulus) noexcept;
    bool shouldDrawBeatGrid() const noexcept;
    double getTargetSpanSeconds() const noexcept;
    int getVisibleColumnCount (int availableColumns) const noexcept;
    float frequencyToY (double frequency, juce::Rectangle<float> plotBounds) const;
    std::vector<double> getGridFrequencies() const;

    juce::AudioBuffer<float> spectrogramData;
    juce::AudioBuffer<float> orderedColumns;
    juce::Image spectrogramImage;
    std::vector<float> binRemap;
    std::array<juce::Colour, 512> colourLut {};
    bool colourLutDirty = true;

    juce::ComboBox scaleBox;
    juce::ComboBox paletteBox;
    juce::ComboBox timeSpanBox;
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton gridButton { "Grid" };
    juce::ToggleButton beatGridButton { "Beat Grid" };
    juce::Slider floorSlider;
    juce::Slider intensitySlider;
    juce::Label floorLabel { {}, "Floor" };
    juce::Label intensityLabel { {}, "Intensity" };
    juce::Label statusLabel;

    TransportInfo transport;

    bool hasData = false;
    bool freezeEnabled = false;
    bool gridEnabled = true;
    bool beatGridEnabled = true;
    int visibleColumns = 0;
    double secondsPerColumn = 0.0;
    double visibleSeconds = 0.0;
    float minDb = -120.0f;
    float maxDb = -5.0f;
    double sampleRate = 48000.0;
    bool spectrogramDirty = true;
    int writePosition = 0;
    bool wrapped = false;
    double targetSpanSeconds = 3.0;

    static constexpr int controlsHeight = 68;
    static constexpr int slidersHeight = 40;
    static constexpr int axisHeight = 24;
    static constexpr int statusHeight = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramMeter)
};
