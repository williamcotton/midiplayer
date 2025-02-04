#pragma once
#include <JuceHeader.h>

class MidiPlayer 
{
public:
    MidiPlayer();
    ~MidiPlayer();

    struct PlaybackState {
        bool isPlaying = false;
        double position = 0.0;
        int currentEvent = 0;
        double lastTime = 0.0;
        int currentLoopIteration = 0;
    };

    // Callback types
    using OnMidiFileLoadedCallback = std::function<void(const juce::MidiMessageSequence&)>;
    using OnPlaybackPositionCallback = std::function<void(double position)>;
    using OnMidiEventCallback = std::function<void(const juce::MidiMessage&)>;
    using OnPlaybackStateCallback = std::function<void(bool isPlaying)>;
    using OnAllNotesOffCallback = std::function<void()>;

    double tempo; // BPM
    juce::MidiFile midiFile;
    juce::MidiMessageSequence midiSequence;

    // Time conversion utilities
    double convertTicksToBeats(double ticks) const;
    double convertBeatsToTicks(double beats) const;
    double convertMillisecondsToBeats(double ms) const;
    double convertBeatsToMilliseconds(double beats) const;

    // File and playback control
    void loadMidiFile();
    void play();
    void stop();
    void setPosition(double beatPosition);
    void update(const std::function<bool(double)>& isInLoop,
                const std::function<double()>& getLoopStart,
                const std::function<double()>& getLoopEnd,
                const std::function<int()>& getLoopCount,
                const std::function<void(int, int, float)>& noteOn,
                const std::function<void(int, int, float)>& noteOff,
                const std::function<void()>& allNotesOff);

    // Callback setters
    void setOnMidiFileLoaded(OnMidiFileLoadedCallback callback) { onMidiFileLoadedCallback = std::move(callback); }
    void setOnPlaybackPosition(OnPlaybackPositionCallback callback) { onPlaybackPositionCallback = std::move(callback); }
    void setOnMidiEvent(OnMidiEventCallback callback) { onMidiEventCallback = std::move(callback); }
    void setOnPlaybackState(OnPlaybackStateCallback callback) { onPlaybackStateCallback = std::move(callback); }
    void setOnAllNotesOff(OnAllNotesOffCallback callback) { onAllNotesOffCallback = std::move(callback); }

    // State accessors
    bool isPlaying() const { return state.isPlaying; }
    double getPosition() const { return state.position; }
    int getCurrentEvent() const { return state.currentEvent; }

private:
    PlaybackState state;
    OnMidiFileLoadedCallback onMidiFileLoadedCallback;
    OnPlaybackPositionCallback onPlaybackPositionCallback;
    OnMidiEventCallback onMidiEventCallback;
    OnPlaybackStateCallback onPlaybackStateCallback;
    OnAllNotesOffCallback onAllNotesOffCallback;

    void processEventsUpToPosition(double position, 
                                 const std::function<void(int, int, float)>& noteOn,
                                 const std::function<void(int, int, float)>& noteOff);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
