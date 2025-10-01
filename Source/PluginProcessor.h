#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>

constexpr int kWaveformResolution = 512;
constexpr int kOscilloscopeBufferSize = 2048;
constexpr float kWaveformLowCrossoverHz = 160.0f;
constexpr float kWaveformHighCrossoverHz = 4000.0f;

struct TransportInfo
{
    bool hasInfo = false;
    double bpm = 120.0;
    double ppqPosition = 0.0;
    double ppqPositionOfLastBarStart = 0.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool isPlaying = false;
};

struct SharedDataSnapshot
{
    juce::AudioBuffer<float> audioHistory;
    int writePosition = 0;
    bool bufferWrapped = false;

    std::vector<float> waveformLeftMins;
    std::vector<float> waveformLeftMaxs;
    std::vector<float> waveformRightMins;
    std::vector<float> waveformRightMaxs;
    int waveformSamplesPerBucket = 0;

    std::vector<float> waveformLeftLowBand;
    std::vector<float> waveformLeftMidBand;
    std::vector<float> waveformLeftHighBand;
    std::vector<float> waveformRightLowBand;
    std::vector<float> waveformRightMidBand;
    std::vector<float> waveformRightHighBand;

    std::vector<float> oscilloscope;

    std::vector<float> spectrum;
    juce::AudioBuffer<float> spectrogram;
    int spectrogramWritePosition = 0;
    bool spectrogramWrapped = false;

    std::array<juce::Point<float>, 512> lissajous {};
    int lissajousCount = 0;

    float momentaryLufs = -100.0f;
    float shortTermLufs = -100.0f;
    float integratedLufs = -100.0f;
    float loudnessRange = 0.0f;
    float maxMomentaryLufs = -100.0f;
    float maxShortTermLufs = -100.0f;
    float rmsFast = 0.0f;
    float rmsSlow = 0.0f;
    float correlation = 0.0f;
    float stereoWidth = 0.0f;
    float leftRms = 0.0f;
    float rightRms = 0.0f;
    float midRms = 0.0f;
    float sideRms = 0.0f;
    float balanceDb = 0.0f;
    float vuNeedleL = 0.0f;
    float vuNeedleR = 0.0f;
    bool clipLeft = false;
    bool clipRight = false;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    double sampleRate = 48000.0;
    float loudnessHistoryInterval = 0.0f;
    std::vector<float> loudnessHistory;
    TransportInfo transport;
};

struct LoudnessMeterState
{
    float targetLufs = -14.0f;
    bool showRms = false;
    int historySeconds = 20;
};

struct StereoMeterState
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

class MiniMetersCloneAudioProcessor : public juce::AudioProcessor
{
public:
    MiniMetersCloneAudioProcessor();
    ~MiniMetersCloneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EasyMeter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL  { 0.0f }, rmsR  { 0.0f };

    void setBallistics (float riseMs, float fallMs);

    void fillSnapshot (SharedDataSnapshot& snapshot, bool includeAudioHistory = false) const;

    void setStickinessRequested (bool shouldBeOnTop) noexcept { stickRequested.store (shouldBeOnTop); }
    bool consumeStickinessRequested() noexcept { return stickRequested.exchange (false); }

    void requestAudioDump (juce::AudioBuffer<float>& dest, bool& hasWrapped) const;

    LoudnessMeterState getLoudnessMeterState() const noexcept { return loudnessState; }
    void setLoudnessMeterState (const LoudnessMeterState& newState) noexcept;

    StereoMeterState getStereoMeterState() const noexcept { return stereoState; }
    void setStereoMeterState (const StereoMeterState& newState) noexcept;

    void resetLoudnessStatistics() noexcept;

private:
    static constexpr float kLoudnessHistoryIntervalSeconds = 0.05f;
    static constexpr float kLoudnessHistorySpanSeconds = 20.0f;

    struct SharedState
    {
        juce::SpinLock lock;
        juce::AudioBuffer<float> audioHistory;
        int writePosition = 0;
        bool hasWrapped = false;

        std::vector<float> spectrum;
        std::vector<float> spectrumAverages;

        std::vector<float> waveformMin[2];
        std::vector<float> waveformMax[2];
        std::array<std::array<std::vector<float>, 3>, 2> waveformBandEnergy;
        int waveformSamplesPerBucket = 0;
        int waveformWriteIndex = 0;
        int waveformFilled = 0;
        int waveformSampleCounter = 0;
        float waveformCurrentMin[2] { 1.0f, 1.0f };
        float waveformCurrentMax[2] { -1.0f, -1.0f };
        std::array<std::array<float, 3>, 2> waveformBandAccum {};

        std::vector<float> oscilloscopeBuffer;
        int oscilloscopeWriteIndex = 0;
        int oscilloscopeFilled = 0;

        juce::AudioBuffer<float> spectrogramHistory;
        int spectrogramWritePosition = 0;
        bool spectrogramWrapped = false;

        std::array<juce::Point<float>, 512> lissajousPoints;
        int lissajousCount = 0;

        float momentaryLufs = -100.0f;
        float shortTermLufs = -100.0f;
        float integratedLufs = -100.0f;
        float loudnessRange = 0.0f;
        float maxMomentary = -100.0f;
        float maxShortTerm = -100.0f;
        float rmsFast = 0.0f;
        float rmsSlow = 0.0f;
        float correlation = 0.0f;
        float stereoWidth = 0.0f;
        float leftRms = 0.0f;
        float rightRms = 0.0f;
        float midRms = 0.0f;
        float sideRms = 0.0f;
        float balanceDb = 0.0f;
        float vuNeedleL = 0.0f;
        float vuNeedleR = 0.0f;
        bool clippedL = false;
        bool clippedR = false;
        std::vector<float> loudnessHistory;
        int loudnessHistoryWrite = 0;
        int loudnessHistoryFilled = 0;
        float loudnessHistoryInterval = kLoudnessHistoryIntervalSeconds;
        TransportInfo transport;
    } shared;

    mutable std::atomic<bool> stickRequested { false };

    float sampleRate = 48000.0f;

    float peakRiseCoeff = 0.0f, peakFallCoeff = 0.0f;
    float rmsCoeff = 0.0f;

    float momentaryCoeff = 0.0f, shortTermCoeff = 0.0f;
    float rmsFastCoeff = 0.0f, rmsSlowCoeff = 0.0f;
    float vuCoeff = 0.0f;

    int integratedBlockSamples = 0;
    double integratedEnergyAccumulator = 0.0;
    int integratedSampleCounter = 0;
    std::vector<float> integratedBlockEnergies;
    size_t integratedWriteIndex = 0;
    size_t integratedFilled = 0;
    std::vector<float> integratedScratch;

    int loudnessHistoryIntervalSamples = 0;
    int loudnessHistorySampleCounter = 0;
    int loudnessHistoryCapacity = 0;
    std::vector<float> shortTermHistoryBuffer;
    size_t shortTermHistoryWriteIndex = 0;
    size_t shortTermHistoryFilled = 0;
    std::vector<float> shortTermScratch;

    float integratedLoudness = -100.0f;
    float loudnessRangeValue = 0.0f;
    float maxMomentaryLufs = -100.0f;
    float maxShortTermLufs = -100.0f;

    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { (size_t) 1u << 11, juce::dsp::WindowingFunction<float>::hann, true };
    std::vector<float> fftInput;
    std::vector<float> fftScratch;
    std::vector<float> fftSpectrumFrame;
    int fftInputPos = 0;
    int fftHop = 512;

    juce::dsp::IIR::Filter<float> kPreFilter;
    juce::dsp::IIR::Filter<float> kHighpass;
    std::array<juce::dsp::IIR::Filter<float>, 2> waveformLowFilters;
    std::array<juce::dsp::IIR::Filter<float>, 2> waveformMidHighFilters;
    std::array<juce::dsp::IIR::Filter<float>, 2> waveformMidLowFilters;
    std::array<juce::dsp::IIR::Filter<float>, 2> waveformHighFilters;

    TransportInfo lastTransportInfo;

    juce::AudioBuffer<float> monoScratch;

    float momentaryEnergy = 1.0e-9f;
    float shortTermEnergy = 1.0e-9f;
    float rmsFastEnergy = 1.0e-9f;
    float rmsSlowEnergy = 1.0e-9f;
    float vuEnergyL = 0.0f;
    float vuEnergyR = 0.0f;

    LoudnessMeterState loudnessState {};
    StereoMeterState stereoState {};

    void initialiseSharedState();
    void updateBallistics();
    void pushIntegratedBlock (float energy);
    void updateIntegratedMetrics();
    void pushShortTermHistoryValue (float value);
    void updateLoudnessRange();
    static float energyToLoudness (float energy) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniMetersCloneAudioProcessor)
};
