#pragma once
#include <JuceHeader.h>

class MidiPlayer 
{
public:
    MidiPlayer();
    ~MidiPlayer();

    // Callback type for MIDI file load completion
    using OnMidiFileLoadedCallback = std::function<void(const juce::MidiMessageSequence&)>;

    double tempo; // BPM
    juce::MidiFile midiFile;
    juce::MidiMessageSequence midiSequence;

    double convertTicksToBeats(double ticks) const;
    double convertBeatsToTicks(double beats) const;
    double convertMillisecondsToBeats(double ms) const;
    double convertBeatsToMilliseconds(double beats) const;
    void loadMidiFile();
    
    // Set the callback to be called when MIDI file is loaded
    void setOnMidiFileLoadedCallback(OnMidiFileLoadedCallback callback)
    {
        onMidiFileLoadedCallback = std::move(callback);
    }

private:
    OnMidiFileLoadedCallback onMidiFileLoadedCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
