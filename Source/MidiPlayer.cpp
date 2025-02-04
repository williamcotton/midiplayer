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
    state = PlaybackState();
}

MidiPlayer::~MidiPlayer()
{
    stop();
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
                        
                        // Reset playback state
                        state = PlaybackState();
                        
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

void MidiPlayer::play()
{
    if (state.currentEvent < midiSequence.getNumEvents())
    {
        state.isPlaying = true;
        state.lastTime = juce::Time::getMillisecondCounterHiRes();
        
        if (onPlaybackStateCallback)
            onPlaybackStateCallback(true);
    }
}

void MidiPlayer::stop()
{
    if (onAllNotesOffCallback)
        onAllNotesOffCallback();
        
    state.isPlaying = false;
    state.currentEvent = 0;
    state.position = 0.0;
    state.currentLoopIteration = 0;
    
    if (onPlaybackStateCallback)
        onPlaybackStateCallback(false);
}

void MidiPlayer::setPosition(double beatPosition)
{
    state.position = beatPosition;
    state.lastTime = juce::Time::getMillisecondCounterHiRes();
    
    // Find the first event at or after this position
    state.currentEvent = 0;
    while (state.currentEvent < midiSequence.getNumEvents())
    {
        auto eventTime = convertTicksToBeats(
            midiSequence.getEventPointer(state.currentEvent)->message.getTimeStamp());
        if (eventTime >= beatPosition)
            break;
        state.currentEvent++;
    }
    
    if (onPlaybackPositionCallback)
        onPlaybackPositionCallback(beatPosition);
}

void MidiPlayer::processEventsUpToPosition(double position,
                                         const std::function<void(int, int, float)>& noteOn,
                                         const std::function<void(int, int, float)>& noteOff)
{
    while (state.currentEvent < midiSequence.getNumEvents())
    {
        auto eventTime = convertTicksToBeats(
            midiSequence.getEventPointer(state.currentEvent)->message.getTimeStamp());
            
        if (eventTime <= position)
        {
            auto& midimsg = midiSequence.getEventPointer(state.currentEvent)->message;
            
            if (midimsg.isNoteOn())
            {
                noteOn(midimsg.getChannel(),
                      midimsg.getNoteNumber(),
                      midimsg.getVelocity() / 127.0f);
            }
            else if (midimsg.isNoteOff())
            {
                noteOff(midimsg.getChannel(),
                       midimsg.getNoteNumber(),
                       midimsg.getVelocity() / 127.0f);
            }
            
            if (onMidiEventCallback)
                onMidiEventCallback(midimsg);
                
            state.currentEvent++;
        }
        else
        {
            break;
        }
    }
}

void MidiPlayer::update(const std::function<bool(double)>& isInLoop,
                       const std::function<double()>& getLoopStart,
                       const std::function<double()>& getLoopEnd,
                       const std::function<int()>& getLoopCount,
                       const std::function<void(int, int, float)>& noteOn,
                       const std::function<void(int, int, float)>& noteOff,
                       const std::function<void()>& allNotesOff)
{
    if (!state.isPlaying)
        return;

    auto currentTime = juce::Time::getMillisecondCounterHiRes();
    auto deltaTimeMs = currentTime - state.lastTime;
    auto deltaBeats = convertMillisecondsToBeats(deltaTimeMs);
    
    // Update position in beats
    auto newPosition = state.position + deltaBeats;
    
    // Check for looping first
    if (isInLoop(state.position))
    {
        if ((state.currentLoopIteration + 1) < getLoopCount())
        {
            if (newPosition >= getLoopEnd())
            {
                // Turn off all currently playing notes before loop
                allNotesOff();
                
                newPosition = getLoopStart();
                double loopStartTicks = convertBeatsToTicks(newPosition);
                
                // Find the first event that's at or after our loop start point
                state.currentEvent = 0;
                while (state.currentEvent < midiSequence.getNumEvents())
                {
                    auto eventTime = midiSequence.getEventPointer(state.currentEvent)->message.getTimeStamp();
                    if (eventTime >= loopStartTicks)
                        break;
                    state.currentEvent++;
                }
                
                // Step back one event to catch any notes that might start exactly at the loop point
                if (state.currentEvent > 0)
                    state.currentEvent--;
                    
                // Scan for notes that should be playing at loop start
                for (int i = 0; i < state.currentEvent; ++i)
                {
                    auto* event = midiSequence.getEventPointer(i);
                    if (event->message.isNoteOn())
                    {
                        auto noteOff = event->noteOffObject;
                        if (noteOff != nullptr)
                        {
                            auto noteOnTime = convertTicksToBeats(event->message.getTimeStamp());
                            auto noteOffTime = convertTicksToBeats(noteOff->message.getTimeStamp());
                            
                            if (noteOnTime <= newPosition && noteOffTime > newPosition)
                            {
                                noteOn(event->message.getChannel(),
                                     event->message.getNoteNumber(),
                                     event->message.getVelocity() / 127.0f);
                            }
                        }
                    }
                }
                
                state.currentLoopIteration++;
            }
        }
    }
    // Only check for sequence end if we're not looping
    else
    {
        // Find the last event timestamp in ticks
        double lastEventTime = 0.0;
        for (int i = 0; i < midiSequence.getNumEvents(); ++i)
        {
            lastEventTime = std::max(lastEventTime, midiSequence.getEventPointer(i)->message.getTimeStamp());
        }
        double lastEventBeat = convertTicksToBeats(lastEventTime);
        
        // If we've reached the end of the sequence and we're not looping
        if (newPosition >= lastEventBeat + 1.0)
        {
            if (onAllNotesOffCallback)
                onAllNotesOffCallback();
            stop();
            return;
        }
    }
    
    // Process MIDI events
    processEventsUpToPosition(newPosition, noteOn, noteOff);
    
    // Update position for next callback
    state.position = newPosition;
    state.lastTime = currentTime;
    
    if (onPlaybackPositionCallback)
        onPlaybackPositionCallback(newPosition);
}