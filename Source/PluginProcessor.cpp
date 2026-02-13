#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
constexpr auto crossoverLowMidID = "crossover.f1";
constexpr auto crossoverMidHighID = "crossover.f2";

juce::NormalisableRange<float> makeFrequencyRange (float minHz, float maxHz, float centre)
{
    juce::NormalisableRange<float> range (minHz, maxHz, 0.0f, 1.0f);
    range.setSkewForCentre (centre);
    return range;
}

float readRaw (std::atomic<float>* value, float fallback)
{
    return value != nullptr ? value->load (std::memory_order_relaxed) : fallback;
}
} // namespace

MultiChainerAudioProcessor::MultiChainerAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout()),
      crossover (multichainer::dsp::LinearPhaseCrossover::defaultTapCount)
{
    cacheRawParameterPointers();
}

MultiChainerAudioProcessor::~MultiChainerAudioProcessor() = default;

void MultiChainerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    crossover.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    crossover.reset();

    ducker.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
    ducker.reset();

    fftAnalyzer.prepare (samplesPerBlock);
    fftAnalyzer.reset();

    setLatencySamples (crossover.getLatencySamples());
}

void MultiChainerAudioProcessor::releaseResources()
{
}

bool MultiChainerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void MultiChainerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    const auto lowMidHz = readRaw (crossoverLowMid, 200.0f);
    const auto midHighHz = readRaw (crossoverMidHigh, 2500.0f);
    crossover.setTargetFrequencies (lowMidHz, midHighHz);

    for (size_t band = 0; band < bandParameters.size(); ++band)
    {
        const auto& rawBand = bandParameters[band];

        multichainer::dsp::MultibandDucker::BandParameters parameters;
        parameters.midiChannel = juce::roundToInt (readRaw (rawBand.midiChannel, 0.0f));

        parameters.depthDb = readRaw (rawBand.depthDb, 0.0f);
        parameters.delayMs = readRaw (rawBand.delayMs, 0.0f);
        parameters.attackMs = readRaw (rawBand.attackMs, 20.0f);
        parameters.holdMs = readRaw (rawBand.holdMs, 30.0f);
        parameters.releaseMs = readRaw (rawBand.releaseMs, 180.0f);
        parameters.curveShape = readRaw (rawBand.curveShape, 1.0f);
        parameters.smoothing = readRaw (rawBand.smoothing, 0.2f);

        ducker.setBandParameters (band, parameters);
    }

    ducker.clearBlockTriggers();

    uint16_t blockChannelMask = 0;

    for (const auto metadata : midiMessages)
    {
        const auto& message = metadata.getMessage();

        if (message.getChannel() >= 1 && message.getChannel() <= 16)
            blockChannelMask |= static_cast<uint16_t> (1u << static_cast<unsigned> (message.getChannel() - 1));

        ducker.pushMidiMessage (message, metadata.samplePosition, numSamples);
    }

    if (blockChannelMask != 0)
    {
        observedMidiChannelsMask.fetch_or (blockChannelMask, std::memory_order_relaxed);
        midiActivityCounter.fetch_add (1, std::memory_order_relaxed);
    }

    crossover.process (buffer, numSamples);

    auto& lowBand = crossover.getLowBandBuffer();
    auto& midBand = crossover.getMidBandBuffer();
    auto& highBand = crossover.getHighBandBuffer();

    ducker.processBands (lowBand, midBand, highBand, numSamples);

    const auto channelsToMix = juce::jmin (buffer.getNumChannels(),
                                           juce::jmin (lowBand.getNumChannels(),
                                                       juce::jmin (midBand.getNumChannels(), highBand.getNumChannels())));

    for (int channel = 0; channel < channelsToMix; ++channel)
    {
        auto* output = buffer.getWritePointer (channel);

        const auto* low = lowBand.getReadPointer (channel);
        const auto* mid = midBand.getReadPointer (channel);
        const auto* high = highBand.getReadPointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            output[sample] = low[sample] + mid[sample] + high[sample];
    }

    for (int channel = channelsToMix; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    fftAnalyzer.pushBlock (buffer, juce::jmin (2, buffer.getNumChannels()));

    midiMessages.clear();
}

juce::AudioProcessorEditor* MultiChainerAudioProcessor::createEditor()
{
    return new MultiChainerAudioProcessorEditor (*this);
}

bool MultiChainerAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MultiChainerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MultiChainerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool MultiChainerAudioProcessor::producesMidi() const
{
    return false;
}

bool MultiChainerAudioProcessor::isMidiEffect() const
{
    return false;
}

double MultiChainerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MultiChainerAudioProcessor::getNumPrograms()
{
    return 1;
}

int MultiChainerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MultiChainerAudioProcessor::setCurrentProgram (int /*index*/)
{
}

const juce::String MultiChainerAudioProcessor::getProgramName (int /*index*/)
{
    return {};
}

void MultiChainerAudioProcessor::changeProgramName (int /*index*/, const juce::String& /*newName*/)
{
}

void MultiChainerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MultiChainerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
    {
        const auto newState = juce::ValueTree::fromXml (*xmlState);

        if (newState.isValid())
            apvts.replaceState (newState);
    }
}

juce::StringArray MultiChainerAudioProcessor::getParameterIDs() const
{
    juce::StringArray ids;

    ids.add (crossoverLowMidID);
    ids.add (crossoverMidHighID);

    for (int band = 0; band < static_cast<int> (bandParameters.size()); ++band)
    {
        ids.add (getBandParameterID (band, "midiChannel"));
        ids.add (getBandParameterID (band, "depthDb"));
        ids.add (getBandParameterID (band, "delayMs"));
        ids.add (getBandParameterID (band, "attackMs"));
        ids.add (getBandParameterID (band, "holdMs"));
        ids.add (getBandParameterID (band, "releaseMs"));
        ids.add (getBandParameterID (band, "curveShape"));
        ids.add (getBandParameterID (band, "smoothing"));
    }

    return ids;
}

juce::var MultiChainerAudioProcessor::buildParameterSnapshot() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    auto params = std::make_unique<juce::DynamicObject>();

    for (const auto& parameterID : getParameterIDs())
    {
        if (const auto* raw = apvts.getRawParameterValue (parameterID))
            params->setProperty (parameterID, raw->load (std::memory_order_relaxed));
    }

    root->setProperty ("params", juce::var (params.release()));
    root->setProperty ("appliedLowMidHz", crossover.getAppliedLowMidHz());
    root->setProperty ("appliedMidHighHz", crossover.getAppliedMidHighHz());
    root->setProperty ("midi", buildMidiInputSnapshot());

    return juce::var (root.release());
}

juce::var MultiChainerAudioProcessor::buildMidiInputSnapshot() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("activityCounter", static_cast<int64> (midiActivityCounter.load (std::memory_order_relaxed)));

    const auto channelMask = observedMidiChannelsMask.load (std::memory_order_relaxed);
    const auto hasObservedChannels = channelMask != 0;

    juce::Array<juce::var> channels;
    channels.ensureStorageAllocated (16);

    for (int channel = 1; channel <= 16; ++channel)
    {
        const auto bit = static_cast<uint16_t> (1u << static_cast<unsigned> (channel - 1));

        if (! hasObservedChannels || (channelMask & bit) != 0)
        {
            auto channelObject = std::make_unique<juce::DynamicObject>();
            channelObject->setProperty ("value", channel);
            channelObject->setProperty ("name", juce::String ("DAW Ch ") + juce::String (channel));
            channels.add (juce::var (channelObject.release()));
        }
    }

    root->setProperty ("channels", juce::var (channels));
    return juce::var (root.release());
}

void MultiChainerAudioProcessor::setParameterFromUI (const juce::String& parameterID, float value)
{
    if (auto* parameter = apvts.getParameter (parameterID))
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            const auto normalised = ranged->convertTo0to1 (value);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (normalised);
            parameter->endChangeGesture();
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MultiChainerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { crossoverLowMidID, 1 },
        "Low/Mid Crossover",
        makeFrequencyRange (20.0f, 20000.0f, 200.0f),
        180.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { crossoverMidHighID, 1 },
        "Mid/High Crossover",
        makeFrequencyRange (20.0f, 20000.0f, 3000.0f),
        2500.0f));

    for (int band = 0; band < static_cast<int> (multichainer::dsp::MultibandDucker::numBands); ++band)
    {
        const auto bandName = juce::String ("Band ") + juce::String (band + 1) + " ";

        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { getBandParameterID (band, "midiChannel"), 1 },
            bandName + "MIDI Channel",
            0,
            16,
            0));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "depthDb"), 1 },
            bandName + "Depth",
            juce::NormalisableRange<float> (0.0f, 60.0f),
            12.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "delayMs"), 1 },
            bandName + "Delay",
            juce::NormalisableRange<float> (0.0f, 200.0f),
            0.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "attackMs"), 1 },
            bandName + "Attack",
            juce::NormalisableRange<float> (0.0f, 1000.0f),
            20.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "holdMs"), 1 },
            bandName + "Hold",
            juce::NormalisableRange<float> (0.0f, 1000.0f),
            30.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "releaseMs"), 1 },
            bandName + "Release",
            juce::NormalisableRange<float> (1.0f, 3000.0f),
            180.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "curveShape"), 1 },
            bandName + "Curve Shape",
            juce::NormalisableRange<float> (0.1f, 10.0f),
            1.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getBandParameterID (band, "smoothing"), 1 },
            bandName + "Curve Smoothing",
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.2f));
    }

    return layout;
}

void MultiChainerAudioProcessor::cacheRawParameterPointers()
{
    crossoverLowMid = apvts.getRawParameterValue (crossoverLowMidID);
    crossoverMidHigh = apvts.getRawParameterValue (crossoverMidHighID);

    for (int band = 0; band < static_cast<int> (bandParameters.size()); ++band)
    {
        auto& rawBand = bandParameters[static_cast<size_t> (band)];

        rawBand.midiChannel = apvts.getRawParameterValue (getBandParameterID (band, "midiChannel"));

        rawBand.depthDb = apvts.getRawParameterValue (getBandParameterID (band, "depthDb"));
        rawBand.delayMs = apvts.getRawParameterValue (getBandParameterID (band, "delayMs"));
        rawBand.attackMs = apvts.getRawParameterValue (getBandParameterID (band, "attackMs"));
        rawBand.holdMs = apvts.getRawParameterValue (getBandParameterID (band, "holdMs"));
        rawBand.releaseMs = apvts.getRawParameterValue (getBandParameterID (band, "releaseMs"));
        rawBand.curveShape = apvts.getRawParameterValue (getBandParameterID (band, "curveShape"));
        rawBand.smoothing = apvts.getRawParameterValue (getBandParameterID (band, "smoothing"));
    }
}

juce::String MultiChainerAudioProcessor::getBandParameterID (int band, juce::StringRef name)
{
    return juce::String ("band") + juce::String (band + 1) + "." + name;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultiChainerAudioProcessor();
}
