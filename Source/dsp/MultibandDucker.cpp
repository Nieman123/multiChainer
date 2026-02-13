#include "MultibandDucker.h"

namespace multichainer::dsp
{
void MultibandDucker::prepare (double sampleRateToUse, int maxBlockSizeToUse, int numChannelsToUse)
{
    sampleRate = juce::jmax (1.0, sampleRateToUse);
    maxBlockSize = juce::jmax (1, maxBlockSizeToUse);
    numChannels = juce::jmax (1, numChannelsToUse);

    for (auto& band : bands)
    {
        band.envelope.prepare (sampleRate);
        band.envelope.reset();
        band.numTriggers = 0;
    }
}

void MultibandDucker::reset()
{
    for (auto& band : bands)
    {
        band.envelope.reset();
        band.numTriggers = 0;
    }
}

void MultibandDucker::setBandParameters (size_t bandIndex, const BandParameters& parameters)
{
    if (bandIndex >= bands.size())
        return;

    auto& band = bands[bandIndex];
    band.parameters = parameters;

    MidiTriggerConfig triggerConfig;
    triggerConfig.midiChannel = parameters.midiChannel;

    band.trigger.setConfig (triggerConfig);

    EnvelopeParams envelope;
    envelope.depthDb = parameters.depthDb;
    envelope.delayMs = parameters.delayMs;
    envelope.attackMs = parameters.attackMs;
    envelope.holdMs = parameters.holdMs;
    envelope.releaseMs = parameters.releaseMs;
    envelope.curveShape = parameters.curveShape;
    envelope.smoothing = parameters.smoothing;

    band.envelope.setParameters (envelope);
}

void MultibandDucker::clearBlockTriggers()
{
    for (auto& band : bands)
        band.numTriggers = 0;
}

void MultibandDucker::pushMidiMessage (const juce::MidiMessage& message, int sampleOffset, int numSamplesInBlock)
{
    if (! message.isNoteOn (false))
        return;

    const auto clampedOffset = juce::jlimit (0, juce::jmax (0, numSamplesInBlock - 1), sampleOffset);

    for (auto& band : bands)
    {
        if (! band.trigger.matchesNoteOn (message))
            continue;

        if (band.numTriggers < maxTriggersPerBlock)
            band.triggerSamples[static_cast<size_t> (band.numTriggers++)] = clampedOffset;
    }
}

void MultibandDucker::processBands (juce::AudioBuffer<float>& lowBand,
                                    juce::AudioBuffer<float>& midBand,
                                    juce::AudioBuffer<float>& highBand,
                                    int numSamples)
{
    processSingleBand (bands[0], lowBand, numSamples);
    processSingleBand (bands[1], midBand, numSamples);
    processSingleBand (bands[2], highBand, numSamples);

    clearBlockTriggers();
}

void MultibandDucker::processSingleBand (BandState& band, juce::AudioBuffer<float>& audio, int numSamples)
{
    const auto channelsToProcess = juce::jmin (numChannels, audio.getNumChannels());

    int triggerIndex = 0;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto triggerNow = false;

        while (triggerIndex < band.numTriggers && band.triggerSamples[static_cast<size_t> (triggerIndex)] == sample)
        {
            triggerNow = true;
            ++triggerIndex;
        }

        const auto gain = band.envelope.processSample (triggerNow);

        for (int channel = 0; channel < channelsToProcess; ++channel)
            audio.getWritePointer (channel)[sample] *= gain;
    }
}
} // namespace multichainer::dsp
