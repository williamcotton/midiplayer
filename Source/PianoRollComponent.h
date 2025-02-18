#pragma once
#include <JuceHeader.h>

class PianoRollComponent : public juce::Component,
                          public juce::Timer
{
public:
    PianoRollComponent()
    {
        setOpaque(true);
        
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&contentComponent, false);
        viewport.setScrollBarsShown(true, true);
        
        pixelsPerBeat = 50;
        pixelsPerNote = 10;
        numBeats = 16;
        loopStartBeat = 0;
        loopEndBeat = 0;
        loopCount = 0;
        isLooping = false;
        ppq = 480;  // Default PPQ value
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        viewport.setBounds(bounds);
        updateContentSize();
    }

    void setMidiSequence(const juce::MidiMessageSequence& sequence)
    {
        notes.clear();
        DBG("PianoRoll: Setting new MIDI sequence with " + juce::String(sequence.getNumEvents()) + " events");
        
        // First find the absolute last timestamp in the sequence
        double lastTimestamp = 0.0;
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto timestamp = sequence.getEventPointer(i)->message.getTimeStamp();
            lastTimestamp = std::max(lastTimestamp, timestamp);
        }
        
        // Convert to beats and round up to the nearest bar
        double sequenceLength = lastTimestamp / ppq; // Convert to beats using current PPQ
        double beatsPerBar = 4.0; // Assuming 4/4 time
        numBeats = static_cast<int>(std::ceil(sequenceLength / beatsPerBar) * beatsPerBar) + 4;
        
        DBG("PianoRoll: Last timestamp is " + juce::String(lastTimestamp) + " ticks, " + 
            juce::String(sequenceLength) + " beats, setting numBeats to " + juce::String(numBeats));
        
        // Now process all notes
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto* event = sequence.getEventPointer(i);
            if (event->message.isNoteOn())
            {
                Note note;
                note.noteNumber = event->message.getNoteNumber();
                note.startBeat = event->message.getTimeStamp() / ppq;
                note.velocity = event->message.getVelocity();
                
                auto noteOffEvent = event->noteOffObject;
                if (noteOffEvent != nullptr)
                {
                    note.endBeat = noteOffEvent->message.getTimeStamp() / ppq;
                }
                else
                {
                    // If there's no note-off event, extend to the end of the sequence plus one beat
                    note.endBeat = sequenceLength + 1.0;
                    DBG("PianoRoll: Note " + juce::String(note.noteNumber) + 
                        " has no note-off event, extending to " + juce::String(note.endBeat));
                }
                
                notes.add(note);
                DBG("PianoRoll: Added note - Number: " + juce::String(note.noteNumber) + 
                    " Start: " + juce::String(note.startBeat) + 
                    " End: " + juce::String(note.endBeat));
            }
        }
        
        updateContentSize();
        repaint();
    }

    void setLoopRegion(double startBeat, double endBeat, int numberOfLoops)
    {
        loopStartBeat = startBeat;
        loopEndBeat = endBeat;
        loopCount = numberOfLoops;
        isLooping = (loopCount > 0);
        repaint();
    }

    int getLoopCount() const { return loopCount; }
    double getLoopStartBeat() const { return loopStartBeat; }
    double getLoopEndBeat() const { return loopEndBeat; }

    void setPlaybackPosition(double beatPosition)
    {
        currentBeatPosition = beatPosition;
        contentComponent.repaint();
    }

    void startPlayback()
    {
        startTimerHz(120);
    }

    void stopPlayback()
    {
        stopTimer();
        currentBeatPosition = 0.0;
        contentComponent.repaint();
    }

    void timerCallback() override
    {
        // Just trigger a repaint to update the position line
        contentComponent.repaint();
    }

    void setPPQ(int ppqValue) { ppq = ppqValue; }

private:
    struct Note
    {
        int noteNumber;
        double startBeat;
        double endBeat;
        int velocity;
    };

    class ContentComponent : public juce::Component
    {
    public:
        ContentComponent(PianoRollComponent& owner) : owner(owner)
        {
            setOpaque(true);
        }

        void paint(juce::Graphics& g) override
        {
            // auto width = getWidth();
            auto height = getHeight();
            
            // Constants
            const float keyWidth = 40.0f;
            
            g.fillAll(juce::Colours::black);

            // Draw grid first
            g.setColour(juce::Colours::darkgrey);
            for (int beat = 0; beat <= owner.numBeats; ++beat)
            {
                float x = keyWidth + static_cast<float>(beat * owner.pixelsPerBeat);
                g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(height));
            }

            // Draw loop region if active
            if (owner.isLooping)
            {
                g.setColour(juce::Colours::yellow.withAlpha(0.3f));
                float x1 = keyWidth + static_cast<float>(owner.loopStartBeat * owner.pixelsPerBeat);
                float x2 = keyWidth + static_cast<float>(owner.loopEndBeat * owner.pixelsPerBeat);
                g.fillRect(x1, 0.0f, x2 - x1, static_cast<float>(height));
            }

            // Draw notes before piano keys to ensure they don't overlay
            for (auto& note : owner.notes)
            {
                float x = keyWidth + static_cast<float>(note.startBeat * owner.pixelsPerBeat);
                float w = static_cast<float>((note.endBeat - note.startBeat) * owner.pixelsPerBeat);
                float y = height - (note.noteNumber + 1) * owner.pixelsPerNote;
                
                g.setColour(juce::Colour::fromHSV(
                    static_cast<float>(note.noteNumber) / 128.0f, 0.5f, 0.9f, 1.0f));
                
                g.fillRect(x, y, w, static_cast<float>(owner.pixelsPerNote));
            }

            // Draw piano keys as an overlay on the left
            g.saveState();  // Save the current state to create a clipping region
            g.reduceClipRegion(0, 0, static_cast<int>(keyWidth), height);  // Only draw keys in this region
            
            for (int note = 0; note < 128; ++note)
            {
                float y = height - (note + 1) * owner.pixelsPerNote;
                bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);
                
                // Draw white keys first
                if (!isBlackKey)
                {
                    g.setColour(juce::Colours::white);
                    g.fillRect(0.0f, y, keyWidth, static_cast<float>(owner.pixelsPerNote));
                    g.setColour(juce::Colours::black);
                    g.drawRect(0.0f, y, keyWidth, static_cast<float>(owner.pixelsPerNote));
                }
            }
            
            // Draw black keys on top
            for (int note = 0; note < 128; ++note)
            {
                float y = height - (note + 1) * owner.pixelsPerNote;
                bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);
                
                if (isBlackKey)
                {
                    g.setColour(juce::Colours::black);
                    g.fillRect(0.0f, y, keyWidth * 0.6f, static_cast<float>(owner.pixelsPerNote));
                }
            }
            g.restoreState();  // Restore the previous clipping region

            // Draw playback position line
            float playbackX = keyWidth + static_cast<float>(owner.currentBeatPosition * owner.pixelsPerBeat);
            g.setColour(juce::Colours::white);
            g.drawVerticalLine(static_cast<int>(playbackX), 0.0f, static_cast<float>(height));
        }

    private:
        PianoRollComponent& owner;
    };

    void updateContentSize()
    {
        int width = numBeats * pixelsPerBeat;
        int height = 128 * pixelsPerNote;  // 128 MIDI notes
        contentComponent.setSize(width, height);
    }

    juce::Viewport viewport;
    ContentComponent contentComponent { *this };
    juce::Array<Note> notes;
    
    int pixelsPerBeat;
    int pixelsPerNote;
    int numBeats;
    
    double loopStartBeat;
    double loopEndBeat;
    int loopCount;
    bool isLooping;
    double currentBeatPosition = 0.0;
    int ppq = 480;  // Default PPQ value

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};