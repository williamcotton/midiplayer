#include "SynthAudioSource.h"
#include "../JuceLibraryCode/BinaryData.h" // For BinaryData::Korg_Triton_Piano_sf2 and its size

SynthAudioSource::SynthAudioSource() {
  // Load the SF2 sound from binary data.
  auto tempFile = juce::File::createTempFile(".sf2");
  tempFile.replaceWithData(BinaryData::gm_sf2,
                           BinaryData::gm_sf2Size);
  sf2Sound = std::make_unique<sfzero::SF2Sound>(tempFile);
  sf2Sound->loadRegions();
  sf2Sound->loadSamples(nullptr);
  tempFile.deleteFile();

  // Print out all available subsounds
  DBG("Available Subsounds:");
  for (int i = 0; i < sf2Sound->numSubsounds(); ++i) {
    DBG(juce::String(i) + ": " + sf2Sound->subsoundName(i));
  }

  // Initialize synths for all channels
  for (int channel = 0; channel < 16; ++channel) {
    channelInfos[channel].synth = std::make_unique<sfzero::Synth>();
    
    // Add voices to each synth
    for (int i = 0; i < 8; ++i) {
      channelInfos[channel].synth->addVoice(new sfzero::Voice());
    }
    
    // Add the shared sound to each synth
    channelInfos[channel].synth->clearSounds();
    channelInfos[channel].synth->addSound(sf2Sound.get());
  }

  // Set up our specific channel mappings
  setupChannel(0, 228);   // Channel 0: Power Kit for drums
  setupChannel(1, 33);     // Channel 1: Acoustic Grand Piano
  setupChannel(2, 0);   // Drums (Taiko Drum - or try 118 for Synth Drum)
  setupChannel(3, 0);     // Additional Piano if needed
  setupChannel(4, 0);     // Additional Piano if needed
}

void SynthAudioSource::setupChannel(int channel, int subsoundIndex) {
  if (channel >= 0 && channel < 16) {
    // Stop any playing notes on this channel
    if (auto* synth = channelInfos[channel].synth.get()) {
      synth->allNotesOff(0, true);
    }
    
    // Store the subsound index for this channel
    channelInfos[channel].subsoundIndex = subsoundIndex;
  }
}

void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
  currentSampleRate = sampleRate;
  
  // Initialize the temporary buffer
  tempBuffer = std::make_unique<juce::AudioBuffer<float>>(2, samplesPerBlockExpected);
  
  // Prepare all synths
  for (auto& info : channelInfos) {
    if (info.synth) {
      info.synth->setCurrentPlaybackSampleRate(sampleRate);
    }
  }
}

void SynthAudioSource::releaseResources() {
  tempBuffer = nullptr;
}

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
  // Clear the output buffer
  outputBuffer.clear(startSample, numSamples);
  
  // Create separate MIDI buffers for each channel
  std::array<juce::MidiBuffer, 16> channelBuffers;
  
  // Sort MIDI events by channel
  for (const auto metadata : midiBuffer) {
    auto msg = metadata.getMessage();
    int channel = msg.getChannel() - 1; // MIDI channels are 1-based
    if (channel >= 0 && channel < 16) {
      // Add debug logging for note-on messages
      if (msg.isNoteOn()) {
        DBG("Channel " + juce::String(channel) + " Note: " + juce::String(msg.getNoteNumber()) + 
            " Velocity: " + juce::String(msg.getVelocity()));
      }
      channelBuffers[channel].addEvent(msg, metadata.samplePosition);
      activeChannels.set(channel);
    }
  }
  
  // Process each active channel
  for (int channel = 0; channel < 16; ++channel) {
    if (activeChannels[channel]) {
      // Clear the temp buffer
      tempBuffer->clear();
      
      // Set the correct subsound for this channel before rendering
      if (sf2Sound) {
        sf2Sound->useSubsound(channelInfos[channel].subsoundIndex);
      }
      
      // Render this channel's synth
      if (auto* synth = channelInfos[channel].synth.get()) {
        synth->renderNextBlock(*tempBuffer, channelBuffers[channel], 0, numSamples);
      }
      
      // Mix into the main output buffer
      for (int i = 0; i < outputBuffer.getNumChannels(); ++i) {
        outputBuffer.addFrom(i, startSample, *tempBuffer, i, 0, numSamples);
      }
    }
  }
}

void SynthAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
  // Always clear the output.
  bufferToFill.clearActiveBufferRegion();

  if (!isPlaying)
    return;

  const int numSamples = bufferToFill.numSamples;
  const double secondsPerBeat = 60.0 / tempo;
  const double beatsPerBlock =
      (numSamples / currentSampleRate) / secondsPerBeat;
  const double currentBeat = playbackPosition.load();

  juce::MidiBuffer midiBuffer;

  // Find MIDI events between currentBeat and currentBeat + beatsPerBlock.
  int startEventIndex = findEventIndexForBeat(currentBeat);
  int endEventIndex = findEventIndexForBeat(currentBeat + beatsPerBlock);

  for (int i = startEventIndex;
       i < endEventIndex && i < midiSequence.getNumEvents(); ++i) {
    auto* event = midiSequence.getEventPointer(i);
    double eventBeat =
        event->message.getTimeStamp() / 480.0; // convert ticks to beats
    double relativeBeat = eventBeat - currentBeat;
    double eventTimeSec = relativeBeat * secondsPerBeat;
    int sampleOffset = static_cast<int>(eventTimeSec * currentSampleRate);
    if (sampleOffset >= 0 && sampleOffset < numSamples)
      midiBuffer.addEvent(event->message,
                          bufferToFill.startSample + sampleOffset);
  }

  // Render the audio block using our multi-channel rendering method
  renderNextBlock(*bufferToFill.buffer, midiBuffer,
                 bufferToFill.startSample, numSamples);

  // Advance our playback position.
  playbackPosition.store(currentBeat + beatsPerBlock);
}

void SynthAudioSource::setMidiSequence(
    const juce::MidiMessageSequence &sequence) {
  midiSequence = sequence;
  playbackPosition.store(0.0);
}

void SynthAudioSource::startPlayback() {
  isPlaying = true;
  playbackPosition.store(0.0);
}

void SynthAudioSource::stopPlayback() { isPlaying = false; }

void SynthAudioSource::setTempo(double newTempo) { tempo = newTempo; }

int SynthAudioSource::findEventIndexForBeat(double beat) {
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

SynthAudioSource::~SynthAudioSource() {
  // Clean up any resources
  tempBuffer = nullptr;
  
  // Clear all synths first
  for (auto& info : channelInfos) {
    if (info.synth) {
      info.synth->clearSounds();
    }
  }
  
  // Then clear the shared sound
  sf2Sound = nullptr;
}