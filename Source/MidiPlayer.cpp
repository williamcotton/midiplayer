/*
  ==============================================================================

    MidiPlayer.cpp
    Created: 4 Feb 2025 10:08:57am
    Author:  administrator

  ==============================================================================
*/

#include "MidiPlayer.h"

MidiPlayer::MidiPlayer()
{
    tempo = 120.0;
}

MidiPlayer::~MidiPlayer()
{
}

double MidiPlayer::convertTicksToBeats(double ticks) const
{
    return ticks / 480.0; // assuming standard MIDI PPQ
}

double MidiPlayer::convertBeatsToTicks(double beats) const
{
    return beats * 480.0;
}

double MidiPlayer::convertMillisecondsToBeats(double ms) const
{
    // Convert ms to beats based on tempo
    return (ms / 1000.0) * (tempo / 60.0);
}

double MidiPlayer::convertBeatsToMilliseconds(double beats) const
{
    // Convert beats to ms based on tempo
    return (beats * 60.0 / tempo) * 1000.0;
}

void MidiPlayer::loadMidiFile()
{
    auto fileChooserFlags =
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;
    
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select a MIDI file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");
        
    chooser->launchAsync(fileChooserFlags,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            std::unique_ptr<juce::InputStream> stream;
            
            #if JUCE_ANDROID
            juce::URL::InputStreamOptions options(juce::URL::ParameterHandling::inAddress);
            stream = fc.getURLResult().createInputStream(options);
            #else
            if (result.exists()) {
                stream = std::make_unique<juce::FileInputStream>(result);
            }
            #endif
            
            if (stream != nullptr)
            {
                DBG("Stream opened successfully");
                if (midiFile.readFrom(*stream))
                {
                    DBG("MIDI file read successfully");
                    if (midiFile.getNumTracks() > 0)
                    {
                        midiSequence = *midiFile.getTrack(0);
                        
                        for (int i = 1; i < midiFile.getNumTracks(); ++i)
                        {
                            midiSequence.addSequence(*midiFile.getTrack(i),
                                                   0.0, 0.0,
                                                   midiFile.getLastTimestamp());
                        }
                        
                        midiSequence.updateMatchedPairs();
                        
                        // Call the callback if it's set
                        if (onMidiFileLoadedCallback)
                        {
                            onMidiFileLoadedCallback(midiSequence);
                        }
                    }
                }
            }
        });
}