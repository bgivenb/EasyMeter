#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
#include <initializer_list>

MiniMetersCloneAudioProcessor::MiniMetersCloneAudioProcessor()
: AudioProcessor (BusesProperties()
                  .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    initialiseSharedState();
}

void MiniMetersCloneAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = (float) sr;
    setBallistics (10.0f, 300.0f);

    const int historySeconds = 10;
    const int historySamples = juce::jmax (1, (int) std::round (sampleRate * historySeconds));

    integratedBlockSamples = juce::jmax (1, (int) std::round (sampleRate * 0.4f));
    const int integratedBlocks = juce::jmax (1, (int) std::round (600.0f / 0.4f));
    integratedBlockEnergies.assign ((size_t) integratedBlocks, 1.0e-9f);
    integratedScratch.resize ((size_t) integratedBlocks);
    integratedEnergyAccumulator = 0.0;
    integratedSampleCounter = 0;
    integratedWriteIndex = 0;
    integratedFilled = 0;
    integratedLoudness = -100.0f;

    loudnessHistoryIntervalSamples = juce::jmax (1, (int) std::round (sampleRate * kLoudnessHistoryIntervalSeconds));
    loudnessHistorySampleCounter = 0;
    loudnessHistoryCapacity = juce::jmax (1, (int) std::round (kLoudnessHistorySpanSeconds / kLoudnessHistoryIntervalSeconds));
    shortTermHistoryBuffer.assign ((size_t) loudnessHistoryCapacity, -100.0f);
    shortTermHistoryWriteIndex = 0;
    shortTermHistoryFilled = 0;
    shortTermScratch.resize ((size_t) loudnessHistoryCapacity);
    loudnessRangeValue = 0.0f;
    maxMomentaryLufs = -100.0f;
    maxShortTermLufs = -100.0f;

    {
        const juce::SpinLock::ScopedLockType sl (shared.lock);
        shared.audioHistory.setSize (2, historySamples, false, false, true);
        shared.audioHistory.clear();
        shared.writePosition = 0;
        shared.hasWrapped = false;

        shared.waveformSamplesPerBucket = juce::jmax (1, historySamples / kWaveformResolution);
        for (int ch = 0; ch < 2; ++ch)
        {
            shared.waveformMin[ch].assign ((size_t) kWaveformResolution, 0.0f);
            shared.waveformMax[ch].assign ((size_t) kWaveformResolution, 0.0f);
            shared.waveformCurrentMin[ch] = 1.0f;
            shared.waveformCurrentMax[ch] = -1.0f;
            for (int band = 0; band < 3; ++band)
            {
                shared.waveformBandEnergy[(size_t) ch][(size_t) band].assign ((size_t) kWaveformResolution, 0.0f);
                shared.waveformBandAccum[(size_t) ch][(size_t) band] = 0.0f;
            }
        }
        shared.waveformWriteIndex = 0;
        shared.waveformFilled = 0;
        shared.waveformSampleCounter = 0;

        shared.oscilloscopeBuffer.assign ((size_t) kOscilloscopeBufferSize, 0.0f);
        shared.oscilloscopeWriteIndex = 0;
        shared.oscilloscopeFilled = 0;

        const auto fftSize = (int) fft.getSize();
        const int bins = fftSize / 2;
        const double maxSpectrogramSeconds = 3.0;
        const int hopSamples = juce::jmax (1, fftSize / 4);
        const int historyWidth = juce::jmax (1, (int) std::ceil (maxSpectrogramSeconds * sr / (double) hopSamples));
        shared.spectrum.resize ((size_t) bins, 0.0f);
        shared.spectrumAverages.resize ((size_t) bins, 0.0f);
        shared.spectrogramHistory.setSize (bins, historyWidth, false, false, true);
        shared.spectrogramHistory.clear();
        shared.spectrogramWritePosition = 0;
        shared.spectrogramWrapped = false;
        shared.lissajousCount = 0;
        shared.momentaryLufs = shared.shortTermLufs = -100.0f;
        shared.integratedLufs = -100.0f;
        shared.loudnessRange = 0.0f;
        shared.maxMomentary = -100.0f;
        shared.maxShortTerm = -100.0f;
        shared.rmsFast = shared.rmsSlow = 0.0f;
        shared.correlation = 0.0f;
        shared.stereoWidth = 0.0f;
        shared.vuNeedleL = shared.vuNeedleR = 0.0f;
        shared.clippedL = shared.clippedR = false;
        shared.loudnessHistoryInterval = sampleRate > 0.0f ? (float) loudnessHistoryIntervalSamples / sampleRate : 0.0f;
        shared.loudnessHistory.assign ((size_t) loudnessHistoryCapacity, -100.0f);
        shared.loudnessHistoryWrite = 0;
        shared.loudnessHistoryFilled = 0;
    }

    fftSpectrumFrame.assign ((size_t) (fft.getSize() / 2), 0.0f);

    fftInput.resize ((size_t) fft.getSize());
    std::fill (fftInput.begin(), fftInput.end(), 0.0f);
    fftScratch.resize ((size_t) fft.getSize() * 2);
    std::fill (fftScratch.begin(), fftScratch.end(), 0.0f);
    fftInputPos = 0;
    fftHop = juce::jmax (1, fft.getSize() / 4);

    monoScratch.setSize (1, 0);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels = 1;

    kPreFilter.prepare (spec);
    kHighpass.prepare (spec);
    for (auto& filter : waveformLowFilters)
        filter.prepare (spec);
    for (auto& filter : waveformMidHighFilters)
        filter.prepare (spec);
    for (auto& filter : waveformMidLowFilters)
        filter.prepare (spec);
    for (auto& filter : waveformHighFilters)
        filter.prepare (spec);

    auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 1680.0f, 0.707f, juce::Decibels::decibelsToGain (4.0f));
    auto highpass = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 38.0f, 0.5f);
    auto lowBand = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, kWaveformLowCrossoverHz, 0.707f);
    auto midHigh = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, kWaveformLowCrossoverHz, 0.707f);
    auto midLow = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, kWaveformHighCrossoverHz, 0.707f);
    auto highBand = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, kWaveformHighCrossoverHz, 0.707f);
    kPreFilter.coefficients = shelf;
    kHighpass.coefficients = highpass;
    kPreFilter.reset ();
    kHighpass.reset ();
    for (int ch = 0; ch < 2; ++ch)
    {
        waveformLowFilters[(size_t) ch].coefficients = lowBand;
        waveformMidHighFilters[(size_t) ch].coefficients = midHigh;
        waveformMidLowFilters[(size_t) ch].coefficients = midLow;
        waveformHighFilters[(size_t) ch].coefficients = highBand;
        waveformLowFilters[(size_t) ch].reset ();
        waveformMidHighFilters[(size_t) ch].reset ();
        waveformMidLowFilters[(size_t) ch].reset ();
        waveformHighFilters[(size_t) ch].reset ();
    }

    momentaryEnergy = shortTermEnergy = 1.0e-9f;
    rmsFastEnergy = rmsSlowEnergy = 1.0e-9f;
    vuEnergyL = vuEnergyR = 0.0f;

    momentaryCoeff = std::exp (-1.0f / (0.4f * sampleRate));
    shortTermCoeff = std::exp (-1.0f / (3.0f * sampleRate));
    rmsFastCoeff = std::exp (-1.0f / (0.3f * sampleRate));
    rmsSlowCoeff = std::exp (-1.0f / (1.0f * sampleRate));
    vuCoeff = std::exp (-1.0f / (0.3f * sampleRate));
}

bool MiniMetersCloneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in  = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void MiniMetersCloneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numCh = juce::jmin (2, buffer.getNumChannels());
    const int  n     = buffer.getNumSamples();

    TransportInfo transportForBlock = lastTransportInfo;
    transportForBlock.hasInfo = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            transportForBlock.hasInfo = true;

            if (auto bpm = position->getBpm())
                transportForBlock.bpm = *bpm;

            if (auto signature = position->getTimeSignature())
            {
                if (signature->numerator > 0)
                    transportForBlock.timeSigNumerator = signature->numerator;

                if (signature->denominator > 0)
                    transportForBlock.timeSigDenominator = signature->denominator;
            }

            const bool isPlaying = position->getIsPlaying();
            const bool isRecording = position->getIsRecording();

            transportForBlock.isPlaying = isPlaying || isRecording;

            const double sr = getSampleRate();
            const double secondsPerQuarter = transportForBlock.bpm > 0.0 ? 60.0 / transportForBlock.bpm : 0.0;
            const double blockSeconds = (sr > 0.0) ? (double) n / sr : 0.0;
            const double quarterAdvance = (secondsPerQuarter > 0.0) ? blockSeconds / secondsPerQuarter : 0.0;

            const double beatLength = transportForBlock.timeSigDenominator > 0
                                          ? (4.0 / (double) transportForBlock.timeSigDenominator)
                                          : 1.0;
            const int beatsPerBarCount = juce::jmax (1, transportForBlock.timeSigNumerator);
            const double barLength = beatLength * beatsPerBarCount;

            const auto currentQuarterOpt = position->getPpqPosition();
            const auto currentBarStartOpt = position->getPpqPositionOfLastBarStart();

            const double currentQuarter = currentQuarterOpt ? *currentQuarterOpt : transportForBlock.ppqPosition;
            const double currentBarStart = currentBarStartOpt ? *currentBarStartOpt
                                                              : transportForBlock.ppqPositionOfLastBarStart;

            if (std::isfinite (currentQuarter))
                transportForBlock.ppqPosition = currentQuarter;

            if (std::isfinite (currentBarStart))
                transportForBlock.ppqPositionOfLastBarStart = currentBarStart;

            if (quarterAdvance > 0.0 && std::isfinite (currentQuarter))
            {
                const double newQuarter = currentQuarter + quarterAdvance;

                if (barLength > 0.0 && std::isfinite (currentBarStart))
                {
                    double quartersIntoBar = currentQuarter - currentBarStart;

                    if (barLength > 0.0)
                    {
                        const double wraps = std::floor (quartersIntoBar / barLength);
                        if (wraps != 0.0)
                            quartersIntoBar -= wraps * barLength;
                        if (quartersIntoBar < 0.0)
                            quartersIntoBar += barLength;
                    }

                    double quartersEnd = quartersIntoBar + quarterAdvance;
                    const double barsAdvanced = barLength > 0.0 ? std::floor (quartersEnd / barLength) : 0.0;
                    double remainder = std::fmod (quartersEnd, barLength);
                    if (remainder < 0.0)
                        remainder += barLength;

                    transportForBlock.ppqPositionOfLastBarStart = currentBarStart + barsAdvanced * barLength;
                    transportForBlock.ppqPosition = transportForBlock.ppqPositionOfLastBarStart + remainder;
                }
                else
                {
                    transportForBlock.ppqPosition = newQuarter;
                }
            }
        }
    }

    if (! transportForBlock.hasInfo && lastTransportInfo.hasInfo)
        transportForBlock = lastTransportInfo;
    else if (transportForBlock.hasInfo)
        lastTransportInfo = transportForBlock;

    float pL = 0.0f, pR = 0.0f;
    double accL = 0.0, accR = 0.0;
    double midAcc = 0.0, sideAcc = 0.0;

    if (monoScratch.getNumSamples() < n)
        monoScratch.setSize (1, n, false, false, true);

    auto* mono = monoScratch.getWritePointer (0);
    if (numCh > 0)
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), n);
    if (numCh > 1)
        juce::FloatVectorOperations::add (mono, buffer.getReadPointer (1), n);
    if (numCh > 0)
        juce::FloatVectorOperations::multiply (mono, 1.0f / (float) numCh, n);

    if (numCh >= 1)
    {
        const float* l = buffer.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (l[i]);
            pL = (a > pL) ? (peakRiseCoeff * pL + (1.0f - peakRiseCoeff) * a)
                          : (peakFallCoeff * pL + (1.0f - peakFallCoeff) * a);
            accL += (double) (l[i] * l[i]);
        }
    }
    if (numCh >= 2)
    {
        const float* r = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (r[i]);
            pR = (a > pR) ? (peakRiseCoeff * pR + (1.0f - peakRiseCoeff) * a)
                          : (peakFallCoeff * pR + (1.0f - peakFallCoeff) * a);
            accR += (double) (r[i] * r[i]);
        }
    }

    if (numCh >= 1)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
        constexpr double sqrtHalf = 1.0 / juce::MathConstants<double>::sqrt2;

        for (int i = 0; i < n; ++i)
        {
            const double lv = l[i];
            const double rv = r[i];
            const double mid = (lv + rv) * sqrtHalf;
            const double side = (lv - rv) * sqrtHalf;
            midAcc += mid * mid;
            sideAcc += side * side;
        }
    }

    const float midRmsBlock = n > 0 ? std::sqrt ((float) (midAcc / juce::jmax (1, n))) : 0.0f;
    const float sideRmsBlock = n > 0 ? std::sqrt ((float) (sideAcc / juce::jmax (1, n))) : 0.0f;

    const float rmsBlockL = numCh >= 1 ? std::sqrt ((float) (accL / juce::jmax (1, n))) : 0.0f;
    const float rmsBlockR = numCh >= 2 ? std::sqrt ((float) (accR / juce::jmax (1, n))) : rmsBlockL;

    const float prevRmsL = rmsL.load (std::memory_order_relaxed);
    const float prevRmsR = rmsR.load (std::memory_order_relaxed);
    rmsL.store (rmsCoeff * prevRmsL + (1.0f - rmsCoeff) * rmsBlockL, std::memory_order_relaxed);
    rmsR.store (rmsCoeff * prevRmsR + (1.0f - rmsCoeff) * rmsBlockR, std::memory_order_relaxed);
    peakL.store (pL, std::memory_order_relaxed);
    peakR.store (pR, std::memory_order_relaxed);

    juce::dsp::AudioBlock<float> monoBlock (monoScratch);
    juce::dsp::ProcessContextReplacing<float> monoContext (monoBlock);
    kPreFilter.process (monoContext);
    kHighpass.process (monoContext);

    const float* filteredMono = monoScratch.getReadPointer (0);
    float monoEnergy = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float sample = filteredMono[i];
        const float sq = sample * sample;
        monoEnergy += sq;

        if (integratedBlockSamples > 0)
        {
            integratedEnergyAccumulator += sq;
            if (++integratedSampleCounter >= integratedBlockSamples)
            {
                const float avgEnergy = (float) (integratedEnergyAccumulator / (double) integratedBlockSamples);
                pushIntegratedBlock (avgEnergy);
                integratedEnergyAccumulator = 0.0;
                integratedSampleCounter = 0;
            }
        }
    }
    if (n > 0)
        monoEnergy /= (float) n;

    const float blockMomentaryCoeff = std::pow (momentaryCoeff, (float) n);
    const float blockShortCoeff     = std::pow (shortTermCoeff, (float) n);
    momentaryEnergy = blockMomentaryCoeff * momentaryEnergy + (1.0f - blockMomentaryCoeff) * monoEnergy;
    shortTermEnergy = blockShortCoeff * shortTermEnergy + (1.0f - blockShortCoeff) * monoEnergy;

    const float blockFastCoeff = std::pow (rmsFastCoeff, (float) n);
    const float blockSlowCoeff = std::pow (rmsSlowCoeff, (float) n);
    const float blockEnergyAvg = (float) ((accL + accR) / (double) (juce::jmax (1, n) * juce::jmax (1, numCh)));
    rmsFastEnergy = blockFastCoeff * rmsFastEnergy + (1.0f - blockFastCoeff) * blockEnergyAvg;
    rmsSlowEnergy = blockSlowCoeff * rmsSlowEnergy + (1.0f - blockSlowCoeff) * blockEnergyAvg;

    const float blockVuCoeff = std::pow (vuCoeff, (float) n);
    vuEnergyL = blockVuCoeff * vuEnergyL + (1.0f - blockVuCoeff) * rmsBlockL;
    vuEnergyR = blockVuCoeff * vuEnergyR + (1.0f - blockVuCoeff) * rmsBlockR;

    float corr = 0.0f;
    if (numCh == 2)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);
        double dot = 0.0;
        double magLSq = 0.0;
        double magRSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double lv = l[i];
            const double rv = r[i];
            dot += lv * rv;
            magLSq += lv * lv;
            magRSq += rv * rv;
        }
        const float magL = std::sqrt ((float) magLSq);
        const float magR = std::sqrt ((float) magRSq);
        if (magL > 1.0e-9f && magR > 1.0e-9f)
            corr = juce::jlimit (-1.0f, 1.0f, (float) (dot / (magL * magR)));

    }

    const float stereoWidth = juce::jlimit (0.0f, 1.0f, 0.5f * (1.0f - corr));
    const float balanceDb = juce::jlimit (-24.0f, 24.0f,
                                          juce::Decibels::gainToDecibels (rmsBlockR + 1.0e-6f, -80.0f)
                                          - juce::Decibels::gainToDecibels (rmsBlockL + 1.0e-6f, -80.0f));

    const float momentaryLufs = monoEnergy > 0.0f ? -0.691f + 10.0f * std::log10 (momentaryEnergy + 1.0e-12f) : -100.0f;
    const float shortTermLufs = monoEnergy > 0.0f ? -0.691f + 10.0f * std::log10 (shortTermEnergy + 1.0e-12f) : -100.0f;
    maxMomentaryLufs = juce::jmax (maxMomentaryLufs, momentaryLufs);
    maxShortTermLufs = juce::jmax (maxShortTermLufs, shortTermLufs);

    loudnessHistorySampleCounter += n;
    int historyUpdates = 0;
    while (loudnessHistorySampleCounter >= loudnessHistoryIntervalSamples)
    {
        loudnessHistorySampleCounter -= loudnessHistoryIntervalSamples;
        pushShortTermHistoryValue (shortTermLufs);
        ++historyUpdates;
    }
    if (historyUpdates > 0)
        updateLoudnessRange();

    const float rmsFastValue = std::sqrt (juce::jmax (1.0e-12f, rmsFastEnergy));
    const float rmsSlowValue = std::sqrt (juce::jmax (1.0e-12f, rmsSlowEnergy));

    std::array<juce::Point<float>, 512> lissa {};
    int lissaCount = 0;
    if (numCh >= 1)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : nullptr;
        const int step = juce::jmax (1, n / (int) lissa.size());
        for (int i = 0; i < n && lissaCount < (int) lissa.size(); i += step)
        {
            const float lv = l[i];
            const float rv = (r != nullptr) ? r[i] : lv;
            lissa[(size_t) lissaCount++] = { juce::jlimit (-1.0f, 1.0f, lv), juce::jlimit (-1.0f, 1.0f, rv) };
        }
    }

    bool clippedL = juce::FloatVectorOperations::findMaximum (buffer.getReadPointer (0), n) >= 0.999f
                    || juce::FloatVectorOperations::findMinimum (buffer.getReadPointer (0), n) <= -0.999f;
    bool clippedR = numCh > 1 && (juce::FloatVectorOperations::findMaximum (buffer.getReadPointer (1), n) >= 0.999f
                                  || juce::FloatVectorOperations::findMinimum (buffer.getReadPointer (1), n) <= -0.999f);

    bool spectrumFrameUpdated = false;
    const float* monoForFft = monoScratch.getReadPointer (0);
    for (int i = 0; i < n; ++i)
    {
        fftInput[(size_t) fftInputPos++] = monoForFft[i];
        if (fftInputPos >= fft.getSize())
        {
            auto* scratch = fftScratch.data();
            std::copy (fftInput.begin(), fftInput.end(), scratch);
            window.multiplyWithWindowingTable (scratch, (size_t) fft.getSize());
            fft.performRealOnlyForwardTransform (scratch);

            const int bins = fft.getSize() / 2;
            if (fftSpectrumFrame.size() != (size_t) bins)
                fftSpectrumFrame.assign ((size_t) bins, 0.0f);

            auto* spectrumFrameData = fftSpectrumFrame.data();
            for (int bin = 0; bin < bins; ++bin)
            {
                const float re = scratch[bin * 2];
                const float im = scratch[bin * 2 + 1];
                const float mag = std::sqrt (re * re + im * im) / (float) fft.getSize();
                spectrumFrameData[(size_t) bin] = mag;
            }

            spectrumFrameUpdated = true;

            std::rotate (fftInput.begin(), fftInput.begin() + (fft.getSize() - fftHop), fftInput.end());
            fftInputPos = fftHop;
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (shared.lock);

        if (shared.audioHistory.getNumSamples() > 0)
        {
            const int totalSamples = shared.audioHistory.getNumSamples();
            const int prevWrite = shared.writePosition;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = buffer.getReadPointer (ch);
                int remaining = n;
                int destPos = shared.writePosition;
                while (remaining > 0)
                {
                    const int space = totalSamples - destPos;
                    const int toCopy = juce::jmin (space, remaining);
                    shared.audioHistory.copyFrom (ch, destPos, src, toCopy);
                    src += toCopy;
                    destPos = (destPos + toCopy) % totalSamples;
                    remaining -= toCopy;
                }
            }

            shared.writePosition = (shared.writePosition + n) % shared.audioHistory.getNumSamples();
            if (! shared.hasWrapped && prevWrite + n >= totalSamples)
                shared.hasWrapped = true;
        }

        if (shared.waveformSamplesPerBucket > 0 && ! shared.waveformMin[0].empty())
        {
            auto* leftPtr = numCh > 0 ? buffer.getReadPointer (0) : nullptr;
            auto* rightPtr = numCh > 1 ? buffer.getReadPointer (1) : leftPtr;

            int bucketIndex = shared.waveformWriteIndex;
            int filled = shared.waveformFilled;
            int sampleCounter = shared.waveformSampleCounter;
            float currentMinL = shared.waveformCurrentMin[0];
            float currentMaxL = shared.waveformCurrentMax[0];
            float currentMinR = shared.waveformCurrentMin[1];
            float currentMaxR = shared.waveformCurrentMax[1];
            auto& bandAccumL = shared.waveformBandAccum[0];
            auto& bandAccumR = shared.waveformBandAccum[1];

            const int bucketCapacity = (int) shared.waveformMin[0].size();

            for (int i = 0; i < n; ++i)
            {
                const float sampleL = leftPtr != nullptr ? leftPtr[i] : 0.0f;
                const float sampleR = rightPtr != nullptr ? rightPtr[i] : sampleL;

                currentMinL = juce::jmin (currentMinL, sampleL);
                currentMaxL = juce::jmax (currentMaxL, sampleL);
                currentMinR = juce::jmin (currentMinR, sampleR);
                currentMaxR = juce::jmax (currentMaxR, sampleR);

                const float lowL = waveformLowFilters[0].processSample (sampleL);
                float midL = waveformMidHighFilters[0].processSample (sampleL);
                midL = waveformMidLowFilters[0].processSample (midL);
                const float highL = waveformHighFilters[0].processSample (sampleL);
                bandAccumL[0] += lowL * lowL;
                bandAccumL[1] += midL * midL;
                bandAccumL[2] += highL * highL;

                const float lowR = waveformLowFilters[1].processSample (sampleR);
                float midR = waveformMidHighFilters[1].processSample (sampleR);
                midR = waveformMidLowFilters[1].processSample (midR);
                const float highR = waveformHighFilters[1].processSample (sampleR);
                bandAccumR[0] += lowR * lowR;
                bandAccumR[1] += midR * midR;
                bandAccumR[2] += highR * highR;

                if (++sampleCounter >= shared.waveformSamplesPerBucket)
                {
                    shared.waveformMin[0][(size_t) bucketIndex] = currentMinL;
                    shared.waveformMax[0][(size_t) bucketIndex] = currentMaxL;
                    shared.waveformMin[1][(size_t) bucketIndex] = currentMinR;
                    shared.waveformMax[1][(size_t) bucketIndex] = currentMaxR;

                    const float invSamples = 1.0f / (float) shared.waveformSamplesPerBucket;
                    for (int band = 0; band < 3; ++band)
                    {
                        const float bandRmsLeft = std::sqrt (juce::jmax (0.0f, bandAccumL[(size_t) band] * invSamples));
                        const float bandRmsRight = std::sqrt (juce::jmax (0.0f, bandAccumR[(size_t) band] * invSamples));
                        shared.waveformBandEnergy[0][(size_t) band][(size_t) bucketIndex] = bandRmsLeft;
                        shared.waveformBandEnergy[1][(size_t) band][(size_t) bucketIndex] = bandRmsRight;
                        bandAccumL[(size_t) band] = 0.0f;
                        bandAccumR[(size_t) band] = 0.0f;
                    }

                    bucketIndex = (bucketIndex + 1) % bucketCapacity;
                    filled = juce::jmin (bucketCapacity, filled + 1);
                    sampleCounter = 0;
                    currentMinL = 1.0f;
                    currentMaxL = -1.0f;
                    currentMinR = 1.0f;
                    currentMaxR = -1.0f;
                }
            }

            shared.waveformWriteIndex = bucketIndex;
            shared.waveformFilled = filled;
            shared.waveformSampleCounter = sampleCounter;
            shared.waveformCurrentMin[0] = currentMinL;
            shared.waveformCurrentMax[0] = currentMaxL;
            shared.waveformCurrentMin[1] = currentMinR;
            shared.waveformCurrentMax[1] = currentMaxR;
        }

        if (! shared.oscilloscopeBuffer.empty())
        {
            auto* leftPtr = numCh > 0 ? buffer.getReadPointer (0) : nullptr;
            auto* rightPtr = numCh > 1 ? buffer.getReadPointer (1) : leftPtr;

            const int oscSize = (int) shared.oscilloscopeBuffer.size();
            int oscIndex = shared.oscilloscopeWriteIndex;
            int oscFilled = shared.oscilloscopeFilled;

            for (int i = 0; i < n; ++i)
            {
                const float sampleL = leftPtr != nullptr ? leftPtr[i] : 0.0f;
                const float sampleR = rightPtr != nullptr ? rightPtr[i] : sampleL;
                shared.oscilloscopeBuffer[(size_t) oscIndex] = 0.5f * (sampleL + sampleR);
                oscIndex = (oscIndex + 1) % oscSize;
                oscFilled = juce::jmin (oscSize, oscFilled + 1);
            }

            shared.oscilloscopeWriteIndex = oscIndex;
            shared.oscilloscopeFilled = oscFilled;
        }

        if (spectrumFrameUpdated)
        {
            shared.spectrum = fftSpectrumFrame;
            if (shared.spectrumAverages.size() != fftSpectrumFrame.size())
                shared.spectrumAverages.resize (fftSpectrumFrame.size(), 0.0f);
            const float smoothing = 0.6f;
            for (size_t i = 0; i < fftSpectrumFrame.size(); ++i)
                shared.spectrumAverages[i] = smoothing * shared.spectrumAverages[i] + (1.0f - smoothing) * fftSpectrumFrame[i];

            if (shared.spectrogramHistory.getNumSamples() > 0)
            {
                const int bins = juce::jmin ((int) fftSpectrumFrame.size(), shared.spectrogramHistory.getNumChannels());
                for (int bin = 0; bin < bins; ++bin)
                    shared.spectrogramHistory.setSample (bin, shared.spectrogramWritePosition, fftSpectrumFrame[(size_t) bin]);

                shared.spectrogramWritePosition = (shared.spectrogramWritePosition + 1) % shared.spectrogramHistory.getNumSamples();
                if (shared.spectrogramWritePosition == 0)
                    shared.spectrogramWrapped = true;
            }
        }

        shared.lissajousCount = lissaCount;
        if (lissaCount > 0)
            std::copy (lissa.begin(), lissa.begin() + lissaCount, shared.lissajousPoints.begin());

        shared.momentaryLufs = momentaryLufs;
        shared.shortTermLufs = shortTermLufs;
        shared.integratedLufs = integratedLoudness;
        shared.loudnessRange = loudnessRangeValue;
        shared.maxMomentary = maxMomentaryLufs;
        shared.maxShortTerm = maxShortTermLufs;
        shared.rmsFast = rmsFastValue;
        shared.rmsSlow = rmsSlowValue;
        shared.correlation = corr;
        shared.stereoWidth = stereoWidth;
        shared.leftRms = rmsBlockL;
        shared.rightRms = rmsBlockR;
        shared.midRms = midRmsBlock;
        shared.sideRms = sideRmsBlock;
        shared.balanceDb = balanceDb;
        shared.vuNeedleL = vuEnergyL;
        shared.vuNeedleR = vuEnergyR;
        shared.clippedL = clippedL;
        shared.clippedR = clippedR;
        shared.transport = transportForBlock;

        if (historyUpdates > 0 && ! shared.loudnessHistory.empty())
        {
            const int capacity = (int) shared.loudnessHistory.size();
            for (int i = 0; i < historyUpdates; ++i)
            {
                shared.loudnessHistory[(size_t) shared.loudnessHistoryWrite] = shortTermLufs;
                shared.loudnessHistoryWrite = (shared.loudnessHistoryWrite + 1) % capacity;
                shared.loudnessHistoryFilled = juce::jmin (capacity, shared.loudnessHistoryFilled + 1);
            }
        }
    }
}

float MiniMetersCloneAudioProcessor::energyToLoudness (float energy) noexcept
{
    return -0.691f + 10.0f * std::log10 (juce::jmax (1.0e-12f, energy));
}

void MiniMetersCloneAudioProcessor::pushIntegratedBlock (float energy)
{
    if (integratedBlockEnergies.empty())
        return;

    energy = juce::jmax (1.0e-12f, energy);
    const size_t capacity = integratedBlockEnergies.size();
    integratedBlockEnergies[integratedWriteIndex] = energy;
    integratedWriteIndex = (integratedWriteIndex + 1) % capacity;
    integratedFilled = juce::jmin (capacity, integratedFilled + 1);

    updateIntegratedMetrics();
}

void MiniMetersCloneAudioProcessor::updateIntegratedMetrics()
{
    if (integratedFilled == 0 || integratedBlockEnergies.empty())
    {
        integratedLoudness = -100.0f;
        updateLoudnessRange();
        return;
    }

    const size_t capacity = integratedBlockEnergies.size();
    const size_t count = integratedFilled;
    const size_t start = (integratedWriteIndex + capacity - count) % capacity;

    if (integratedScratch.size() < count)
        integratedScratch.resize (capacity);

    size_t valid = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const float energy = integratedBlockEnergies[(start + i) % capacity];
        const float loudness = energyToLoudness (energy);
        if (loudness > -70.0f)
            integratedScratch[valid++] = energy;
    }

    if (valid == 0)
    {
        integratedLoudness = -100.0f;
        updateLoudnessRange();
        return;
    }

    double sum = 0.0;
    for (size_t i = 0; i < valid; ++i)
        sum += integratedScratch[i];

    const double averageEnergy = sum / (double) valid;
    const float averageLoudness = energyToLoudness ((float) averageEnergy);
    const float relativeGate = averageLoudness - 10.0f;

    double gatedSum = 0.0;
    size_t gatedCount = 0;
    for (size_t i = 0; i < valid; ++i)
    {
        const float loudness = energyToLoudness (integratedScratch[i]);
        if (loudness >= relativeGate)
        {
            gatedSum += integratedScratch[i];
            ++gatedCount;
        }
    }

    if (gatedCount == 0)
        integratedLoudness = averageLoudness;
    else
        integratedLoudness = energyToLoudness ((float) (gatedSum / (double) gatedCount));

    updateLoudnessRange();
}

void MiniMetersCloneAudioProcessor::pushShortTermHistoryValue (float value)
{
    if (shortTermHistoryBuffer.empty())
        return;

    value = juce::jlimit (-100.0f, 10.0f, value);
    const size_t capacity = shortTermHistoryBuffer.size();
    shortTermHistoryBuffer[shortTermHistoryWriteIndex] = value;
    shortTermHistoryWriteIndex = (shortTermHistoryWriteIndex + 1) % capacity;
    shortTermHistoryFilled = juce::jmin (capacity, shortTermHistoryFilled + 1);
}

void MiniMetersCloneAudioProcessor::updateLoudnessRange()
{
    if (shortTermHistoryFilled < 2 || shortTermHistoryBuffer.empty())
    {
        loudnessRangeValue = 0.0f;
        return;
    }

    const size_t capacity = shortTermHistoryBuffer.size();
    const size_t count = shortTermHistoryFilled;
    const size_t start = (shortTermHistoryWriteIndex + capacity - count) % capacity;

    if (shortTermScratch.size() < count)
        shortTermScratch.resize (capacity);

    for (size_t i = 0; i < count; ++i)
        shortTermScratch[i] = shortTermHistoryBuffer[(start + i) % capacity];

    const float gatingThreshold = integratedLoudness - 20.0f;
    auto first = shortTermScratch.begin();
    auto last = first + static_cast<std::vector<float>::difference_type> (count);
    auto newEnd = std::remove_if (first, last, [gatingThreshold] (float value)
    {
        return value < gatingThreshold;
    });

    const size_t valid = (size_t) std::distance (first, newEnd);
    if (valid < 2)
    {
        loudnessRangeValue = 0.0f;
        return;
    }

    std::sort (shortTermScratch.begin(), shortTermScratch.begin() + static_cast<std::vector<float>::difference_type> (valid));

    auto percentile = [this, valid] (float p)
    {
        const float index = (valid - 1) * p;
        const size_t lower = (size_t) std::floor (index);
        const size_t upper = juce::jmin (valid - 1, lower + 1);
        const float fraction = index - (float) lower;
        const float lowVal = shortTermScratch[lower];
        const float highVal = shortTermScratch[upper];
        return lowVal + (highVal - lowVal) * fraction;
    };

    const float low = percentile (0.10f);
    const float high = percentile (0.95f);
    loudnessRangeValue = juce::jmax (0.0f, high - low);
}

void MiniMetersCloneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MMCLONE");

    juce::ValueTree loudnessTree ("LOUDNESS");
    loudnessTree.setProperty ("target", loudnessState.targetLufs, nullptr);
    loudnessTree.setProperty ("showRms", loudnessState.showRms, nullptr);
    loudnessTree.setProperty ("history", loudnessState.historySeconds, nullptr);
    state.addChild (loudnessTree, -1, nullptr);

    juce::ValueTree stereoTree ("STEREO");
    stereoTree.setProperty ("viewMode", stereoState.viewMode, nullptr);
    stereoTree.setProperty ("displayMode", stereoState.displayMode, nullptr);
    stereoTree.setProperty ("scopeScale", stereoState.scopeScale, nullptr);
    stereoTree.setProperty ("historySeconds", stereoState.historySeconds, nullptr);
    stereoTree.setProperty ("freeze", stereoState.freeze, nullptr);
    stereoTree.setProperty ("showDots", stereoState.showDots, nullptr);
    stereoTree.setProperty ("persistence", stereoState.persistence, nullptr);
    stereoTree.setProperty ("trailSeconds", stereoState.trailSeconds, nullptr);
    state.addChild (stereoTree, -1, nullptr);

    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void MiniMetersCloneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes)); state.isValid())
    {
        if (auto loudnessTree = state.getChildWithName ("LOUDNESS"); loudnessTree.isValid())
        {
            LoudnessMeterState newState;
            newState.targetLufs = (float) loudnessTree.getProperty ("target", loudnessState.targetLufs);
            newState.showRms = (bool) loudnessTree.getProperty ("showRms", loudnessState.showRms);
            newState.historySeconds = (int) loudnessTree.getProperty ("history", loudnessState.historySeconds);
            setLoudnessMeterState (newState);
        }

        if (auto stereoTree = state.getChildWithName ("STEREO"); stereoTree.isValid())
        {
            StereoMeterState newState;
            newState.viewMode = (int) stereoTree.getProperty ("viewMode", stereoState.viewMode);
            newState.displayMode = (int) stereoTree.getProperty ("displayMode", stereoState.displayMode);
            newState.scopeScale = (float) stereoTree.getProperty ("scopeScale", stereoState.scopeScale);
            newState.historySeconds = (int) stereoTree.getProperty ("historySeconds", stereoState.historySeconds);
            newState.freeze = (bool) stereoTree.getProperty ("freeze", stereoState.freeze);
            newState.showDots = (bool) stereoTree.getProperty ("showDots", stereoState.showDots);
            newState.persistence = (bool) stereoTree.getProperty ("persistence", stereoState.persistence);
            newState.trailSeconds = (float) stereoTree.getProperty ("trailSeconds", stereoState.trailSeconds);

            if (! stereoTree.hasProperty ("displayMode"))
            {
                if (newState.persistence)
                    newState.displayMode = 3;
                else if (newState.showDots)
                    newState.displayMode = 2;
                else
                    newState.displayMode = 1;
            }

            newState.showDots = (newState.displayMode == 2);
            newState.persistence = (newState.displayMode == 3);
            setStereoMeterState (newState);
        }
    }
}

void MiniMetersCloneAudioProcessor::setBallistics (float riseMs, float fallMs)
{
    auto a = [this](float ms){ return std::exp (-1.0f / ( (ms * 0.001f) * sampleRate )); };
    peakRiseCoeff = a (riseMs);
    peakFallCoeff = a (fallMs);
    rmsCoeff = a (300.0f);
}

void MiniMetersCloneAudioProcessor::updateBallistics()
{
    setBallistics (10.0f, 300.0f);
}

juce::AudioProcessorEditor* MiniMetersCloneAudioProcessor::createEditor()
{
    return new MiniMetersCloneAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiniMetersCloneAudioProcessor();
}

void MiniMetersCloneAudioProcessor::initialiseSharedState()
{
    const juce::SpinLock::ScopedLockType sl (shared.lock);
    shared.audioHistory.setSize (2, 1, false, false, true);
    shared.audioHistory.clear();
    shared.writePosition = 0;
    shared.hasWrapped = false;
    shared.waveformSamplesPerBucket = 1;
    for (int ch = 0; ch < 2; ++ch)
    {
        shared.waveformMin[ch].assign ((size_t) kWaveformResolution, 0.0f);
        shared.waveformMax[ch].assign ((size_t) kWaveformResolution, 0.0f);
        shared.waveformCurrentMin[ch] = 1.0f;
        shared.waveformCurrentMax[ch] = -1.0f;
    }
    shared.waveformWriteIndex = 0;
    shared.waveformFilled = 0;
    shared.waveformSampleCounter = 0;
    shared.oscilloscopeBuffer.assign ((size_t) kOscilloscopeBufferSize, 0.0f);
    shared.oscilloscopeWriteIndex = 0;
    shared.oscilloscopeFilled = 0;
    shared.spectrum.clear();
    shared.spectrumAverages.clear();
    shared.spectrogramHistory.setSize (1, 1);
    shared.spectrogramHistory.clear();
    shared.spectrogramWritePosition = 0;
    shared.spectrogramWrapped = false;
    shared.lissajousCount = 0;
    shared.momentaryLufs = shared.shortTermLufs = -100.0f;
    shared.integratedLufs = -100.0f;
    shared.loudnessRange = 0.0f;
    shared.maxMomentary = -100.0f;
    shared.maxShortTerm = -100.0f;
    shared.rmsFast = shared.rmsSlow = 0.0f;
    shared.correlation = shared.stereoWidth = 0.0f;
    shared.leftRms = shared.rightRms = 0.0f;
    shared.midRms = shared.sideRms = 0.0f;
    shared.balanceDb = 0.0f;
    shared.vuNeedleL = shared.vuNeedleR = 0.0f;
    shared.clippedL = shared.clippedR = false;
    shared.loudnessHistory.clear();
    shared.loudnessHistoryWrite = 0;
    shared.loudnessHistoryFilled = 0;
    shared.loudnessHistoryInterval = kLoudnessHistoryIntervalSeconds;
    shared.transport = {};
    lastTransportInfo = {};
}

void MiniMetersCloneAudioProcessor::fillSnapshot (SharedDataSnapshot& snapshot, bool includeAudioHistory) const
{
    const juce::SpinLock::ScopedTryLockType sl (shared.lock);
    if (! sl.isLocked())
        return;

    snapshot.writePosition = shared.writePosition;
    snapshot.bufferWrapped = shared.hasWrapped;

    if (includeAudioHistory && shared.audioHistory.getNumSamples() > 0)
    {
        snapshot.audioHistory.makeCopyOf (shared.audioHistory);
    }
    else if (! includeAudioHistory)
    {
        snapshot.audioHistory.setSize (0, 0);
    }

    snapshot.waveformSamplesPerBucket = shared.waveformSamplesPerBucket;

    const int bucketCapacity = (int) shared.waveformMin[0].size();
    const int validWaveform = juce::jmin (shared.waveformFilled, bucketCapacity);
    if (validWaveform > 0)
    {
        snapshot.waveformLeftMins.resize ((size_t) validWaveform);
        snapshot.waveformLeftMaxs.resize ((size_t) validWaveform);
        snapshot.waveformRightMins.resize ((size_t) validWaveform);
        snapshot.waveformRightMaxs.resize ((size_t) validWaveform);
        snapshot.waveformLeftLowBand.resize ((size_t) validWaveform);
        snapshot.waveformLeftMidBand.resize ((size_t) validWaveform);
        snapshot.waveformLeftHighBand.resize ((size_t) validWaveform);
        snapshot.waveformRightLowBand.resize ((size_t) validWaveform);
        snapshot.waveformRightMidBand.resize ((size_t) validWaveform);
        snapshot.waveformRightHighBand.resize ((size_t) validWaveform);

        const int start = (shared.waveformWriteIndex - validWaveform + bucketCapacity) % bucketCapacity;
        for (int i = 0; i < validWaveform; ++i)
        {
            const int idx = (start + i) % bucketCapacity;
            snapshot.waveformLeftMins[(size_t) i] = shared.waveformMin[0][(size_t) idx];
            snapshot.waveformLeftMaxs[(size_t) i] = shared.waveformMax[0][(size_t) idx];
            snapshot.waveformRightMins[(size_t) i] = shared.waveformMin[1][(size_t) idx];
            snapshot.waveformRightMaxs[(size_t) i] = shared.waveformMax[1][(size_t) idx];
            snapshot.waveformLeftLowBand[(size_t) i] = shared.waveformBandEnergy[0][0][(size_t) idx];
            snapshot.waveformLeftMidBand[(size_t) i] = shared.waveformBandEnergy[0][1][(size_t) idx];
            snapshot.waveformLeftHighBand[(size_t) i] = shared.waveformBandEnergy[0][2][(size_t) idx];
            snapshot.waveformRightLowBand[(size_t) i] = shared.waveformBandEnergy[1][0][(size_t) idx];
            snapshot.waveformRightMidBand[(size_t) i] = shared.waveformBandEnergy[1][1][(size_t) idx];
            snapshot.waveformRightHighBand[(size_t) i] = shared.waveformBandEnergy[1][2][(size_t) idx];
        }
    }
    else
    {
        snapshot.waveformLeftMins.clear();
        snapshot.waveformLeftMaxs.clear();
        snapshot.waveformRightMins.clear();
        snapshot.waveformRightMaxs.clear();
        snapshot.waveformLeftLowBand.clear();
        snapshot.waveformLeftMidBand.clear();
        snapshot.waveformLeftHighBand.clear();
        snapshot.waveformRightLowBand.clear();
        snapshot.waveformRightMidBand.clear();
        snapshot.waveformRightHighBand.clear();
    }

    const int oscValid = juce::jmin ((int) shared.oscilloscopeBuffer.size(), shared.oscilloscopeFilled);
    if (oscValid > 0)
    {
        snapshot.oscilloscope.resize ((size_t) oscValid);
        const int oscSize = (int) shared.oscilloscopeBuffer.size();
        const int start = (shared.oscilloscopeWriteIndex - oscValid + oscSize) % oscSize;
        for (int i = 0; i < oscValid; ++i)
        {
            const int idx = (start + i) % oscSize;
            snapshot.oscilloscope[(size_t) i] = shared.oscilloscopeBuffer[(size_t) idx];
        }
    }
    else
    {
        snapshot.oscilloscope.clear();
    }

    snapshot.spectrum = shared.spectrumAverages;

    if (shared.spectrogramHistory.getNumSamples() > 0)
    {
        snapshot.spectrogram = shared.spectrogramHistory;
        snapshot.spectrogramWritePosition = shared.spectrogramWritePosition;
        snapshot.spectrogramWrapped = shared.spectrogramWrapped;
    }

    snapshot.lissajousCount = shared.lissajousCount;
    for (int i = 0; i < shared.lissajousCount; ++i)
    {
        const auto index = static_cast<size_t> (i);
        snapshot.lissajous[index] = shared.lissajousPoints[index];
    }

    snapshot.momentaryLufs = shared.momentaryLufs;
    snapshot.shortTermLufs = shared.shortTermLufs;
    snapshot.integratedLufs = shared.integratedLufs;
    snapshot.loudnessRange = shared.loudnessRange;
    snapshot.maxMomentaryLufs = shared.maxMomentary;
    snapshot.maxShortTermLufs = shared.maxShortTerm;
    snapshot.rmsFast = shared.rmsFast;
    snapshot.rmsSlow = shared.rmsSlow;
    snapshot.correlation = shared.correlation;
    snapshot.stereoWidth = shared.stereoWidth;
    snapshot.leftRms = shared.leftRms;
    snapshot.rightRms = shared.rightRms;
    snapshot.midRms = shared.midRms;
    snapshot.sideRms = shared.sideRms;
    snapshot.balanceDb = shared.balanceDb;
    snapshot.vuNeedleL = shared.vuNeedleL;
    snapshot.vuNeedleR = shared.vuNeedleR;
    snapshot.clipLeft = shared.clippedL;
    snapshot.clipRight = shared.clippedR;
    snapshot.peakLeft = peakL.load (std::memory_order_relaxed);
    snapshot.peakRight = peakR.load (std::memory_order_relaxed);
    snapshot.sampleRate = getSampleRate();
    snapshot.loudnessHistoryInterval = shared.loudnessHistoryInterval;
    snapshot.transport = shared.transport;

    const int loudnessValid = juce::jmin ((int) shared.loudnessHistory.size(), shared.loudnessHistoryFilled);
    if (loudnessValid > 0)
    {
        snapshot.loudnessHistory.resize ((size_t) loudnessValid);
        const int capacity = (int) shared.loudnessHistory.size();
        const int start = (shared.loudnessHistoryWrite - loudnessValid + capacity) % capacity;
        for (int i = 0; i < loudnessValid; ++i)
        {
            const int idx = (start + i) % capacity;
            snapshot.loudnessHistory[(size_t) i] = shared.loudnessHistory[(size_t) idx];
        }
    }
    else
    {
        snapshot.loudnessHistory.clear();
    }
}

void MiniMetersCloneAudioProcessor::requestAudioDump (juce::AudioBuffer<float>& dest, bool& hasWrapped) const
{
    const juce::SpinLock::ScopedTryLockType sl (shared.lock);
    if (! sl.isLocked())
        return;

    hasWrapped = shared.hasWrapped;
    dest.makeCopyOf (shared.audioHistory);
}

static int sanitiseHistorySeconds (int value, std::initializer_list<int> allowed)
{
    if (allowed.size() == 0)
        return value;

    int closest = *allowed.begin();
    int smallestDiff = std::abs (value - closest);
    for (int option : allowed)
    {
        const int diff = std::abs (value - option);
        if (diff < smallestDiff)
        {
            smallestDiff = diff;
            closest = option;
        }
    }

    return closest;
}

void MiniMetersCloneAudioProcessor::setLoudnessMeterState (const LoudnessMeterState& newState) noexcept
{
    LoudnessMeterState sanitised = newState;
    sanitised.targetLufs = juce::jlimit (-36.0f, -6.0f, sanitised.targetLufs);
    sanitised.historySeconds = sanitiseHistorySeconds (sanitised.historySeconds, { 20, 60, 120 });
    loudnessState = sanitised;
}

void MiniMetersCloneAudioProcessor::setStereoMeterState (const StereoMeterState& newState) noexcept
{
    StereoMeterState sanitised = newState;
    sanitised.viewMode = (sanitised.viewMode == 2) ? 2 : 1;
    sanitised.displayMode = juce::jlimit (1, 3, sanitised.displayMode);
    sanitised.scopeScale = juce::jlimit (0.5f, 2.0f, sanitised.scopeScale);
    sanitised.historySeconds = sanitiseHistorySeconds (sanitised.historySeconds, { 3, 6, 12, 24 });
    sanitised.freeze = newState.freeze;
    sanitised.trailSeconds = juce::jlimit (0.2f, 3.0f, sanitised.trailSeconds);
    sanitised.showDots = (sanitised.displayMode == 2);
    sanitised.persistence = (sanitised.displayMode == 3);
    stereoState = sanitised;
}

void MiniMetersCloneAudioProcessor::resetLoudnessStatistics() noexcept
{
    maxMomentaryLufs = -100.0f;
    maxShortTermLufs = -100.0f;
    peakL.store (0.0f, std::memory_order_relaxed);
    peakR.store (0.0f, std::memory_order_relaxed);

    const juce::SpinLock::ScopedLockType sl (shared.lock);
    shared.maxMomentary = -100.0f;
    shared.maxShortTerm = -100.0f;
    shared.clippedL = false;
    shared.clippedR = false;
    shared.loudnessHistoryWrite = 0;
    shared.loudnessHistoryFilled = 0;
    std::fill (shared.loudnessHistory.begin(), shared.loudnessHistory.end(), -100.0f);
}
