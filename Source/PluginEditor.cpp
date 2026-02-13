#include "PluginEditor.h"

MultiChainerAudioProcessorEditor::MultiChainerAudioProcessorEditor (MultiChainerAudioProcessor& processor)
    : AudioProcessorEditor (&processor),
      audioProcessor (processor),
      webUIBridge (audioProcessor)
{
    addAndMakeVisible (webUIBridge);
    setResizable (true, true);
    setResizeLimits (900, 620, 2000, 1400);
    setSize (1280, 820);
}

MultiChainerAudioProcessorEditor::~MultiChainerAudioProcessorEditor() = default;

void MultiChainerAudioProcessorEditor::resized()
{
    webUIBridge.setBounds (getLocalBounds());
}
