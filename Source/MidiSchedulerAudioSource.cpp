#include "MidiSchedulerAudioSource.h"

MidiSchedulerAudioSource::MidiSchedulerAudioSource(
    SynthAudioSource *synthSource)
    : synth(synthSource) {}

MidiSchedulerAudioSource::~MidiSchedulerAudioSource() {}

void MidiSchedulerAudioSource::prepareToPlay(int samplesPerBlockExpected,
                                             double sampleRate) {
  currentSampleRate = sampleRate;
  if (synth != nullptr)
    synth->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MidiSchedulerAudioSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  bufferToFill.clearActiveBufferRegion();
  if (!isPlaying || synth == nullptr)
    return;

  const int numSamples = bufferToFill.numSamples;
  const double currentTempo = tempo.load();
  const double secondsPerBeat = 60.0 / currentTempo;
  const double beatsPerBlock =
      (numSamples / currentSampleRate) / secondsPerBeat;
  double currentBeat = playbackPosition.load();

  // --- Looping Check ---
  if (isLooping && currentBeat > loopEndBeat) {
    if (currentLoopIteration < loopCount - 1) {
      ++currentLoopIteration;
      currentBeat = loopStartBeat;
      playbackPosition.store(currentBeat);
    } else {
      // End looping: exit loop mode (or you could choose to stop playback).
      isLooping = false;
      currentLoopIteration = 0;
    }
  }

  // --- End-of-File Check ---
  double fileEndBeat = 0.0;
  if (midiSequence.getNumEvents() > 0)
    fileEndBeat =
        ticksToBeats(
            midiSequence.getEventPointer(midiSequence.getNumEvents() - 1)
                ->message.getTimeStamp()) +
        1.0;

  if (!isLooping && (currentBeat + beatsPerBlock) >= fileEndBeat) {
    double beatsToEnd = fileEndBeat - currentBeat;
    int samplesToEnd =
        juce::jmin(numSamples, static_cast<int>(beatsToEnd * currentSampleRate /
                                                (60.0 / currentTempo)));
    juce::MidiBuffer midiBuffer;
    int startEventIndex = findEventIndexForBeat(currentBeat);
    int endEventIndex = findEventIndexForBeat(fileEndBeat);
    for (int i = startEventIndex;
         i < endEventIndex && i < midiSequence.getNumEvents(); ++i) {
      auto *event = midiSequence.getEventPointer(i);
      double eventBeat = ticksToBeats(event->message.getTimeStamp());
      double relativeBeat = eventBeat - currentBeat;
      double eventTimeSec = relativeBeat * secondsPerBeat;
      int sampleOffset = static_cast<int>(eventTimeSec * currentSampleRate);
      if (sampleOffset >= 0 && sampleOffset < samplesToEnd)
        midiBuffer.addEvent(event->message,
                            bufferToFill.startSample + sampleOffset);
    }
    synth->renderNextBlock(*bufferToFill.buffer, midiBuffer,
                           bufferToFill.startSample, samplesToEnd);
    playbackPosition.store(fileEndBeat);

    // Call the onPlaybackStopped callback on the message thread.
    if (onPlaybackStopped) {
      juce::MessageManager::callAsync([this] { onPlaybackStopped(); });
    }
    return;
  }

  // --- Normal Playback ---
  juce::MidiBuffer midiBuffer;
  int startEventIndex = findEventIndexForBeat(currentBeat);
  int endEventIndex = findEventIndexForBeat(currentBeat + beatsPerBlock);
  for (int i = startEventIndex;
       i < endEventIndex && i < midiSequence.getNumEvents(); ++i) {
    auto *event = midiSequence.getEventPointer(i);
    double eventBeat = ticksToBeats(event->message.getTimeStamp());
    double relativeBeat = eventBeat - currentBeat;
    double eventTimeSec = relativeBeat * secondsPerBeat;
    int sampleOffset = static_cast<int>(eventTimeSec * currentSampleRate);
    if (sampleOffset >= 0 && sampleOffset < numSamples)
      midiBuffer.addEvent(event->message,
                          bufferToFill.startSample + sampleOffset);
  }
  synth->renderNextBlock(*bufferToFill.buffer, midiBuffer,
                         bufferToFill.startSample, numSamples);
  playbackPosition.store(currentBeat + beatsPerBlock);
}

void MidiSchedulerAudioSource::releaseResources() {
  if (synth != nullptr)
    synth->releaseResources();
}

void MidiSchedulerAudioSource::setMidiSequence (const juce::MidiMessageSequence& sequence)
{
    midiSequence = sequence;
    // Reset only the playback position, not the tempo:
    playbackPosition.store(0.0);
    // Do NOT reset tempo here.
}

void MidiSchedulerAudioSource::startPlayback() {
  isPlaying = true;
  isLooping = true;
}

void MidiSchedulerAudioSource::stopPlayback() { isPlaying = false; }

void MidiSchedulerAudioSource::setTempo(double newTempo) {
  tempo.store(newTempo);
}

void MidiSchedulerAudioSource::setLoopRegion(double startBeat, double endBeat,
                                             int loops) {
  loopStartBeat = startBeat;
  loopEndBeat = endBeat;
  loopCount = loops;
  currentLoopIteration = 0;
  isLooping = (loops > 0 && endBeat > startBeat);
  playbackPosition.store(loopStartBeat);
}
