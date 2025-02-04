#pragma once
#include <JuceHeader.h>

class MidiPlayer 
{
public:
    MidiPlayer();
    ~MidiPlayer();

    double tempo; // BPM
    juce::MidiFile midiFile;
    juce::MidiMessageSequence midiSequence;

    double convertTicksToBeats(double ticks) const;
    double convertBeatsToTicks(double beats) const;
    double convertMillisecondsToBeats(double ms) const;
    double convertBeatsToMilliseconds(double beats) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
