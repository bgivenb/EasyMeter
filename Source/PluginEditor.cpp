#include <JuceHeader.h>
#include "PluginEditor.h"

MiniMetersCloneAudioProcessorEditor::MiniMetersCloneAudioProcessorEditor (MiniMetersCloneAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (640, 480, 1920, 1080);
    setSize (1100, 720);

    struct ModuleInfo
    {
        const char* name = nullptr;
        MeterComponent* component = nullptr;
    };

    const ModuleInfo moduleList[] =
    {
        { "Waveform",       &waveform },
        { "Spectrogram",   &spectrogram },
        { "Spectrum",       &spectrum },
        { "Oscilloscope",   &oscilloscope },
        { "Loudness",       &loudness },
        { "Stereo Field",   &stereo },
        { "Info",           &info }
    };

    const size_t moduleCount = sizeof (moduleList) / sizeof (moduleList[0]);
    moduleComponents.reserve (moduleCount);

    for (const auto& module : moduleList)
    {
        moduleComponents.push_back (module.component);
        meterTabs.addTab (juce::String (module.name).toUpperCase(), juce::Colours::transparentBlack, module.component, false);
    }

    meterTabs.setTabBarDepth (34);
    meterTabs.setOutline (0);
    auto& tabBar = meterTabs.getTabbedButtonBar();
    tabBar.setMinimumTabScaleFactor (0.68f);
    meterTabs.onCurrentTabChanged = [this] (int, const juce::String&)
    {
        updateActiveModule();
        repaint();
    };

    addAndMakeVisible (meterTabs);

    const auto savedLoudnessState = audioProcessor.getLoudnessMeterState();
    LoudnessMeter::State loudnessState;
    loudnessState.targetLufs = savedLoudnessState.targetLufs;
    loudnessState.showRms = savedLoudnessState.showRms;
    loudnessState.historySeconds = savedLoudnessState.historySeconds;
    loudness.setState (loudnessState, true);
    loudness.setOnStateChanged ([this] (const LoudnessMeter::State& newState)
    {
        LoudnessMeterState stored;
        stored.targetLufs = newState.targetLufs;
        stored.showRms = newState.showRms;
        stored.historySeconds = newState.historySeconds;
        audioProcessor.setLoudnessMeterState (stored);
    });
    loudness.setOnResetRequested ([this]
    {
        audioProcessor.resetLoudnessStatistics();
    });

    const auto savedStereoState = audioProcessor.getStereoMeterState();
    StereoMeter::State stereoState;
    stereoState.viewMode = savedStereoState.viewMode;
    stereoState.displayMode = savedStereoState.displayMode;
    stereoState.scopeScale = savedStereoState.scopeScale;
    stereoState.historySeconds = savedStereoState.historySeconds;
    stereoState.freeze = savedStereoState.freeze;
    stereoState.showDots = savedStereoState.showDots;
    stereoState.persistence = savedStereoState.persistence;
    stereoState.trailSeconds = savedStereoState.trailSeconds;
    stereo.setState (stereoState, true);
    stereo.setOnStateChanged ([this] (const StereoMeter::State& newState)
    {
        StereoMeterState stored;
        stored.viewMode = newState.viewMode;
        stored.displayMode = newState.displayMode;
        stored.scopeScale = newState.scopeScale;
        stored.historySeconds = newState.historySeconds;
        stored.freeze = newState.freeze;
        stored.showDots = newState.showDots;
        stored.persistence = newState.persistence;
        stored.trailSeconds = newState.trailSeconds;
        audioProcessor.setStereoMeterState (stored);
    });

    updateTheme();
    updateActiveModule();

    resized();
    startTimerHz (30);
}

MiniMetersCloneAudioProcessorEditor::~MiniMetersCloneAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MiniMetersCloneAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient backgroundGradient (theme.background.darker (0.35f), bounds.getBottomLeft(),
                                             theme.background.brighter (0.12f), bounds.getTopRight(), false);
    backgroundGradient.addColour (0.4, theme.background.darker (0.12f));
    g.setGradientFill (backgroundGradient);
    g.fillRect (bounds);

    auto halo = bounds.reduced (3.0f);
    juce::ColourGradient haloGradient (theme.secondary.withAlpha (0.08f), halo.getBottomLeft().toFloat(),
                                       theme.tertiary.withAlpha (0.12f), halo.getTopRight().toFloat(), false);
    haloGradient.addColour (0.5, theme.primary.withAlpha (0.06f));
    g.setGradientFill (haloGradient);
    g.drawRoundedRectangle (halo.toFloat(), 22.0f, 1.2f);
}

void MiniMetersCloneAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto content = bounds.reduced (6, 6);
    meterTabs.setBounds (content);
}

void MiniMetersCloneAudioProcessorEditor::timerCallback()
{
    audioProcessor.fillSnapshot (snapshot);
    updateTheme();

    updateActiveModule();
}

void MiniMetersCloneAudioProcessorEditor::updateActiveModule()
{
    if (auto* activeModule = getActiveModule())
        activeModule->update (snapshot, theme);
}

MeterComponent* MiniMetersCloneAudioProcessorEditor::getActiveModule() const noexcept
{
    const int currentIndex = meterTabs.getCurrentTabIndex();

    if (! juce::isPositiveAndBelow (currentIndex, (int) moduleComponents.size()))
        return nullptr;

    return moduleComponents[(size_t) currentIndex];
}

void MiniMetersCloneAudioProcessorEditor::updateTheme()
{
    theme = createThemeForSelection();

    meterTabs.setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
    meterTabs.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    auto& tabBar = meterTabs.getTabbedButtonBar();
    tabBar.setColour (juce::TabbedButtonBar::tabTextColourId, theme.text.withAlpha (0.38f));
    tabBar.setColour (juce::TabbedButtonBar::frontTextColourId, theme.text);
    tabBar.setColour (juce::TabbedButtonBar::frontOutlineColourId, theme.secondary.withAlpha (0.8f));
    tabBar.setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    tabBar.setColour (MmLookAndFeel::tabInactiveBackgroundColourId, theme.background.darker (0.1f));
    tabBar.setColour (MmLookAndFeel::tabActiveBackgroundColourId, theme.primary.withAlpha (0.22f));

    repaint();

}

MeterTheme MiniMetersCloneAudioProcessorEditor::createThemeForSelection() const
{
    MeterTheme base;

    base.background = juce::Colour::fromRGB (8, 10, 30);
    base.primary    = juce::Colour::fromRGB (94, 210, 255);
    base.secondary  = juce::Colour::fromRGB (152, 134, 255);
    base.tertiary   = juce::Colour::fromRGB (255, 126, 208);
    base.text       = juce::Colour::fromRGB (226, 238, 255);
    base.outline    = juce::Colour::fromRGB (26, 32, 68);
    base.warning    = juce::Colour::fromRGB (255, 142, 176);

    return base;
}

void MiniMetersCloneAudioProcessorEditor::saveAudioHistory()
{
    SharedDataSnapshot latest;
    audioProcessor.fillSnapshot (latest, true);

    const auto& history = latest.audioHistory;
    const int totalSamples = history.getNumSamples();
    const int channels = history.getNumChannels();
    if (totalSamples <= 0 || channels == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "EasyMeter", "No audio captured yet.");
        return;
    }

    const int available = latest.bufferWrapped ? totalSamples : latest.writePosition;
    if (available <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "EasyMeter", "Not enough audio to save.");
        return;
    }

    auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    juce::File file = documents.getNonexistentChildFile ("EasyMeter_Audio_" + timestamp, ".wav");
    juce::AudioBuffer<float> ordered;
    ordered.setSize (channels, available);

    const int startIndex = latest.bufferWrapped ? (latest.writePosition - available + totalSamples) % totalSamples
                                                : juce::jmax (0, latest.writePosition - available);

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = history.getReadPointer (ch);
        float* dest = ordered.getWritePointer (ch);

        if (latest.bufferWrapped)
        {
            int idx = startIndex;
            for (int i = 0; i < available; ++i)
            {
                dest[i] = src[idx];
                idx = (idx + 1) % totalSamples;
            }
        }
        else
        {
            std::copy (src + startIndex, src + startIndex + available, dest);
        }
    }

    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "EasyMeter", "Unable to create file.");
        return;
    }

    juce::WavAudioFormat format;
    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (format.createWriterFor (stream.get(), latest.sampleRate,
                                                                                        static_cast<unsigned int> (channels),
                                                                                        24, {}, 0)))
    {
        stream.release();
        writer->writeFromAudioSampleBuffer (ordered, 0, ordered.getNumSamples());
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "EasyMeter", "Failed to create WAV writer.");
    }
}

