#pragma once
#include "../JuceLibraryCode/BinaryData.h"
#include "../Modules/SFZero/SFZero.h"
#include "PianoRollComponent.h"
#include "MidiSchedulerAudioSource.h"
#include <JuceHeader.h>

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::KeyListener {
public:
  MainComponent();
  ~MainComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void timerCallback() override;

  // KeyListener overrides
  bool keyPressed(const juce::KeyPress &key) override {
    return keyPressed(key, this);
  }
  bool keyPressed(const juce::KeyPress &key,
                  Component *originatingComponent) override;

  bool keyStateChanged(bool isKeyDown) override {
    return keyStateChanged(isKeyDown, this);
  }
  bool keyStateChanged(bool isKeyDown,
                       Component *originatingComponent) override {
    return false;
  }

  // (Other member functions for MIDI handling, file loading, etc. remain as
  // before)
  void loadMidiFile();
  void playMidiFile();
  void stopMidiFile();
  void setupLoopRegion();
  void clearLoopRegion();

private:
  // File chooser
  std::unique_ptr<juce::FileChooser> fileChooser;

  // MIDI handling and playback state
  juce::MidiFile midiFile;
  juce::MidiMessageSequence midiSequence;
  bool isPlaying = false;
  int currentEvent = 0;
  std::atomic<double> playbackPosition{0.0};
  double lastTime = 0.0;
  int currentLoopIteration = 0;
  double tempo = 120.0; // BPM
  double loopStartBeat = 0.0, loopEndBeat = 0.0;
  int loopCount = 0;
  bool isLooping = false;

  // Audio setup
  juce::AudioDeviceManager audioDeviceManager;
  juce::AudioSourcePlayer audioSourcePlayer;
  std::unique_ptr<juce::MixerAudioSource> audioMixerSource;
  std::unique_ptr<SynthAudioSource> synthAudioSource;
  std::unique_ptr<MidiSchedulerAudioSource> midiSchedulerAudioSource;
  juce::AudioFormatManager formatManager;

  // GUI components
  juce::TextButton loadButton;
  juce::TextButton playButton;
  juce::TextButton stopButton;
  juce::TextButton setLoopButton;
  juce::TextButton clearLoopButton;
  juce::ComboBox presetBox; // For preset selection
  juce::ComboBox transpositionBox; // For note transposition
  juce::Label transpositionLabel;
  PianoRollComponent pianoRoll;
  juce::Slider tempoSlider;
  juce::Label tempoLabel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
