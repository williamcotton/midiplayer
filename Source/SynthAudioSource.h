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

  // Get the shared SF2 sound
  sfzero::SF2Sound *getSF2Sound() const { return sf2Sound.get(); }

  // Helper to set up a channel with a specific subsound
  void setupChannel(int channel, int subsoundIndex);

  // Stop all notes on all channels
  void stopAllNotes();

  // Set the transposition amount in semitones
  void setTransposition(int semitones) { transpositionAmount = semitones; }

private:
  // Our SF2 synthesizer and sound.
  struct ChannelInfo {
    std::unique_ptr<sfzero::Synth> synth;
    int subsoundIndex = 0;
  };

  // Single shared SF2 sound instance
  std::unique_ptr<sfzero::SF2Sound> sf2Sound;

  // Separate synth instances per channel
  std::array<ChannelInfo, 16> channelInfos;

  // Track which channels are active
  std::bitset<16> activeChannels;

  // Our MIDI playback data.
  juce::MidiMessageSequence midiSequence;
  std::atomic<double> playbackPosition{0.0};
  double tempo = 120.0; // BPM
  double currentSampleRate = 44100.0;
  bool isPlaying = false;

  // Temporary buffer for mixing
  std::unique_ptr<juce::AudioBuffer<float>> tempBuffer;

  // Transposition amount in semitones
  std::atomic<int> transpositionAmount{0};

  // Helper: Given a beat value, find the first event in our MIDI sequence that
  // occurs at or after that beat.
  int findEventIndexForBeat(double beat);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioSource)
};
