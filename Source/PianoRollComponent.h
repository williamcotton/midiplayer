#pragma once
#include <JuceHeader.h>

class PianoRollComponent : public juce::Component,
                          public juce::ChangeListener
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
        
        // Find the sequence length to determine number of beats
        double sequenceLength = 0.0;
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto timestamp = sequence.getEventPointer(i)->message.getTimeStamp();
            sequenceLength = std::max(sequenceLength, timestamp);
        }
        
        numBeats = static_cast<int>(std::ceil(sequenceLength / 480.0)) + 1; // Assuming standard MIDI PPQ of 480
        DBG("PianoRoll: Sequence length is " + juce::String(sequenceLength) + " ticks, setting numBeats to " + juce::String(numBeats));
        
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            auto* event = sequence.getEventPointer(i);
            if (event->message.isNoteOn())
            {
                auto noteOffEvent = event->noteOffObject;
                if (noteOffEvent != nullptr)
                {
                    Note note;
                    note.noteNumber = event->message.getNoteNumber();
                    note.startBeat = event->message.getTimeStamp() / 480.0; // Convert ticks to beats
                    note.endBeat = noteOffEvent->message.getTimeStamp() / 480.0;
                    note.velocity = event->message.getVelocity();
                    notes.add(note);
                    
                    DBG("PianoRoll: Added note - Number: " + juce::String(note.noteNumber) + 
                        " Start: " + juce::String(note.startBeat) + 
                        " End: " + juce::String(note.endBeat));
                }
            }
        }
        
        DBG("PianoRoll: Added " + juce::String(notes.size()) + " notes total");
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

    bool isPositionInLoop(double beat) const
    {
        return isLooping && beat >= loopStartBeat && beat < loopEndBeat;
    }

    int getLoopCount() const { return loopCount; }
    double getLoopStartBeat() const { return loopStartBeat; }
    double getLoopEndBeat() const { return loopEndBeat; }

    void changeListenerCallback(juce::ChangeBroadcaster*) override {}

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
            auto width = getWidth();
            auto height = getHeight();
            DBG("PianoRoll ContentComponent: Painting with dimensions " + 
                juce::String(width) + "x" + juce::String(height));

            g.fillAll(juce::Colours::black);

            // Draw beat lines
            g.setColour(juce::Colours::darkgrey);
            for (int beat = 0; beat < owner.numBeats; ++beat)
            {
                float x = static_cast<float>(beat * owner.pixelsPerBeat);
                g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(height));
            }

            // Draw piano keys
            const float keyWidth = 40.0f;
            for (int note = 0; note < 128; ++note)
            {
                float y = height - (note + 1) * owner.pixelsPerNote;
                bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(note);
                g.setColour(isBlackKey ? juce::Colours::darkgrey : juce::Colours::lightgrey);
                g.fillRect(0.0f, y, keyWidth, static_cast<float>(owner.pixelsPerNote));
            }

            // Draw notes
            DBG("PianoRoll ContentComponent: Drawing " + juce::String(owner.notes.size()) + " notes");
            for (auto& note : owner.notes)
            {
                float x = static_cast<float>(note.startBeat * owner.pixelsPerBeat);
                float w = static_cast<float>((note.endBeat - note.startBeat) * owner.pixelsPerBeat);
                float y = height - (note.noteNumber + 1) * owner.pixelsPerNote;
                
                g.setColour(juce::Colour::fromHSV(
                    static_cast<float>(note.noteNumber) / 128.0f, 0.5f, 0.9f, 1.0f));
                
                DBG("PianoRoll ContentComponent: Drawing note at x=" + juce::String(x) + 
                    " y=" + juce::String(y) + 
                    " w=" + juce::String(w) + 
                    " h=" + juce::String(owner.pixelsPerNote));
                    
                g.fillRect(x + keyWidth, y, w, static_cast<float>(owner.pixelsPerNote));
            }

            if (owner.isLooping)
            {
                g.setColour(juce::Colours::yellow.withAlpha(0.3f));
                float x1 = static_cast<float>(owner.loopStartBeat * owner.pixelsPerBeat);
                float x2 = static_cast<float>(owner.loopEndBeat * owner.pixelsPerBeat);
                g.fillRect(x1, 0.0f, x2 - x1, static_cast<float>(getHeight()));
            }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};