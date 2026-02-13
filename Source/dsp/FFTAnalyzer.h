#pragma once

#include <JuceHeader.h>

namespace multichainer::dsp
{
class FFTAnalyzer
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2;

    FFTAnalyzer();

    void prepare (int expectedSamplesPerBlock);
    void reset();

    void pushBlock (const juce::AudioBuffer<float>& buffer, int channelsToUse);
    bool popLatestFrame (std::vector<float>& output);

    int getNumBins() const noexcept { return numBins; }

private:
    static constexpr int queueCapacity = 32;

    void pushSample (float sample);
    void computeFrame();
    void pushFrameToQueue (const float* frame);

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { static_cast<size_t> (fftSize),
                                                  juce::dsp::WindowingFunction<float>::hann,
                                                  true };

    std::array<float, fftSize> fifoSamples {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, numBins> scratchFrame {};

    int fifoIndex = 0;

    juce::AbstractFifo frameFifo { queueCapacity };
    std::vector<float> frameStorage;
};
} // namespace multichainer::dsp
