#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "../Modules/SFZero/SFZero.h"
#include "../JuceLibraryCode/BinaryData.h"

// Synthesizer voice
class SineWaveSound : public juce::SynthesiserSound
{
public:
    SineWaveSound() {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class SineWaveVoice : public juce::SynthesiserVoice
{
public:
    SineWaveVoice() {}

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        auto cyclesPerSample = cyclesPerSecond / getSampleRate();
        angleDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
    }

    void stopNote(float velocity, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            if (tailOff == 0.0)
                tailOff = 1.0;
        }
        else
        {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (angleDelta != 0.0)
        {
            if (tailOff > 0.0)
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float)(std::sin(currentAngle) * level * tailOff);
                    
                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample(i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;

                    tailOff *= 0.99;

                    if (tailOff <= 0.005)
                    {
                        clearCurrentNote();
                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float)(std::sin(currentAngle) * level);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample(i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

private:
    double currentAngle = 0.0, angleDelta = 0.0, level = 0.0, tailOff = 0.0;
};

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
    bool isPositionInLoop(double beat) const;
    void timerCallback() override;
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

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
    int findEventIndexForBeat(double beat);
    void processSegment(
        const juce::AudioSourceChannelInfo &bufferInfo, int segmentStartSample,
        int segmentNumSamples, double segmentStartBeat,
        double segmentEndBeat);
    void reTriggerSustainedNotesAt(double loopStartBeat);

    double convertTicksToBeats(double ticks) const {
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

    // File chooser
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Audio setup
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    
    // GUI components
    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton setLoopButton;
    juce::TextButton clearLoopButton;
    juce::ComboBox presetBox;  // Add ComboBox for preset selection
    PianoRollComponent pianoRoll;

    // MIDI handling
    juce::MidiFile midiFile;
    juce::MidiMessageSequence midiSequence;
    juce::Synthesiser synth;

    // Playback state
    bool isPlaying = false;
    int currentEvent = 0;
    std::atomic<double> playbackPosition{0.0};
    double lastTime = 0.0;
    int currentLoopIteration = 0;
    double tempo = 120.0; // BPM
    juce::Slider tempoSlider;
    juce::Label tempoLabel;
    double loopStartBeat;
    double loopEndBeat;
    int loopCount;
    bool isLooping;

    // Add SF2 synth components
    sfzero::Synth sf2Synth;
    bool useSF2Synth = true;  // Default to using SF2 synth
    juce::ReferenceCountedObjectPtr<sfzero::SF2Sound> sf2Sound;
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};