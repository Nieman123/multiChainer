#pragma once

#include <JuceHeader.h>

namespace multichainer::dsp
{
struct MidiTriggerConfig
{
    int midiChannel = 0; // 0 = omni, 1-16 = channel
};

class MidiTrigger
{
public:
    void setConfig (MidiTriggerConfig newConfig);
    bool matchesNoteOn (const juce::MidiMessage& message) const;

private:
    MidiTriggerConfig config;
};
} // namespace multichainer::dsp
