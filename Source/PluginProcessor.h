#pragma once

#include <JuceHeader.h>

#include "dsp/FFTAnalyzer.h"
#include "dsp/LinearPhaseCrossover.h"
#include "dsp/MultibandDucker.h"

class MultiChainerAudioProcessor final : public juce::AudioProcessor
{
public:
    MultiChainerAudioProcessor();
    ~MultiChainerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }

    multichainer::dsp::FFTAnalyzer& getFFTAnalyzer() noexcept { return fftAnalyzer; }

    juce::StringArray getParameterIDs() const;
    juce::var buildParameterSnapshot() const;
    juce::var buildMidiInputSnapshot() const;
    void setParameterFromUI (const juce::String& parameterID, float value);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct BandRawParameters
    {
        std::atomic<float>* midiChannel = nullptr;

        std::atomic<float>* depthDb = nullptr;
        std::atomic<float>* delayMs = nullptr;
        std::atomic<float>* attackMs = nullptr;
        std::atomic<float>* holdMs = nullptr;
        std::atomic<float>* releaseMs = nullptr;
        std::atomic<float>* curveShape = nullptr;
        std::atomic<float>* smoothing = nullptr;
    };

    void cacheRawParameterPointers();

    static juce::String getBandParameterID (int band, juce::StringRef name);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* crossoverLowMid = nullptr;
    std::atomic<float>* crossoverMidHigh = nullptr;

    std::array<BandRawParameters, multichainer::dsp::MultibandDucker::numBands> bandParameters;

    multichainer::dsp::LinearPhaseCrossover crossover;
    multichainer::dsp::MultibandDucker ducker;
    multichainer::dsp::FFTAnalyzer fftAnalyzer;

    std::atomic<uint32_t> midiActivityCounter { 0 };
    std::atomic<uint16_t> observedMidiChannelsMask { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiChainerAudioProcessor)
};
