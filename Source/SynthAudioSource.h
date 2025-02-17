#pragma once

#include "../Modules/SFZero/SFZero.h" // Adjust include path as needed
#include <JuceHeader.h>


class SynthAudioSource : public juce::AudioSource {
public:
  SynthAudioSource();
  ~SynthAudioSource() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  // Methods to control MIDI playback.
  void setMidiSequence(const juce::MidiMessageSequence &sequence);
  void startPlayback();
  void stopPlayback();
  void setTempo(double newTempo);

  // Forward MIDI rendering to the underlying SF2 synth.
  void renderNextBlock(juce::AudioBuffer<float> &outputBuffer,
                       const juce::MidiBuffer &midiBuffer, int startSample,
                       int numSamples);

  sfzero::SF2Sound *getSF2Sound() const { return sf2Sound.get(); }
  sfzero::Synth *getSF2Synth() { return &sf2Synth; }

private:
  // Our SF2 synthesizer and sound.
  sfzero::Synth sf2Synth;
  std::unique_ptr<sfzero::SF2Sound> sf2Sound;

  // Our MIDI playback data.
  juce::MidiMessageSequence midiSequence;
  std::atomic<double> playbackPosition{0.0};
  double tempo = 120.0; // BPM
  double currentSampleRate = 44100.0;
  bool isPlaying = false;

  // Helper: Given a beat value, find the first event in our MIDI sequence that
  // occurs at or after that beat.
  int findEventIndexForBeat(double beat) {
    int low = 0;
    int high = midiSequence.getNumEvents();
    while (low < high) {
      int mid = (low + high) / 2;
      // Convert the event's timestamp (in ticks) to beats (assuming 480 PPQ)
      double eventBeat =
          midiSequence.getEventPointer(mid)->message.getTimeStamp() / 480.0;
      if (eventBeat < beat)
        low = mid + 1;
      else
        high = mid;
    }
    return low;
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioSource)
};
