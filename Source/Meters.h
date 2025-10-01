#pragma once

#include <JuceHeader.h>
#include <array>
#include <deque>
#include <functional>
#include "PluginProcessor.h"

struct MeterTheme
{
    juce::Colour background;
    juce::Colour outline;
    juce::Colour primary;
    juce::Colour secondary;
    juce::Colour tertiary;
    juce::Colour text;
    juce::Colour warning;

    bool operator== (const MeterTheme& other) const noexcept
    {
        return background == other.background
            && outline == other.outline
            && primary == other.primary
            && secondary == other.secondary
            && tertiary == other.tertiary
            && text == other.text
            && warning == other.warning;
    }

    bool operator!= (const MeterTheme& other) const noexcept
    {
        return ! (*this == other);
    }
};

class MeterComponent : public juce::Component
{
public:
    explicit MeterComponent (juce::String titleToUse);
    ~MeterComponent() override = default;

    virtual void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) = 0;

protected:
    static constexpr float headerSectionHeight = 36.0f;

    juce::Rectangle<float> getFrameBounds() const noexcept;
    juce::Rectangle<float> getPanelHeaderBounds() const noexcept;
    juce::Rectangle<float> getPanelContentBounds() const noexcept;
    juce::Rectangle<float> drawPanelFrame (juce::Graphics& g) const;
    juce::Rectangle<float> drawPanelHeader (juce::Graphics& g, juce::Rectangle<float> panelBounds) const;
    const MeterTheme& getTheme() const noexcept { return theme; }
    bool applyTheme (const MeterTheme& newTheme) noexcept;
    juce::Rectangle<float> getPlotArea (juce::Rectangle<float> bounds, float headerHeight = 24.0f) const noexcept;

    juce::String title;
    MeterTheme theme {};
};

class WaveformMeter : public MeterComponent
{
public:
    WaveformMeter();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    enum class LayoutMode
    {
        dual = 1,
        overlay,
        mono,
        midSide
    };

    enum class AmplitudeMode
    {
        natural = 1,
        expanded,
        focused
    };

    enum class SpanMode
    {
        full = 1,
        threeQuarter,
        half,
        quarter
    };

    void handlePresetSelection();
    void updateButtonColours();
    float getAmplitudeScale() const noexcept;
    float getNormalisationGain (const std::vector<float>& mins, const std::vector<float>& maxs) const noexcept;
    void buildChannelPaths (juce::Path& fillPath,
                            juce::Path& upperPath,
                            juce::Path& lowerPath,
                            juce::Path& centrePath,
                            const std::vector<float>& mins,
                            const std::vector<float>& maxs,
                            float amplitudeScale,
                            bool applySmoothing);
    void buildBandPath (juce::Path& dest,
                        const std::vector<float>& values,
                        float amplitudeScale,
                        float normalisation,
                        bool applySmoothing);
    void refreshStatusText();

    juce::Path leftPath, rightPath;
    juce::Path leftUpperPath, rightUpperPath;
    juce::Path leftLowerPath, rightLowerPath;
    juce::Path leftCentrePath, rightCentrePath;
    std::array<juce::Path, 3> leftBandPaths{};
    std::array<juce::Path, 3> rightBandPaths{};
    juce::Path monoPath, midPath, sidePath;
    juce::Path monoUpperPath, monoLowerPath, monoCentrePath;
    juce::Path midUpperPath, midLowerPath, midCentrePath;
    juce::Path sideUpperPath, sideLowerPath, sideCentrePath;
    std::array<juce::Path, 3> monoBandPaths{};
    std::array<juce::Path, 3> midBandPaths{};
    std::array<juce::Path, 3> sideBandPaths{};
    std::array<float, 3> leftBandPeaks { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> rightBandPeaks { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> monoBandPeaks { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> midBandPeaks { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> sideBandPeaks { 0.0f, 0.0f, 0.0f };
    bool hasData = false;
    bool detailedMode = false;
    bool freezeEnabled = false;
    bool rmsOverlayEnabled = false;
    bool normaliseEnabled = false;
    bool gridEnabled = true;
    [[maybe_unused]] bool smoothingEnabled = true;
    juce::ToggleButton detailToggle { "Detailed" };
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton rmsButton { "RMS" };
    juce::ToggleButton normaliseButton { "Normalize" };
    juce::ToggleButton gridButton { "Grid" };
    juce::ComboBox presetBox;
    juce::ComboBox layoutBox;
    juce::ComboBox spanBox;
    juce::ComboBox amplitudeBox;
    juce::Label statusLabel;
    juce::String statusText;
    LayoutMode layoutMode = LayoutMode::dual;
    AmplitudeMode amplitudeMode = AmplitudeMode::natural;
    SpanMode spanMode = SpanMode::full;
    bool clipLeft = false;
    bool clipRight = false;
    float clipGlowLeft = 0.0f;
    float clipGlowRight = 0.0f;
    float peakLeftDb = -120.0f;
    float peakRightDb = -120.0f;
    float peakLeftGain = 0.0f;
    float peakRightGain = 0.0f;
    int waveformResolution = 0;
    int visibleResolution = 0;
    int visibleOffset = 0;
    int samplesPerBucket = 0;
    double waveformSpanSeconds = 0.0;
    double waveformSampleRate = 48000.0;
    TransportInfo transport;
    float rmsLeft = 0.0f;
    float rmsRight = 0.0f;
    float rmsMid = 0.0f;
    float rmsSide = 0.0f;
    juce::String presetLockedStatus;
    float currentAmplitudeScale = 0.45f;
};

class InfoPanel : public MeterComponent
{
public:
    InfoPanel();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void updateColours();

    juce::Label headlineLabel;
    juce::Label taglineLabel;
    juce::Label loveLabel;
    juce::Label connectLabel;
    juce::HyperlinkButton websiteButton;
    juce::HyperlinkButton instagramButton;
    juce::HyperlinkButton spotifyButton;
    juce::HyperlinkButton soundcloudButton;
};

class SpectrumMeter : public MeterComponent
{
public:
    SpectrumMeter();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    enum class Scale
    {
        linear = 1,
        logarithmic,
        mel
    };

    enum class DisplayMode
    {
        line = 1,
        filledLine,
        bars,
        overlay
    };

    void updateControlColours();
    void rebuildPaths();
    void updateLegendText();
    float frequencyToNorm (float frequency) const noexcept;
    float getSmoothingAmount() const noexcept;
    void applyProcessing();

    juce::Path spectrumPath;
    juce::Path overlayPath;
    std::vector<float> bands;
    std::vector<float> processedBands;
    std::vector<float> peakHoldBands;
    std::vector<float> frequencyAxis;
    juce::ComboBox scaleBox;
    juce::ComboBox modeBox;
    juce::ComboBox smoothingBox;
    juce::Slider tiltSlider;
    juce::Slider floorSlider;
    juce::ToggleButton gridButton { "Grid" };
    juce::ToggleButton legendButton { "Legend" };
    juce::ToggleButton peakHoldButton { "Peak Hold" };
    juce::Slider decaySlider;
    juce::Label tiltLabel;
    juce::Label floorLabel;
    juce::Label decayLabel;
    juce::String legendText;
    double lastUpdateSeconds = 0.0;
    bool hasData = false;
    Scale scale = Scale::logarithmic;
    DisplayMode displayMode = DisplayMode::filledLine;
    float tiltDbPerOct = 0.0f;
    float noiseFloorDb = -120.0f;
    double sampleRate = 48000.0;
    bool legendVisible = true;
    float decayPerSecondDb = 6.0f;
};

class LoudnessMeter : public MeterComponent
{
public:
    LoudnessMeter();

    struct State
    {
        float targetLufs = -14.0f;
        bool showRms = false;
        int historySeconds = 20;
    };

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

    void setState (const State& newState, bool force = false);
    void setOnStateChanged (std::function<void (const State&)> callback);
    void setOnResetRequested (std::function<void()> callback);

private:
    void notifyStateChanged();
    void updateHistorySelection (int seconds);
    void updateControlColours();
    void rebuildHistoryPath();
    static float clampDisplayLoudness (float value) noexcept;

    State state {};
    std::function<void (const State&)> onStateChanged;
    std::function<void()> onResetRequested;

    float momentary = -100.0f;
    float shortTerm = -100.0f;
    float integrated = -100.0f;
    float loudnessRange = 0.0f;
    float maxMomentary = -100.0f;
    float maxShortTerm = -100.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
    bool clipL = false;
    bool clipR = false;
    std::vector<float> historyValues;
    float historyInterval = 0.05f;
    juce::Path historyPath;
    juce::Path historyFillPath;
    juce::Path historyOverPath;
    juce::Path historyOverFillPath;
    bool historyHasData = false;
    bool historyOverHasData = false;
    double overTargetSeconds = 0.0;
    int visibleHistoryPoints = 0;
    float visibleHistorySeconds = 0.0f;

    float rmsFast = 0.0f;
    float rmsSlow = 0.0f;

    juce::Slider targetSlider;
    juce::ComboBox historyBox;
    juce::ToggleButton rmsToggle { "RMS" };
    juce::TextButton targetResetButton { "Reset" };
    juce::TextButton targetPresetButton { "Presets" };
    juce::TextButton clearHistoryButton { "Clear" };
    juce::Label targetLabel;
    juce::Label historyLengthLabel;

    struct HistoryPoint
    {
        juce::Point<float> position;
        float value = -100.0f;
        float norm = 0.0f;
    };

    std::vector<HistoryPoint> historyPoints;
    float targetLinePosition = 1.0f;
    bool momentaryOverTarget = false;
    bool shortTermOverTarget = false;
    bool integratedOverTarget = false;
    TransportInfo transport;
};

class StereoMeter : public MeterComponent
{
public:
    StereoMeter();

    struct State
    {
        int viewMode = 1;
        int displayMode = 1;
        float scopeScale = 1.0f;
        int historySeconds = 6;
        bool freeze = false;
        bool showDots = false;
        bool persistence = false;
        float trailSeconds = 0.6f;
    };

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

    void setState (const State& newState, bool force = false);
    void setOnStateChanged (std::function<void (const State&)> callback);

private:
    enum class PlotMode { midSide = 1, leftRight };
    enum class DisplayMode { lines = 1, dots, persistence };

    void rebuildPaths();
    void updateControlColours();
    void pushCorrelationHistory (float value) noexcept;
    juce::Path createTransformedPath (const juce::Path& source, juce::Rectangle<float> bounds) const;
    void refreshHistoryCapacity();
    void handleHistorySelectionChanged();
    void handleScopeScaleChanged();
    void pushTrailFrame();
    void decayTrailFrames();
    void applyDisplayMode (DisplayMode mode, bool notifyState, bool forceRepaint, bool updateButtons);
    void updateTrailSettings();
    void updateTrailComponents();
    juce::Colour getCorrelationColour() const noexcept;
    void notifyStateChanged();

    juce::ComboBox viewModeBox;
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton dotsButton { "Dots" };
    juce::ToggleButton persistenceButton { "Trail" };
    juce::Slider scopeScaleSlider;
    juce::Label scopeScaleLabel;
    juce::ComboBox historyBox;
    juce::Label trailDecayLabel;
    juce::Slider trailDecaySlider;

    juce::Path lissajousMidSide;
    juce::Path lissajousLeftRight;
    std::vector<juce::Point<float>> rawLissajous;
    std::vector<juce::Point<float>> pointsMidSide;
    std::vector<juce::Point<float>> pointsLeftRight;

    std::vector<float> correlationHistory;
    int correlationHistoryCapacity = 180;
    int correlationHistoryWrite = 0;
    int correlationHistoryFilled = 0;

    float correlation = 0.0f;
    float width = 0.0f;
    float leftLevel = 0.0f;
    float rightLevel = 0.0f;
    float midLevel = 0.0f;
    float sideLevel = 0.0f;
    float balanceDb = 0.0f;
    float sideToMidRatio = 0.0f;

    float scopeScale = 1.0f;
    int historySeconds = 6;
    bool persistenceEnabled = false;

    struct TrailFrame
    {
        juce::Path midSide;
        juce::Path leftRight;
        float alpha = 1.0f;
    };

    std::deque<TrailFrame> trailFrames;
    int maxTrailFrames = 18;

    PlotMode plotMode = PlotMode::midSide;
    DisplayMode displayMode = DisplayMode::lines;
    bool freezeDisplay = false;
    bool showDots = false;
    bool hasData = false;
    float trailSeconds = 0.6f;
    int trailDecayFrames = 18;
    State state {};
    std::function<void (const State&)> onStateChanged;
    TransportInfo transport;
};

class OscilloscopeMeter : public MeterComponent
{
public:
    OscilloscopeMeter();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    enum class TriggerMode
    {
        free = 1,
        rising,
        falling
    };

    enum class WindowMode
    {
        rectangular = 1,
        hann,
        blackman
    };

    void refreshControlAppearance();
    void rebuildPath();
    void updateStatus();
    std::vector<float> buildVisibleSamples (const std::vector<float>& source) const;
    void applyWindow (std::vector<float>& data) const;
    void applySmoothing (std::vector<float>& data) const;
    void updatePersistencePath();

    juce::Path monoPath;
    juce::Path persistencePath;
    std::vector<float> cachedSamples;
    std::vector<float> scratchBuffer;
    std::vector<float> persistenceSamples;
    bool hasData = false;
    bool freezeEnabled = false;
    bool persistenceEnabled = false;
    bool fillEnabled = false;
    [[maybe_unused]] bool smoothingEnabled = true;
    bool gridEnabled = true;
    TriggerMode triggerMode = TriggerMode::rising;
    WindowMode windowMode = WindowMode::hann;
    float triggerLevel = 0.0f;
    float verticalGain = 1.0f;
    float timeBaseRatio = 1.0f;
    [[maybe_unused]] double lastUpdateTime = 0.0;
    double oscSampleRate = 0.0;
    int oscSampleCount = 0;
    int visibleSampleCount = 0;
    double visibleSpanSeconds = 0.0;
    float peakValue = 0.0f;
    float rmsValue = 0.0f;
    juce::ComboBox triggerBox;
    juce::ComboBox windowBox;
    juce::ComboBox timeBaseBox;
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton persistenceButton { "Persist" };
    juce::ToggleButton gridButton { "Grid" };
    juce::ToggleButton fillButton { "Fill" };
    juce::ToggleButton smoothButton { "Smooth" };
    juce::Slider triggerSlider;
    juce::Slider gainSlider;
    juce::Label triggerLabel;
    juce::Label gainLabel;
    juce::String statusText;
};

class VuNeedleMeter : public MeterComponent
{
public:
    VuNeedleMeter();

    void update (const SharedDataSnapshot& snapshot, const MeterTheme& newTheme) override;
    void paint (juce::Graphics& g) override;

private:
    float leftNeedle = 0.0f;
    float rightNeedle = 0.0f;
    bool clipL = false;
    bool clipR = false;
};
