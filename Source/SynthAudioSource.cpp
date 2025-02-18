#include "SynthAudioSource.h"
#include "../JuceLibraryCode/BinaryData.h" // For BinaryData::Korg_Triton_Piano_sf2 and its size

SynthAudioSource::SynthAudioSource() {
  // Add several voices for polyphony.
  for (int i = 0; i < 8; ++i)
    sf2Synth.addVoice(new sfzero::Voice());

  // Load the SF2 sound from binary data.
  auto tempFile = juce::File::createTempFile(".sf2");
  tempFile.replaceWithData(BinaryData::gm_sf2,
                           BinaryData::gm_sf2Size);
  sf2Sound.reset(new sfzero::SF2Sound(tempFile));
  sf2Sound->loadRegions();
  sf2Sound->loadSamples(
      nullptr); // In a full app you might pass a format manager
  tempFile.deleteFile();

  // Clear any existing sounds and add our SF2 sound.
  sf2Synth.clearSounds();
  sf2Synth.addSound(sf2Sound.get());
}

SynthAudioSource::~SynthAudioSource() {}

void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected,
                                     double sampleRate) {
  currentSampleRate = sampleRate;
  sf2Synth.setCurrentPlaybackSampleRate(sampleRate);
}

void SynthAudioSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  // Always clear the output.
  bufferToFill.clearActiveBufferRegion();

  if (!isPlaying)
    return;

  const int numSamples = bufferToFill.numSamples;
  const double secondsPerBeat = 60.0 / tempo;
  const double beatsPerBlock =
      (numSamples / currentSampleRate) / secondsPerBeat;
  const double currentBeat = playbackPosition.load();

  juce::MidiBuffer midiBuffer; // possibly a real-time issue here due to allocation in the audio thread but probably 
                               // not a problem due to the small size of the midi buffer

  // Find MIDI events between currentBeat and currentBeat + beatsPerBlock.
  int startEventIndex = findEventIndexForBeat(currentBeat);
  int endEventIndex = findEventIndexForBeat(currentBeat + beatsPerBlock);

  for (int i = startEventIndex;
       i < endEventIndex && i < midiSequence.getNumEvents(); ++i) {
    auto *event = midiSequence.getEventPointer(i);
    double eventBeat =
        event->message.getTimeStamp() / 480.0; // convert ticks to beats
    double relativeBeat = eventBeat - currentBeat;
    double eventTimeSec = relativeBeat * secondsPerBeat;
    int sampleOffset = static_cast<int>(eventTimeSec * currentSampleRate);
    if (sampleOffset >= 0 && sampleOffset < numSamples)
      midiBuffer.addEvent(event->message,
                          bufferToFill.startSample + sampleOffset);
  }

  // Render the audio block using the SF2 synthesizer.
  sf2Synth.renderNextBlock(*bufferToFill.buffer, midiBuffer,
                           bufferToFill.startSample, numSamples);

  // Advance our playback position.
  playbackPosition.store(currentBeat + beatsPerBlock);
}

void SynthAudioSource::releaseResources() {
  // Nothing needed here for this example.
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

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float> &outputBuffer,
                                       const juce::MidiBuffer &midiBuffer,
                                       int startSample, int numSamples) {
  sf2Synth.renderNextBlock(outputBuffer, midiBuffer, startSample, numSamples);
}