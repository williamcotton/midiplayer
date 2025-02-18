#include "SynthAudioSource.h"
#include "../JuceLibraryCode/BinaryData.h" // For BinaryData::Korg_Triton_Piano_sf2 and its size

SynthAudioSource::SynthAudioSource() {
  // Load the shared SF2 sound from binary data
  auto tempFile = juce::File::createTempFile(".sf2");
  tempFile.replaceWithData(BinaryData::Korg_Triton_Piano_sf2,
                          BinaryData::Korg_Triton_Piano_sf2Size);

  // Initialize the shared sound
  sharedSF2Sound = std::make_unique<sfzero::SF2Sound>(tempFile);
  sharedSF2Sound->loadRegions();
  sharedSF2Sound->loadSamples(nullptr);

  // Initialize synths for all channels
  for (int channel = 0; channel < 16; ++channel) {
    channelSynths[channel] = std::make_unique<sfzero::Synth>();
    
    // Add voices to each synth
    for (int i = 0; i < 8; ++i) {
      channelSynths[channel]->addVoice(new sfzero::Voice());
    }
    
    // Add the shared sound to each synth
    channelSynths[channel]->clearSounds();
    channelSynths[channel]->addSound(sharedSF2Sound.get());
  }
  
  tempFile.deleteFile();
}

SynthAudioSource::~SynthAudioSource() {}

void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
  currentSampleRate = sampleRate;
  
  // Pre-allocate mixing buffer
  mixingBuffer.setSize(2, samplesPerBlockExpected, false, false, true);
  
  // Clear all MIDI buffers
  for (auto& buffer : channelMidiBuffers) {
    buffer.clear();
  }
  
  // Prepare all synths
  for (auto& synth : channelSynths) {
    synth->setCurrentPlaybackSampleRate(sampleRate);
  }
  
  numChannels = 2;  // Store channel count for later use
}

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
  // Safety checks
  if (mixingBuffer.getNumSamples() < numSamples || 
      mixingBuffer.getNumChannels() < numChannels) {
    outputBuffer.clear(startSample, numSamples);
    return;
  }

  // Clear all MIDI buffers (reuse existing buffers)
  for (auto& buffer : channelMidiBuffers) {
    buffer.clear();
  }

  // Sort MIDI events by channel
  for (const auto metadata : midiBuffer) {
    auto msg = metadata.getMessage();
    int channel = msg.getChannel() - 1; // MIDI channels are 1-based
    if (channel >= 0 && channel < 16) {
      channelMidiBuffers[channel].addEvent(msg, metadata.samplePosition);
      activeChannels.set(channel);
    }
  }

  // Clear the output buffer
  outputBuffer.clear(startSample, numSamples);
  
  // Process each active channel
  for (int channel = 0; channel < 16; ++channel) {
    if (activeChannels[channel]) {
      // Clear mixing buffer (avoiding reallocation)
      mixingBuffer.clear();
      
      // Render this channel's synth
      channelSynths[channel]->renderNextBlock(mixingBuffer, 
                                            channelMidiBuffers[channel], 
                                            0, numSamples);
      
      // Mix into the main output buffer
      for (int i = 0; i < numChannels; ++i) {
        outputBuffer.addFrom(i, startSample, 
                           mixingBuffer, i, 0, 
                           numSamples);
      }
    }
  }
}

void SynthAudioSource::releaseResources() {
  // Free the mixing buffer when not needed
  mixingBuffer.setSize(0, 0);
  
  // Clear all MIDI buffers
  for (auto& buffer : channelMidiBuffers) {
    buffer.clear();
  }
}

void SynthAudioSource::setChannelPreset(int channel, int presetIndex) {
  if (channel >= 0 && channel < 16) {
    // Stop any playing notes on this channel
    if (auto* synth = channelSynths[channel].get()) {
      synth->allNotesOff(0, true);
    }
    
    // Update preset
    if (sharedSF2Sound) {
      sharedSF2Sound->useSubsound(presetIndex);
    }
  }
}

void SynthAudioSource::setMidiSequence(const juce::MidiMessageSequence& sequence) {
  midiSequence = sequence;
  playbackPosition.store(0.0);
}

void SynthAudioSource::startPlayback() {
  isPlaying = true;
  playbackPosition.store(0.0);
}

void SynthAudioSource::stopPlayback() { 
  isPlaying = false; 
}

void SynthAudioSource::setTempo(double newTempo) { 
  tempo = newTempo; 
}

void SynthAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
  // Always clear the output
  bufferToFill.clearActiveBufferRegion();

  if (!isPlaying)
    return;

  const int numSamples = bufferToFill.numSamples;
  const double secondsPerBeat = 60.0 / tempo;
  const double beatsPerBlock = (numSamples / currentSampleRate) / secondsPerBeat;
  const double currentBeat = playbackPosition.load();

  // Find MIDI events between currentBeat and currentBeat + beatsPerBlock
  int startEventIndex = findEventIndexForBeat(currentBeat);
  int endEventIndex = findEventIndexForBeat(currentBeat + beatsPerBlock);

  // Clear all MIDI buffers (reuse existing buffers)
  for (auto& buffer : channelMidiBuffers) {
    buffer.clear();
  }
  activeChannels.reset();

  // Sort MIDI events by channel
  for (int i = startEventIndex; i < endEventIndex && i < midiSequence.getNumEvents(); ++i) {
    auto* event = midiSequence.getEventPointer(i);
    double eventBeat = event->message.getTimeStamp() / 480.0; // convert ticks to beats
    double relativeBeat = eventBeat - currentBeat;
    double eventTimeSec = relativeBeat * secondsPerBeat;
    int sampleOffset = static_cast<int>(eventTimeSec * currentSampleRate);
    
    if (sampleOffset >= 0 && sampleOffset < numSamples) {
      int channel = event->message.getChannel() - 1;
      if (channel >= 0 && channel < 16) {
        channelMidiBuffers[channel].addEvent(event->message, bufferToFill.startSample + sampleOffset);
        activeChannels.set(channel);
      }
    }
  }

  // Process each active channel
  for (int channel = 0; channel < 16; ++channel) {
    if (activeChannels[channel]) {
      // Clear mixing buffer (avoiding reallocation)
      mixingBuffer.clear();
      
      // Render this channel's synth
      channelSynths[channel]->renderNextBlock(mixingBuffer, 
                                            channelMidiBuffers[channel], 
                                            0, numSamples);
      
      // Mix into the main output buffer
      for (int i = 0; i < numChannels; ++i) {
        bufferToFill.buffer->addFrom(i, bufferToFill.startSample, 
                                    mixingBuffer, i, 0, 
                                    numSamples);
      }
    }
  }

  // Advance our playback position
  playbackPosition.store(currentBeat + beatsPerBlock);
}