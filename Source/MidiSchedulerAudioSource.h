#pragma once

#include "SynthAudioSource.h"
#include <JuceHeader.h>

// This class centralizes MIDI scheduling, global playback position, and
// looping. It does no audio rendering on its own but uses its contained
// SynthAudioSource to render audio for the scheduled MIDI events.
class MidiSchedulerAudioSource : public juce::AudioSource {
public:
  // The constructor takes a pointer to a SynthAudioSource.
  // The scheduler does not take ownership of the synth.
  MidiSchedulerAudioSource(SynthAudioSource *synthSource);
  ~MidiSchedulerAudioSource() override;

  // AudioSource methods.
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  // Sets the MIDI sequence to play.
  void setMidiSequence(const juce::MidiMessageSequence &sequence);

  // Playback controls.
  void startPlayback();
  void stopPlayback();
  void setTempo(double newTempo);
  void setPPQ(int ppqValue) { ppq = ppqValue; }

  // Time signature getters
  int getNumerator() const { return timeSignatureNumerator; }
  int getDenominator() const { return timeSignatureDenominator; }
  
  // Callback for tempo changes
  std::function<void(double)> onTempoChanged;
  
  // Loop settings (in beats). For a valid loop, endBeat must be greater than
  // startBeat. loops: the number of times to loop.
  void setLoopRegion(double startBeat, double endBeat, int loops);
  double getPlaybackPosition() const { return playbackPosition.load(); }

  std::function<void()> onPlaybackStopped;

  // Method to set the transposition value
  void setTransposition(int semitones);

private:
  // The synth that actually renders audio.
  SynthAudioSource *synth = nullptr; // not owned

  // The MIDI sequence for playback.
  juce::MidiMessageSequence midiSequence;

  struct TempoEvent {
    double timestamp;  // in ticks
    double tempo;      // in microseconds per quarter note
    
    bool operator<(const TempoEvent& other) const {
      return timestamp < other.timestamp;
    }
  };
  
  std::vector<TempoEvent> tempoEvents;
  double getCurrentTempo(double timestamp) const;
  void extractTempoEvents();
  void extractTimeSignature();

  // Global playback state.
  std::atomic<double> playbackPosition{0.0}; // in beats
  std::atomic<double> tempo{120.0};          // BPM
  double currentSampleRate = 44100.0;
  bool isPlaying = false;
  int ppq = 480;  // Pulses Per Quarter note, default 480
  
  // Time signature state
  int timeSignatureNumerator = 4;
  int timeSignatureDenominator = 4;
  int clocksPerClick = 24;
  int thirtySecondPer24Clocks = 8;

  // Looping variables.
  bool isLooping = false;
  double loopStartBeat = 0.0;
  double loopEndBeat = 0.0;
  int loopCount = 0;
  int currentLoopIteration = 0;

  // Helper: convert ticks (assuming 480 PPQ) to beats.
  inline double ticksToBeats(double ticks) const { return ticks / ppq; }

  // Helper: find the first MIDI event at or after a given beat.
  int findEventIndexForBeat(double beat) {
    int low = 0;
    int high = midiSequence.getNumEvents();
    while (low < high) {
      int mid = (low + high) / 2;
      double eventBeat = ticksToBeats(
          midiSequence.getEventPointer(mid)->message.getTimeStamp());
      if (eventBeat < beat)
        low = mid + 1;
      else
        high = mid;
    }
    return low;
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSchedulerAudioSource)
};
