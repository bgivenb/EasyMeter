#include <JuceHeader.h>
#pragma once
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "Meters.h"
#include "Spectrogram.h"
#include "LookAndFeel.h"

class NotifyingTabbedComponent : public juce::TabbedComponent
{
public:
    explicit NotifyingTabbedComponent (juce::TabbedButtonBar::Orientation orientation)
        : juce::TabbedComponent (orientation) {}

    std::function<void (int, const juce::String&)> onCurrentTabChanged;

    void currentTabChanged (int newCurrentTabIndex, const juce::String& newTabName) override
    {
        juce::TabbedComponent::currentTabChanged (newCurrentTabIndex, newTabName);

        if (onCurrentTabChanged != nullptr)
            onCurrentTabChanged (newCurrentTabIndex, newTabName);
    }
};

class MiniMetersCloneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit MiniMetersCloneAudioProcessorEditor (MiniMetersCloneAudioProcessor&);
    ~MiniMetersCloneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MiniMetersCloneAudioProcessor& audioProcessor;

    MmLookAndFeel lookAndFeel;
    SharedDataSnapshot snapshot;
    MeterTheme theme {};

    WaveformMeter waveform;
    SpectrogramMeter spectrogram;
    SpectrumMeter spectrum;
    LoudnessMeter loudness;
    StereoMeter stereo;
    OscilloscopeMeter oscilloscope;
    InfoPanel info;

    NotifyingTabbedComponent meterTabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<MeterComponent*> moduleComponents;

    void timerCallback() override;
    void updateTheme();
    MeterTheme createThemeForSelection() const;
    void saveAudioHistory();
    void updateActiveModule();
    MeterComponent* getActiveModule() const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniMetersCloneAudioProcessorEditor)
};
