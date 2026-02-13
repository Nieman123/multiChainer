#pragma once

#include <JuceHeader.h>

#include <atomic>

namespace multichainer::dsp
{
class LinearPhaseCrossover
{
public:
    static constexpr int numBands = 3;
    static constexpr int maxSupportedChannels = 2;
    static constexpr int defaultTapCount = 1025;

    explicit LinearPhaseCrossover (int tapCount = defaultTapCount);
    ~LinearPhaseCrossover();

    void prepare (double sampleRateToUse, int maxBlockSizeToUse, int numChannelsToUse);
    void reset();

    void setTargetFrequencies (float lowMidHz, float midHighHz);
    void process (const juce::AudioBuffer<float>& input, int numSamples);

    int getLatencySamples() const noexcept;

    juce::AudioBuffer<float>& getLowBandBuffer() noexcept { return lowBand; }
    juce::AudioBuffer<float>& getMidBandBuffer() noexcept { return midBand; }
    juce::AudioBuffer<float>& getHighBandBuffer() noexcept { return highBand; }

    const juce::AudioBuffer<float>& getLowBand() const noexcept { return lowBand; }
    const juce::AudioBuffer<float>& getMidBand() const noexcept { return midBand; }
    const juce::AudioBuffer<float>& getHighBand() const noexcept { return highBand; }

    float getAppliedLowMidHz() const noexcept { return appliedLowMidHz; }
    float getAppliedMidHighHz() const noexcept { return appliedMidHighHz; }

private:
    class FIRLowpassFilter
    {
    public:
        void prepare (int numChannels, int tapCount);
        void reset();
        void setCoefficients (const std::vector<float>& newCoefficients);
        void process (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output, int numSamples);

    private:
        int tapCount = 0;
        int halfTapCount = 0;
        int numChannels = 0;

        juce::AudioBuffer<float> history;
        std::vector<int> writeIndices;
        std::vector<float> coefficients;
    };

    class DelayCompensator
    {
    public:
        void prepare (int numChannels, int delaySamples, int maxBlockSizeInSamples);
        void reset();
        void process (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output, int numSamples);

    private:
        int delaySamples = 0;
        int numChannels = 0;
        int bufferLength = 0;

        juce::AudioBuffer<float> buffer;
        std::vector<int> writeIndices;
    };

    class CoefficientDesignerThread : public juce::Thread
    {
    public:
        explicit CoefficientDesignerThread (LinearPhaseCrossover& ownerIn);
        void run() override;

    private:
        LinearPhaseCrossover& owner;
    };

    static void designWindowedSincLowpass (std::vector<float>& coefficients,
                                           float cutoffHz,
                                           double sampleRate);

    void requestRedesignIfNeeded (float sanitizedLowMidHz, float sanitizedMidHighHz);
    void applyPendingDesignIfAvailable();

    static std::pair<float, float> sanitizeCrossovers (float lowMidHz,
                                                       float midHighHz,
                                                       double sampleRate);

    const int tapCount;
    const int halfTapCount;

    std::atomic<bool> isPrepared { false };

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    int numChannels = 2;

    FIRLowpassFilter lowMidFilter;
    FIRLowpassFilter midHighFilter;
    DelayCompensator delayCompensator;

    juce::AudioBuffer<float> delayedInput;
    juce::AudioBuffer<float> lowMidBuffer;
    juce::AudioBuffer<float> midHighBuffer;
    juce::AudioBuffer<float> lowBand;
    juce::AudioBuffer<float> midBand;
    juce::AudioBuffer<float> highBand;

    std::array<std::array<std::vector<float>, 2>, 2> coefficientSlots;
    std::array<std::pair<float, float>, 2> slotFrequencies;

    std::atomic<float> requestedLowMidHz { 200.0f };
    std::atomic<float> requestedMidHighHz { 2500.0f };

    std::atomic<int> activeSlot { 0 };
    std::atomic<int> pendingSlot { -1 };
    std::atomic<bool> redesignRequested { false };

    float appliedLowMidHz = 200.0f;
    float appliedMidHighHz = 2500.0f;

    juce::SpinLock designLock;
    juce::WaitableEvent redesignEvent;
    CoefficientDesignerThread designerThread;
};
} // namespace multichainer::dsp
