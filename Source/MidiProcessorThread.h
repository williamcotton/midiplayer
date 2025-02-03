#pragma once
#include <JuceHeader.h>

class MidiProcessorThread : public juce::Thread
{
public:
    MidiProcessorThread()
        : Thread("MidiProcessorThread", 9)
    {
    }

    void setMidiSequence(const juce::MidiMessageSequence& sequence)
    {
        const juce::ScopedLock sl(lock);
        midiSequence = sequence;
        currentEvent = 0;
    }

    void setSynth(juce::Synthesiser* s)
    {
        const juce::ScopedLock sl(lock);
        synth = s;
    }

    void setTempo(double newTempo)
    {
        const juce::ScopedLock sl(lock);
        tempo = newTempo;
    }

    void setLoopRegion(double start, double end, int count)
    {
        const juce::ScopedLock sl(lock);
        loopStartBeat = start;
        loopEndBeat = end;
        loopCount = count;
        currentLoopIteration = 0;
    }

    void startPlayback()
    {
        const juce::ScopedLock sl(lock);
        if (!isThreadRunning() && synth != nullptr)
        {
            // Reset all state
            playbackPosition = 0.0;
            currentEvent = 0;
            currentLoopIteration = 0;
            
            // Turn off any lingering notes
            synth->allNotesOff(0, true);
            for (int channel = 1; channel <= 16; ++channel)
            {
                for (int note = 0; note < 128; ++note)
                {
                    synth->noteOff(channel, note, 0.0f, true);
                }
            }
            
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
            startThread();
        }
    }

    void startPlaybackFromPosition(double beatPosition)
    {
        const juce::ScopedLock sl(lock);
        if (!isThreadRunning() && synth != nullptr)
        {
            // Reset state
            playbackPosition = beatPosition;
            currentEvent = findEventAtTime(convertBeatsToTicks(beatPosition));
            currentLoopIteration = 0;
            
            // Turn off any lingering notes
            synth->allNotesOff(0, true);
            for (int channel = 1; channel <= 16; ++channel)
            {
                for (int note = 0; note < 128; ++note)
                {
                    synth->noteOff(channel, note, 0.0f, true);
                }
            }
            
            lastProcessTimeMs = juce::Time::getMillisecondCounterHiRes();
            startThread();
        }
    }

    void stopPlayback()
    {
        signalThreadShouldExit();
        stopThread(2000);
        
        if (synth != nullptr)
        {
            const juce::ScopedLock sl(lock);
            synth->allNotesOff(0, true);
            for (int channel = 1; channel <= 16; ++channel)
            {
                for (int note = 0; note < 128; ++note)
                {
                    synth->noteOff(channel, note, 0.0f, true);
                }
            }
        }
    }

    double getPlaybackPosition() const
    {
        const juce::ScopedLock sl(lock);
        return playbackPosition;
    }

private:
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

    bool processNextBlock()
    {
        const juce::ScopedLock sl(lock);
        
        if (synth == nullptr || midiSequence.getNumEvents() == 0)
            return false;
            
        auto currentTime = juce::Time::getMillisecondCounterHiRes();
        auto deltaTimeMs = currentTime - lastProcessTimeMs;
        auto deltaBeats = (deltaTimeMs / 1000.0) * (tempo / 60.0);
        
        auto newPosition = playbackPosition + deltaBeats;
        
        // Handle looping
        if (loopCount > 0 && newPosition >= loopEndBeat)
        {
            if (currentLoopIteration < loopCount - 1)
            {
                synth->allNotesOff(0, true);
                newPosition = loopStartBeat;
                currentEvent = findEventAtTime(convertBeatsToTicks(newPosition));
                currentLoopIteration++;
                
                // Scan for notes that should be playing at loop start
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
                            
                            if (noteOnTime <= newPosition && noteOffTime > newPosition)
                            {
                                synth->noteOn(event->message.getChannel(),
                                           event->message.getNoteNumber(),
                                           event->message.getVelocity() / 127.0f);
                            }
                        }
                    }
                }
            }
            else
            {
                // End of last loop iteration - continue playing from loop end
                synth->allNotesOff(0, true);
                currentEvent = findEventAtTime(convertBeatsToTicks(loopEndBeat));
                // Set loop count to 0 to prevent further looping
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
        if (loopCount == 0 && newPosition >= lastEventTime + 1.0) // Add 1 beat buffer
        {
            synth->allNotesOff(0, true);
            return false; // Stop the thread
        }
        
        // Process MIDI events
        while (currentEvent < midiSequence.getNumEvents())
        {
            auto eventTime = convertTicksToBeats(
                midiSequence.getEventPointer(currentEvent)->message.getTimeStamp());
                
            if (eventTime <= newPosition)
            {
                auto& midimsg = midiSequence.getEventPointer(currentEvent)->message;
                
                if (midimsg.isNoteOn())
                {
                    synth->noteOn(midimsg.getChannel(),
                               midimsg.getNoteNumber(),
                               midimsg.getVelocity() / 127.0f);
                }
                else if (midimsg.isNoteOff())
                {
                    synth->noteOff(midimsg.getChannel(),
                                midimsg.getNoteNumber(),
                                midimsg.getVelocity() / 127.0f,
                                true);
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
        return true; // Continue processing
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

    juce::CriticalSection lock;
    juce::MidiMessageSequence midiSequence;
    juce::Synthesiser* synth = nullptr;
    
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
