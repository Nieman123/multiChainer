#include "MidiTrigger.h"

namespace multichainer::dsp
{
void MidiTrigger::setConfig (MidiTriggerConfig newConfig)
{
    newConfig.midiChannel = juce::jlimit (0, 16, newConfig.midiChannel);

    config = newConfig;
}

bool MidiTrigger::matchesNoteOn (const juce::MidiMessage& message) const
{
    if (! message.isNoteOn (false))
        return false;

    if (config.midiChannel != 0 && message.getChannel() != config.midiChannel)
        return false;

    return true;
}
} // namespace multichainer::dsp
