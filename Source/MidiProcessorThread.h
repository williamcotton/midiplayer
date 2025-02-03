#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"

class MidiProcessorThread : public juce::Thread
{
public:
    MidiProcessorThread(AudioEngine& audioEngineToUse)
        : Thread("MidiProcessorThread", 9),
          audioEngine(audioEngineToUse)
    {
    }

    void setMidiSequence(const juce::MidiMessageSequence& sequence)
    {
        const juce::ScopedLock sl(lock);
        stopPlayback(); // Ensure clean state
        midiSequence = sequence;
        currentEvent = 0;
        resetLoopState();
    }

    void setTempo(double newTempo)
    {
        const juce::ScopedLock sl(lock);
        tempo = newTempo;
        // Recalculate timing to maintain position
        if (isThreadRunning())
        {
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
        }
    }

    void setLoopRegion(double start, double end, int count)
    {
        const juce::ScopedLock sl(lock);
        loopStartBeat = start;
        loopEndBeat = end;
        loopCount = count;
        originalLoopCount = count;
        currentLoopIteration = 0;
        
        // If we're currently playing and past the loop end, reset to loop start
        if (isThreadRunning() && playbackPosition >= end)
        {
            playbackPosition = start;
            currentEvent = findEventAtTime(convertBeatsToTicks(start));
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
        }
        
        DBG("Set loop region: start=" + juce::String(start) + 
            " end=" + juce::String(end) + 
            " count=" + juce::String(count));
    }

    void startPlayback()
    {
        const juce::ScopedLock sl(lock);
        if (!isThreadRunning())
        {
            // Reset all state
            playbackPosition = 0.0;
            currentEvent = 0;
            resetLoopState();
            
            // Turn off any lingering notes with immediate timing
            audioEngine.allNotesOff();
            
            // Initialize timing
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
            startThread();
        }
    }

    void startPlaybackFromPosition(double beatPosition)
    {
        const juce::ScopedLock sl(lock);
        if (!isThreadRunning())
        {
            // Reset state
            playbackPosition = beatPosition;
            currentEvent = findEventAtTime(convertBeatsToTicks(beatPosition));
            resetLoopState();
            
            // Turn off any lingering notes with immediate timing
            audioEngine.allNotesOff();
            
            // Initialize timing
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
            
            // Scan for notes that should be playing at this position
            scanAndTriggerActiveNotes(beatPosition);
            
            startThread();
        }
    }

    void stopPlayback()
    {
        // First signal the thread to stop
        signalThreadShouldExit();
        
        // Wait for the thread to finish with a timeout
        if (!stopThread(2000))
        {
            // If thread didn't stop in time, force it
            stopThread(-1);
        }
        
        // Now safely update state
        const juce::ScopedLock sl(lock);
        audioEngine.allNotesOff();
        resetLoopState();
        currentEvent = 0;
        playbackPosition = 0.0;
        lastProcessTimeMs = 0.0;
    }

    double getPlaybackPosition() const
    {
        const juce::ScopedLock sl(lock);
        return playbackPosition;
    }

private:
    void resetLoopState()
    {
        // Already under lock from calling context
        currentLoopIteration = 0;
        loopCount = originalLoopCount;
    }

    void scanAndTriggerActiveNotes(double position)
    {
        // Already under lock from calling context
        for (int i = 0; i < currentEvent; ++i)
        {
            auto* event = midiSequence.getEventPointer(i);
            if (event->message.isNoteOn())
            {
                auto noteOff = event->noteOffObject;
                if (noteOff != nullptr)
                {
                    auto noteOnTime = convertTicksToBeats(event->message.getTimeStamp());
                    auto noteOffTime = convertTicksToBeats(noteOff->message.getTimeStamp());
                    
                    if (noteOnTime <= position && noteOffTime > position)
                    {
                        // Calculate precise timing for the audio thread
                        double timeStamp = (position - noteOnTime) * (60.0 / tempo);
                        audioEngine.noteOn(event->message.getChannel(),
                                       event->message.getNoteNumber(),
                                       event->message.getVelocity() / 127.0f,
                                       timeStamp);
                    }
                }
            }
        }
    }

    bool processNextBlock()
    {
        const juce::ScopedLock sl(lock);
        
        if (midiSequence.getNumEvents() == 0)
            return false;
            
        auto currentTime = juce::Time::getMillisecondCounterHiRes();
        auto deltaTimeMs = currentTime - lastProcessTimeMs;
        auto deltaBeats = (deltaTimeMs / 1000.0) * (tempo / 60.0);
        
        // Calculate the exact timestamp for the audio thread
        double audioThreadTimeStamp = deltaTimeMs / 1000.0;
        
        auto newPosition = playbackPosition + deltaBeats;
        
        // Handle looping
        if (loopCount > 0 && newPosition >= loopEndBeat)
        {
            if (currentLoopIteration < loopCount - 1)
            {
                audioEngine.allNotesOff(audioThreadTimeStamp);
                newPosition = loopStartBeat;
                currentEvent = findEventAtTime(convertBeatsToTicks(newPosition));
                currentLoopIteration++;
                
                // Reset timing for accurate loop start
                lastProcessTimeMs = currentTime;
                audioThreadTimeStamp = 0.0;
                
                // Scan for notes at loop start with adjusted timing
                scanAndTriggerActiveNotes(newPosition);
            }
            else
            {
                // End of last loop iteration - continue playing from loop end
                audioEngine.allNotesOff(audioThreadTimeStamp);
                
                // Set the position exactly at loop end
                newPosition = loopEndBeat;
                
                // Find the first event after the loop end point
                currentEvent = findEventAtTime(convertBeatsToTicks(loopEndBeat));
                
                // Step back one event to catch any notes that might start exactly at the loop end
                if (currentEvent > 0)
                    currentEvent--;
                
                // Scan for notes at loop end with adjusted timing
                scanAndTriggerActiveNotes(newPosition);
                
                // Reset timing for accurate continuation
                lastProcessTimeMs = currentTime;
                audioThreadTimeStamp = 0.0;
                
                // Disable looping
                loopCount = 0;
            }
        }
        
        // Find the last event timestamp
        double lastEventTime = 0.0;
        for (int i = 0; i < midiSequence.getNumEvents(); ++i)
        {
            lastEventTime = std::max(lastEventTime, 
                convertTicksToBeats(midiSequence.getEventPointer(i)->message.getTimeStamp()));
        }
        
        // Check if we've reached the end of the sequence (when not looping)
        if (loopCount == 0 && newPosition >= lastEventTime + 1.0)
        {
            audioEngine.allNotesOff(audioThreadTimeStamp);
            return false;
        }
        
        // Process MIDI events
        while (currentEvent < midiSequence.getNumEvents())
        {
            auto eventTime = convertTicksToBeats(
                midiSequence.getEventPointer(currentEvent)->message.getTimeStamp());
                
            if (eventTime <= newPosition)
            {
                auto& midimsg = midiSequence.getEventPointer(currentEvent)->message;
                
                // Calculate precise timing for this event
                double eventDeltaBeats = newPosition - eventTime;
                double eventTimeStamp = audioThreadTimeStamp - (eventDeltaBeats * 60.0 / tempo);
                
                if (midimsg.isNoteOn())
                {
                    audioEngine.noteOn(midimsg.getChannel(),
                                   midimsg.getNoteNumber(),
                                   midimsg.getVelocity() / 127.0f,
                                   eventTimeStamp);
                }
                else if (midimsg.isNoteOff())
                {
                    audioEngine.noteOff(midimsg.getChannel(),
                                    midimsg.getNoteNumber(),
                                    midimsg.getVelocity() / 127.0f,
                                    eventTimeStamp);
                }
                
                currentEvent++;
            }
            else
            {
                break;
            }
        }
        
        playbackPosition = newPosition;
        lastProcessTimeMs = currentTime;
        return true;
    }

    int findEventAtTime(double timeStamp)
    {
        for (int i = 0; i < midiSequence.getNumEvents(); ++i)
        {
            if (midiSequence.getEventPointer(i)->message.getTimeStamp() >= timeStamp)
                return i;
        }
        return midiSequence.getNumEvents();
    }

    double convertTicksToBeats(double ticks) const
    {
        return ticks / 480.0; // assuming standard MIDI PPQ
    }
    
    double convertBeatsToTicks(double beats) const
    {
        return beats * 480.0;
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            if (!processNextBlock())
            {
                signalThreadShouldExit();
            }
            juce::Thread::sleep(1); // Small sleep to prevent CPU hogging
        }
    }

    juce::CriticalSection lock;
    juce::MidiMessageSequence midiSequence;
    AudioEngine& audioEngine;
    
    int originalLoopCount = 0;
    int currentEvent = 0;
    double playbackPosition = 0.0;
    double lastProcessTimeMs = 0.0;
    double tempo = 120.0;
    
    double loopStartBeat = 0.0;
    double loopEndBeat = 0.0;
    int loopCount = 0;
    int currentLoopIteration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiProcessorThread)
};
