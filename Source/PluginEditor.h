#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/WebUIBridge.h"

class MultiChainerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MultiChainerAudioProcessorEditor (MultiChainerAudioProcessor&);
    ~MultiChainerAudioProcessorEditor() override;

    void resized() override;

private:
    MultiChainerAudioProcessor& audioProcessor;
    multichainer::ui::WebUIBridge webUIBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiChainerAudioProcessorEditor)
};
