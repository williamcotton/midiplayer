#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "MidiProcessorThread.h"
#include "AudioEngine.h"

class MainComponent : public juce::Component,
                     public juce::Timer,
                     public juce::AudioSource,
                     public juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        audioEngine.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }
    
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        audioEngine.getNextAudioBlock(bufferToFill);
    }
    
    void releaseResources() override
    {
        audioEngine.releaseResources();
    }

    // We need to implement both Component's and KeyListener's versions of keyboard event handlers
    // to avoid hiding virtual functions. Component has single-parameter versions while KeyListener 
    // has two-parameter versions. Here we handle both by forwarding the Component versions to our
    // KeyListener implementations to maintain consistent keyboard event handling.
    bool keyPressed(const juce::KeyPress& key) override
    {
        return keyPressed(key, this);
    }
    bool keyPressed(const juce::KeyPress& key, Component* originatingComponent) override;

    bool keyStateChanged(bool isKeyDown) override
    {
        return keyStateChanged(isKeyDown, this);
    }
    bool keyStateChanged(bool isKeyDown, Component* originatingComponent) override
    {
        return false;
    }

private:
    void loadMidiFile();
    void playMidiFile();
    void stopMidiFile();
    void setupLoopRegion();
    void clearLoopRegion();
    int findEventAtTime(double timeStamp);
    double convertTicksToBeats(double ticks) const
    {
        return ticks / 480.0; // assuming standard MIDI PPQ
    }
    
    double convertBeatsToTicks(double beats) const
    {
        return beats * 480.0;
    }
    
    double convertMillisecondsToBeats(double ms) const
    {
        // Convert ms to beats based on tempo
        return (ms / 1000.0) * (tempo / 60.0);
    }
    
    double convertBeatsToMilliseconds(double beats) const
    {
        // Convert beats to ms based on tempo
        return (beats * 60.0 / tempo) * 1000.0;
    }

    // Audio setup
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    AudioEngine audioEngine;
    
    // GUI components
    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton setLoopButton;
    juce::TextButton clearLoopButton;
    PianoRollComponent pianoRoll;

    // MIDI handling
    juce::MidiFile midiFile;
    juce::MidiMessageSequence midiSequence;
    std::unique_ptr<MidiProcessorThread> midiProcessor;

    // Playback state
    bool isPlaying = false;
    int currentEvent = 0;
    double playbackPosition = 0.0;
    double lastTime = 0.0;
    int currentLoopIteration = 0;
    double tempo = 120.0; // BPM
    juce::Slider tempoSlider;
    juce::Label tempoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};