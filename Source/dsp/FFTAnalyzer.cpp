#include "FFTAnalyzer.h"

namespace multichainer::dsp
{
FFTAnalyzer::FFTAnalyzer()
    : frameStorage (static_cast<size_t> (queueCapacity * numBins), 0.0f)
{
}

void FFTAnalyzer::prepare (int /*expectedSamplesPerBlock*/)
{
    reset();
}

void FFTAnalyzer::reset()
{
    std::fill (fifoSamples.begin(), fifoSamples.end(), 0.0f);
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::fill (scratchFrame.begin(), scratchFrame.end(), -120.0f);

    fifoIndex = 0;
    frameFifo.reset();
}

void FFTAnalyzer::pushBlock (const juce::AudioBuffer<float>& buffer, int channelsToUse)
{
    const auto channels = juce::jlimit (1, juce::jmax (1, buffer.getNumChannels()), channelsToUse);
    const auto numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = 0.0f;

        for (int channel = 0; channel < channels; ++channel)
            mono += buffer.getReadPointer (channel)[sample];

        mono /= static_cast<float> (channels);
        pushSample (mono);
    }
}

bool FFTAnalyzer::popLatestFrame (std::vector<float>& output)
{
    const auto ready = frameFifo.getNumReady();

    if (ready <= 0)
        return false;

    if (static_cast<int> (output.size()) != numBins)
        output.resize (static_cast<size_t> (numBins));

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;

    frameFifo.prepareToRead (ready, start1, size1, start2, size2);

    int lastIndex = -1;

    if (size2 > 0)
        lastIndex = start2 + size2 - 1;
    else if (size1 > 0)
        lastIndex = start1 + size1 - 1;

    if (lastIndex >= 0)
    {
        const auto* source = frameStorage.data() + (lastIndex * numBins);
        std::copy (source, source + numBins, output.begin());
    }

    frameFifo.finishedRead (ready);
    return lastIndex >= 0;
}

void FFTAnalyzer::pushSample (float sample)
{
    fifoSamples[static_cast<size_t> (fifoIndex)] = sample;
    ++fifoIndex;

    if (fifoIndex == fftSize)
    {
        computeFrame();
        fifoIndex = 0;
    }
}

void FFTAnalyzer::computeFrame()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (fifoSamples.begin(), fifoSamples.end(), fftData.begin());

    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto magnitude = fftData[static_cast<size_t> (bin)] / static_cast<float> (fftSize);
        scratchFrame[static_cast<size_t> (bin)] = juce::Decibels::gainToDecibels (magnitude, -120.0f);
    }

    pushFrameToQueue (scratchFrame.data());
}

void FFTAnalyzer::pushFrameToQueue (const float* frame)
{
    if (frameFifo.getFreeSpace() == 0)
    {
        int readStart1 = 0;
        int readSize1 = 0;
        int readStart2 = 0;
        int readSize2 = 0;
        frameFifo.prepareToRead (1, readStart1, readSize1, readStart2, readSize2);
        frameFifo.finishedRead (readSize1 + readSize2);
    }

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;

    frameFifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
        std::copy (frame, frame + numBins, frameStorage.begin() + (start1 * numBins));

    if (size2 > 0)
        std::copy (frame, frame + numBins, frameStorage.begin() + (start2 * numBins));

    frameFifo.finishedWrite (size1 + size2);
}
} // namespace multichainer::dsp
