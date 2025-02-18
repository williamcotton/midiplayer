#pragma once

#include "../Modules/SFZero/SFZero.h" // Adjust include path as needed
#include <JuceHeader.h>


class SynthAudioSource : public juce::AudioSource {
public:
  SynthAudioSource();
  ~SynthAudioSource() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  // Methods to control MIDI playback
  void setMidiSequence(const juce::MidiMessageSequence &sequence);
  void startPlayback();
  void stopPlayback();
  void setTempo(double newTempo);

  // Forward MIDI rendering to the underlying SF2 synth
  void renderNextBlock(juce::AudioBuffer<float> &outputBuffer,
                      const juce::MidiBuffer &midiBuffer, int startSample,
                      int numSamples);

  // Get the shared SF2 sound instance
  sfzero::SF2Sound *getSF2Sound() const { return sharedSF2Sound.get(); }
  
  // Get a specific channel's synth
  sfzero::Synth *getSF2Synth(int channel = 0) { 
    if (channel >= 0 && channel < 16)
      return channelSynths[channel].get(); 
    return nullptr;
  }

  // Set preset for a specific channel
  void setChannelPreset(int channel, int presetIndex);

private:
  // Single shared SF2 sound instance
  std::unique_ptr<sfzero::SF2Sound> sharedSF2Sound;
  
  // Separate synth instances per channel
  std::array<std::unique_ptr<sfzero::Synth>, 16> channelSynths;
  
  // Track preset per channel
  std::array<int, 16> channelPresets{};
  
  // Pre-allocated mixing resources
  juce::AudioBuffer<float> mixingBuffer;
  std::array<juce::MidiBuffer, 16> channelMidiBuffers;
  
  // Track active channels
  std::bitset<16> activeChannels;
  
  // Cache channel count
  int numChannels = 2;

  // MIDI playback data
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
