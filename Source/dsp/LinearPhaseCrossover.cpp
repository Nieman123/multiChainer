#include "LinearPhaseCrossover.h"

#include <cmath>

namespace multichainer::dsp
{
namespace
{
constexpr float minimumCrossoverHz = 20.0f;
constexpr float maximumCrossoverHz = 20000.0f;
constexpr float minimumCrossoverSeparationHz = 20.0f;
constexpr float redesignThresholdHz = 0.5f;

int makeValidTapCount (int requestedTapCount)
{
    auto tapCount = juce::jmax (63, requestedTapCount);

    if ((tapCount % 2) == 0)
        ++tapCount;

    return tapCount;
}
} // namespace

LinearPhaseCrossover::LinearPhaseCrossover (int requestedTapCount)
    : tapCount (makeValidTapCount (requestedTapCount)),
      halfTapCount ((tapCount - 1) / 2),
      designerThread (*this)
{
    for (auto& slot : coefficientSlots)
    {
        slot[0].assign (static_cast<size_t> (tapCount), 0.0f);
        slot[1].assign (static_cast<size_t> (tapCount), 0.0f);
    }

    slotFrequencies[0] = { requestedLowMidHz.load(), requestedMidHighHz.load() };
    slotFrequencies[1] = slotFrequencies[0];

    designerThread.startThread (juce::Thread::Priority::low);
}

LinearPhaseCrossover::~LinearPhaseCrossover()
{
    designerThread.signalThreadShouldExit();
    redesignEvent.signal();
    designerThread.stopThread (1000);
}

void LinearPhaseCrossover::prepare (double sampleRateToUse, int maxBlockSizeToUse, int numChannelsToUse)
{
    isPrepared.store (false, std::memory_order_release);

    sampleRate = juce::jmax (1.0, sampleRateToUse);
    maxBlockSize = juce::jmax (1, maxBlockSizeToUse);
    numChannels = juce::jlimit (1, maxSupportedChannels, numChannelsToUse);

    delayedInput.setSize (numChannels, maxBlockSize, false, false, true);
    lowMidBuffer.setSize (numChannels, maxBlockSize, false, false, true);
    midHighBuffer.setSize (numChannels, maxBlockSize, false, false, true);

    lowBand.setSize (numChannels, maxBlockSize, false, false, true);
    midBand.setSize (numChannels, maxBlockSize, false, false, true);
    highBand.setSize (numChannels, maxBlockSize, false, false, true);

    lowMidFilter.prepare (numChannels, tapCount);
    midHighFilter.prepare (numChannels, tapCount);
    delayCompensator.prepare (numChannels, halfTapCount, maxBlockSize);

    {
        const juce::SpinLock::ScopedLockType lock (designLock);

        auto [f1, f2] = sanitizeCrossovers (requestedLowMidHz.load(), requestedMidHighHz.load(), sampleRate);

        designWindowedSincLowpass (coefficientSlots[0][0], f1, sampleRate);
        designWindowedSincLowpass (coefficientSlots[0][1], f2, sampleRate);

        slotFrequencies[0] = { f1, f2 };
        slotFrequencies[1] = slotFrequencies[0];

        activeSlot.store (0, std::memory_order_release);
        pendingSlot.store (-1, std::memory_order_release);

        lowMidFilter.setCoefficients (coefficientSlots[0][0]);
        midHighFilter.setCoefficients (coefficientSlots[0][1]);

        appliedLowMidHz = f1;
        appliedMidHighHz = f2;
    }

    reset();

    redesignRequested.store (false, std::memory_order_release);
    isPrepared.store (true, std::memory_order_release);
}

void LinearPhaseCrossover::reset()
{
    lowMidFilter.reset();
    midHighFilter.reset();
    delayCompensator.reset();

    delayedInput.clear();
    lowMidBuffer.clear();
    midHighBuffer.clear();
    lowBand.clear();
    midBand.clear();
    highBand.clear();
}

void LinearPhaseCrossover::setTargetFrequencies (float lowMidHz, float midHighHz)
{
    auto [f1, f2] = sanitizeCrossovers (lowMidHz, midHighHz, sampleRate);
    requestRedesignIfNeeded (f1, f2);
}

void LinearPhaseCrossover::process (const juce::AudioBuffer<float>& input, int numSamples)
{
    if (! isPrepared.load (std::memory_order_acquire))
        return;

    jassert (numSamples <= maxBlockSize);

    applyPendingDesignIfAvailable();

    const auto channelsFromInput = input.getNumChannels();
    const auto channelsToCopy = juce::jmin (numChannels, channelsFromInput);

    for (int channel = 0; channel < channelsToCopy; ++channel)
    {
        lowMidBuffer.copyFrom (channel, 0, input, channel, 0, numSamples);
        midHighBuffer.copyFrom (channel, 0, input, channel, 0, numSamples);
    }

    for (int channel = channelsToCopy; channel < numChannels; ++channel)
    {
        lowMidBuffer.clear (channel, 0, numSamples);
        midHighBuffer.clear (channel, 0, numSamples);
    }

    delayCompensator.process (input, delayedInput, numSamples);
    lowMidFilter.process (lowMidBuffer, lowBand, numSamples);
    midHighFilter.process (midHighBuffer, midHighBuffer, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* low = lowBand.getWritePointer (channel);
        auto* mid = midBand.getWritePointer (channel);
        auto* high = highBand.getWritePointer (channel);

        const auto* delayed = delayedInput.getReadPointer (channel);
        const auto* lp2 = midHighBuffer.getReadPointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            mid[sample] = lp2[sample] - low[sample];
            high[sample] = delayed[sample] - lp2[sample];
        }
    }
}

int LinearPhaseCrossover::getLatencySamples() const noexcept
{
    return halfTapCount;
}

void LinearPhaseCrossover::requestRedesignIfNeeded (float sanitizedLowMidHz, float sanitizedMidHighHz)
{
    const auto previousLowMid = requestedLowMidHz.exchange (sanitizedLowMidHz, std::memory_order_release);
    const auto previousMidHigh = requestedMidHighHz.exchange (sanitizedMidHighHz, std::memory_order_release);

    const auto lowMidChanged = std::abs (previousLowMid - sanitizedLowMidHz) > redesignThresholdHz;
    const auto midHighChanged = std::abs (previousMidHigh - sanitizedMidHighHz) > redesignThresholdHz;

    if (! (lowMidChanged || midHighChanged))
        return;

    redesignRequested.store (true, std::memory_order_release);
    redesignEvent.signal();
}

void LinearPhaseCrossover::applyPendingDesignIfAvailable()
{
    if (! designLock.tryEnter())
        return;

    const auto slot = pendingSlot.exchange (-1, std::memory_order_acq_rel);

    if (slot >= 0)
    {
        lowMidFilter.setCoefficients (coefficientSlots[static_cast<size_t> (slot)][0]);
        midHighFilter.setCoefficients (coefficientSlots[static_cast<size_t> (slot)][1]);

        activeSlot.store (slot, std::memory_order_release);
        appliedLowMidHz = slotFrequencies[static_cast<size_t> (slot)].first;
        appliedMidHighHz = slotFrequencies[static_cast<size_t> (slot)].second;
    }

    designLock.exit();
}

std::pair<float, float> LinearPhaseCrossover::sanitizeCrossovers (float lowMidHz,
                                                                  float midHighHz,
                                                                  double sampleRateValue)
{
    const auto nyquistLimited = static_cast<float> (sampleRateValue * 0.49);
    const auto upper = juce::jmax (minimumCrossoverHz + minimumCrossoverSeparationHz,
                                   juce::jmin (maximumCrossoverHz, nyquistLimited));

    auto f1 = juce::jlimit (minimumCrossoverHz,
                            upper - minimumCrossoverSeparationHz,
                            lowMidHz);

    auto f2 = juce::jlimit (f1 + minimumCrossoverSeparationHz,
                            upper,
                            midHighHz);

    if (f2 <= f1)
        f2 = juce::jmin (upper, f1 + minimumCrossoverSeparationHz);

    return { f1, f2 };
}

void LinearPhaseCrossover::designWindowedSincLowpass (std::vector<float>& coefficients,
                                                      float cutoffHz,
                                                      double sampleRateValue)
{
    if (coefficients.empty())
        return;

    const auto taps = static_cast<int> (coefficients.size());
    const auto m = taps - 1;
    const auto clampedCutoff = juce::jlimit (minimumCrossoverHz,
                                             static_cast<float> (sampleRateValue * 0.49),
                                             cutoffHz);

    const auto fc = static_cast<double> (clampedCutoff / sampleRateValue);

    double normalisation = 0.0;

    for (int n = 0; n < taps; ++n)
    {
        const auto centered = static_cast<double> (n) - (static_cast<double> (m) * 0.5);
        const auto x = 2.0 * fc * centered;

        double sinc = 1.0;
        if (std::abs (x) > 1.0e-12)
            sinc = std::sin (juce::MathConstants<double>::pi * x)
                   / (juce::MathConstants<double>::pi * x);

        const auto ideal = 2.0 * fc * sinc;

        const auto phase = (2.0 * juce::MathConstants<double>::pi * static_cast<double> (n))
                           / static_cast<double> (m);

        // 4-term Blackman-Harris for strong sidelobe suppression.
        const auto window = 0.35875
                            - (0.48829 * std::cos (phase))
                            + (0.14128 * std::cos (2.0 * phase))
                            - (0.01168 * std::cos (3.0 * phase));

        const auto value = ideal * window;
        coefficients[static_cast<size_t> (n)] = static_cast<float> (value);
        normalisation += value;
    }

    if (normalisation == 0.0)
        return;

    const auto invNormalisation = static_cast<float> (1.0 / normalisation);

    for (auto& coefficient : coefficients)
        coefficient *= invNormalisation;
}

//==============================================================================
void LinearPhaseCrossover::FIRLowpassFilter::prepare (int numChannelsToUse, int tapCountToUse)
{
    numChannels = juce::jmax (1, numChannelsToUse);
    tapCount = juce::jmax (1, tapCountToUse);
    halfTapCount = (tapCount - 1) / 2;

    history.setSize (numChannels, tapCount, false, false, true);
    history.clear();

    writeIndices.assign (static_cast<size_t> (numChannels), 0);
    coefficients.assign (static_cast<size_t> (tapCount), 0.0f);

    coefficients[static_cast<size_t> (halfTapCount)] = 1.0f;
}

void LinearPhaseCrossover::FIRLowpassFilter::reset()
{
    history.clear();
    std::fill (writeIndices.begin(), writeIndices.end(), 0);
}

void LinearPhaseCrossover::FIRLowpassFilter::setCoefficients (const std::vector<float>& newCoefficients)
{
    jassert (static_cast<int> (newCoefficients.size()) == tapCount);

    if (static_cast<int> (newCoefficients.size()) != tapCount)
        return;

    std::copy (newCoefficients.begin(), newCoefficients.end(), coefficients.begin());
}

void LinearPhaseCrossover::FIRLowpassFilter::process (const juce::AudioBuffer<float>& input,
                                                      juce::AudioBuffer<float>& output,
                                                      int numSamples)
{
    const auto channelsToProcess = juce::jmin (numChannels,
                                               juce::jmin (input.getNumChannels(), output.getNumChannels()));

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        const auto* inputData = input.getReadPointer (channel);
        auto* outputData = output.getWritePointer (channel);
        auto* historyData = history.getWritePointer (channel);

        auto writeIndex = writeIndices[static_cast<size_t> (channel)];

        for (int sample = 0; sample < numSamples; ++sample)
        {
            historyData[writeIndex] = inputData[sample];

            auto centreIndex = writeIndex - halfTapCount;
            if (centreIndex < 0)
                centreIndex += tapCount;

            auto accumulator = coefficients[static_cast<size_t> (halfTapCount)] * historyData[centreIndex];

            for (int tap = 0; tap < halfTapCount; ++tap)
            {
                auto indexA = writeIndex - tap;
                if (indexA < 0)
                    indexA += tapCount;

                auto indexB = writeIndex + 1 + tap;
                if (indexB >= tapCount)
                    indexB -= tapCount;

                accumulator += coefficients[static_cast<size_t> (tap)] * (historyData[indexA] + historyData[indexB]);
            }

            outputData[sample] = accumulator;

            ++writeIndex;
            if (writeIndex >= tapCount)
                writeIndex = 0;
        }

        writeIndices[static_cast<size_t> (channel)] = writeIndex;
    }

    for (int channel = channelsToProcess; channel < output.getNumChannels(); ++channel)
        output.clear (channel, 0, numSamples);
}

//==============================================================================
void LinearPhaseCrossover::DelayCompensator::prepare (int numChannelsToUse, int delaySamplesToUse, int maxBlockSizeInSamples)
{
    numChannels = juce::jmax (1, numChannelsToUse);
    delaySamples = juce::jmax (0, delaySamplesToUse);

    bufferLength = juce::jmax (delaySamples + maxBlockSizeInSamples + 1, delaySamples + 2);

    buffer.setSize (numChannels, bufferLength, false, false, true);
    buffer.clear();

    writeIndices.assign (static_cast<size_t> (numChannels), 0);
}

void LinearPhaseCrossover::DelayCompensator::reset()
{
    buffer.clear();
    std::fill (writeIndices.begin(), writeIndices.end(), 0);
}

void LinearPhaseCrossover::DelayCompensator::process (const juce::AudioBuffer<float>& input,
                                                      juce::AudioBuffer<float>& output,
                                                      int numSamples)
{
    const auto channelsToProcess = juce::jmin (numChannels, output.getNumChannels());

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        const auto* inputData = channel < input.getNumChannels() ? input.getReadPointer (channel) : nullptr;
        auto* outputData = output.getWritePointer (channel);
        auto* delayData = buffer.getWritePointer (channel);

        auto writeIndex = writeIndices[static_cast<size_t> (channel)];

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto in = inputData != nullptr ? inputData[sample] : 0.0f;
            delayData[writeIndex] = in;

            auto readIndex = writeIndex - delaySamples;
            if (readIndex < 0)
                readIndex += bufferLength;

            outputData[sample] = delayData[readIndex];

            ++writeIndex;
            if (writeIndex >= bufferLength)
                writeIndex = 0;
        }

        writeIndices[static_cast<size_t> (channel)] = writeIndex;
    }

    for (int channel = channelsToProcess; channel < output.getNumChannels(); ++channel)
        output.clear (channel, 0, numSamples);
}

//==============================================================================
LinearPhaseCrossover::CoefficientDesignerThread::CoefficientDesignerThread (LinearPhaseCrossover& ownerIn)
    : juce::Thread ("MultiChainer FIR Designer"), owner (ownerIn)
{
}

void LinearPhaseCrossover::CoefficientDesignerThread::run()
{
    while (! threadShouldExit())
    {
        owner.redesignEvent.wait (200);

        if (threadShouldExit())
            return;

        if (! owner.isPrepared.load (std::memory_order_acquire))
            continue;

        if (! owner.redesignRequested.exchange (false, std::memory_order_acq_rel))
            continue;

        const auto requestedLow = owner.requestedLowMidHz.load (std::memory_order_acquire);
        const auto requestedHigh = owner.requestedMidHighHz.load (std::memory_order_acquire);

        auto [f1, f2] = sanitizeCrossovers (requestedLow, requestedHigh, owner.sampleRate);

        const auto writeSlot = 1 - owner.activeSlot.load (std::memory_order_acquire);

        {
            const juce::SpinLock::ScopedLockType lock (owner.designLock);

            designWindowedSincLowpass (owner.coefficientSlots[static_cast<size_t> (writeSlot)][0], f1, owner.sampleRate);
            designWindowedSincLowpass (owner.coefficientSlots[static_cast<size_t> (writeSlot)][1], f2, owner.sampleRate);

            owner.slotFrequencies[static_cast<size_t> (writeSlot)] = { f1, f2 };
            owner.pendingSlot.store (writeSlot, std::memory_order_release);
        }
    }
}
} // namespace multichainer::dsp
