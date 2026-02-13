#include "MidiTrigger.h"

namespace multichainer::dsp
{
void MidiTrigger::setConfig (MidiTriggerConfig newConfig)
{
    newConfig.midiChannel = juce::jlimit (0, 16, newConfig.midiChannel);
    newConfig.noteMin = juce::jlimit (0, 127, newConfig.noteMin);
    newConfig.noteMax = juce::jlimit (0, 127, newConfig.noteMax);

    if (newConfig.noteMax < newConfig.noteMin)
        std::swap (newConfig.noteMin, newConfig.noteMax);

    config = newConfig;
}

bool MidiTrigger::matchesNoteOn (const juce::MidiMessage& message) const
{
    if (! message.isNoteOn (false))
        return false;

    if (config.midiChannel != 0 && message.getChannel() != config.midiChannel)
        return false;

    const auto note = message.getNoteNumber();
    return note >= config.noteMin && note <= config.noteMax;
}
} // namespace multichainer::dsp
