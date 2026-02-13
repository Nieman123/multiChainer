#pragma once

#include <JuceHeader.h>

#include "EnvelopeFollower.h"
#include "MidiTrigger.h"

namespace multichainer::dsp
{
class MultibandDucker
{
public:
    static constexpr size_t numBands = 3;

    struct BandParameters
    {
        int midiChannel = 0;

        float depthDb = 0.0f;
        float delayMs = 0.0f;
        float attackMs = 20.0f;
        float holdMs = 30.0f;
        float releaseMs = 160.0f;
        float curveShape = 1.0f;
        float smoothing = 0.2f;
    };

    void prepare (double sampleRateToUse, int maxBlockSizeToUse, int numChannelsToUse);
    void reset();

    void setBandParameters (size_t bandIndex, const BandParameters& parameters);

    void clearBlockTriggers();
    void pushMidiMessage (const juce::MidiMessage& message, int sampleOffset, int numSamplesInBlock);

    void processBands (juce::AudioBuffer<float>& lowBand,
                       juce::AudioBuffer<float>& midBand,
                       juce::AudioBuffer<float>& highBand,
                       int numSamples);

private:
    static constexpr int maxTriggersPerBlock = 512;

    struct BandState
    {
        MidiTrigger trigger;
        EnvelopeFollower envelope;
        BandParameters parameters;
        std::array<int, maxTriggersPerBlock> triggerSamples {};
        int numTriggers = 0;
    };

    void processSingleBand (BandState& band, juce::AudioBuffer<float>& audio, int numSamples);

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    int numChannels = 2;
    std::array<BandState, numBands> bands;
};
} // namespace multichainer::dsp
