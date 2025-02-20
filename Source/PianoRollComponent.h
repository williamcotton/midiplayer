#pragma once
#include <JuceHeader.h>

class PianoRollComponent : public juce::Component,
                          public juce::Timer,
                          public juce::ScrollBar::Listener
{
public:
    PianoRollComponent()
    {
        setOpaque(true);
        
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&contentComponent, false);
        viewport.setScrollBarsShown(true, true);
        
        // Add listeners for both horizontal and vertical scrollbars
        viewport.getHorizontalScrollBar().addListener(this);
        viewport.getVerticalScrollBar().addListener(this);
        
        pixelsPerBeat = 50;
        pixelsPerNote = 10;
        numBeats = 16;
        loopStartBeat = 0;
        loopEndBeat = 0;
        loopCount = 0;
        isLooping = false;
        ppq = 480;  // Default PPQ value
        timeSignatureNumerator = 4;
        timeSignatureDenominator = 4;
        beatsPerBar = 4.0;  // Default 4/4 time
        transposition = 0;  // No transposition by default
        isPlaying = false;
        isAutoScrolling = false;
        isManuallyScrolling = false;
    }

    ~PianoRollComponent() override
    {
        viewport.getHorizontalScrollBar().removeListener(this);
        viewport.getVerticalScrollBar().removeListener(this);
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
                note.channel = event->message.getChannel() - 1;  // Store MIDI channel (0-15)
                
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
                    " Channel: " + juce::String(note.channel) +
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
        
        // Only auto-scroll if playback is active and not manually scrolling
        if (isPlaying && !isManuallyScrolling)
        {
            // Calculate the x position of the playback line
            const float keyWidth = 40.0f;
            float playbackX = keyWidth + static_cast<float>(currentBeatPosition * pixelsPerBeat);
            
            // Get the current viewport position and size
            auto viewportBounds = viewport.getViewArea();
            float viewportLeft = static_cast<float>(viewportBounds.getX());
            float viewportRight = static_cast<float>(viewportBounds.getRight());
            
            // Check if the playback line is outside the visible area
            if (playbackX < viewportLeft || playbackX > viewportRight)
            {
                // Calculate new viewport position to center the playback line
                targetScrollX = playbackX - (viewportBounds.getWidth() / 2.0f);
                
                // Ensure we don't scroll past the content bounds
                targetScrollX = juce::jlimit(0.0f, 
                                          static_cast<float>(contentComponent.getWidth() - viewportBounds.getWidth()),
                                          targetScrollX);
                
                // Initialize current scroll position if needed
                if (currentScrollX < 0)
                    currentScrollX = static_cast<float>(viewport.getViewPositionX());
            }
        }
        
        contentComponent.repaint();
    }

    void startPlayback()
    {
        isPlaying = true;
        currentScrollX = static_cast<float>(viewport.getViewPositionX());
        targetScrollX = currentScrollX;
        startTimerHz(60); // Increased refresh rate for smoother animation
    }

    void stopPlayback()
    {
        isPlaying = false;
        currentScrollX = -1; // Reset scroll animation state
        stopTimer();
        currentBeatPosition = 0.0;
        contentComponent.repaint();
    }

    void timerCallback() override
    {
        if (isManuallyScrolling)
        {
            isManuallyScrolling = false;
            stopTimer();
            startTimerHz(60); // Restart the normal animation timer
        }
        else
        {
            // Normal animation timer callback
            if (isPlaying && currentScrollX >= 0)
            {
                float diff = targetScrollX - currentScrollX;
                if (std::abs(diff) > 0.5f)
                {
                    currentScrollX += diff * scrollAnimationSpeed;
                    isAutoScrolling = true;  // Set flag before viewport change
                    viewport.setViewPosition(static_cast<int>(currentScrollX), viewport.getViewPositionY());
                    isAutoScrolling = false;  // Reset flag after viewport change
                }
            }
            
            // Update playback line
            contentComponent.repaint();
        }
    }

    void setPPQ(int ppqValue) { ppq = ppqValue; }

    void setTimeSignature(int numerator, int denominator) {
        timeSignatureNumerator = numerator;
        timeSignatureDenominator = denominator;
        beatsPerBar = static_cast<double>(timeSignatureNumerator) * (4.0 / static_cast<double>(timeSignatureDenominator));
        repaint();
    }

    void setTransposition(int semitones)
    {
        transposition = semitones;
        contentComponent.repaint();
    }

    int getTransposition() const { return transposition; }

    // Modify ScrollBar::Listener callback
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override
    {
        if (!isAutoScrolling)
        {            
            isManuallyScrolling = true;
            stopTimer(); // Stop any existing timer
            startTimer(1000); // Start 1 second timeout
        }
    }

private:
    struct Note
    {
        int noteNumber;
        double startBeat;
        double endBeat;
        int velocity;
        int channel;  // Add MIDI channel information

        int getTransposedNoteNumber(int transposition) const
        {
            // Don't transpose if it would go out of MIDI note range (0-127)
            int transposed = noteNumber + transposition;
            return juce::jlimit(0, 127, transposed);
        }
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
            auto height = getHeight();
            
            // Constants
            const float keyWidth = 40.0f;
            
            g.fillAll(juce::Colours::black);

            // Draw grid first
            for (int beat = 0; beat <= owner.numBeats; ++beat)
            {
                float x = keyWidth + static_cast<float>(beat * owner.pixelsPerBeat);
                
                // Draw bar lines darker and thicker
                if (beat % static_cast<int>(owner.beatsPerBar) == 0) {
                    g.setColour(juce::Colours::grey);
                    g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(height));
                } else {
                    g.setColour(juce::Colours::darkgrey.darker());
                    g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(height));
                }
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
                
                // Use transposed note number for y-position
                int transposedNote = note.getTransposedNoteNumber(owner.transposition);
                float y = height - (transposedNote + 1) * owner.pixelsPerNote;
                
                // Calculate hue based on both note number and channel
                float baseHue = static_cast<float>(note.channel) / 16.0f;  // Base hue from channel (0-1)
                float noteHue = static_cast<float>(transposedNote) / 128.0f;  // Note variation (0-1)
                float finalHue = std::fmod(baseHue + (noteHue * 0.2f), 1.0f);  // Combine with smaller note influence
                
                g.setColour(juce::Colour::fromHSV(finalHue, 0.7f, 0.9f, 1.0f));
                
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
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    double beatsPerBar = 4.0;  // Default 4/4 time
    int transposition = 0;  // Number of semitones to transpose (can be negative)
    bool isPlaying = false;

    float targetScrollX = 0.0f;
    float currentScrollX = 0.0f;
    static constexpr float scrollAnimationSpeed = 0.3f; // Lower = smoother but slower

    bool isAutoScrolling = false;
    bool isManuallyScrolling = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};