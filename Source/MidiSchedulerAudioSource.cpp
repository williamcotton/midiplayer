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
  const double currentTick = playbackPosition.load() * ppq;
  const double currentTempo = getCurrentTempo(currentTick);
  tempo.store(currentTempo);  // Update the stored tempo for UI purposes
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
    
    // Extract tempo and time signature information
    extractTempoEvents();
    extractTimeSignature();
}

void MidiSchedulerAudioSource::extractTempoEvents()
{
    tempoEvents.clear();
    bool foundTempoEvent = false;
    double initialTempo = 120.0;  // Default tempo
    
    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
    {
        auto* event = midiSequence.getEventPointer(i);
        auto& msg = event->message;
        
        if (msg.isMetaEvent())
        {
            const uint8_t* data = msg.getRawData();
            if (data[1] == 0x51 && data[2] == 0x03)  // Tempo meta event
            {
                // Extract tempo in microseconds per quarter note
                uint32_t tempoValue = (data[3] << 16) | (data[4] << 8) | data[5];
                double bpm = 60000000.0 / tempoValue;
                DBG("Found tempo event at tick " + juce::String(msg.getTimeStamp()) + 
                    ": " + juce::String(bpm) + " BPM");
                tempoEvents.push_back({msg.getTimeStamp(), static_cast<double>(tempoValue)});
                
                // If this is the first tempo event, store it as our initial tempo
                if (!foundTempoEvent) {
                    initialTempo = bpm;
                    foundTempoEvent = true;
                }
            }
        }
    }
    
    // Sort tempo events by timestamp
    std::sort(tempoEvents.begin(), tempoEvents.end());
    
    // If no tempo events were found, use default tempo
    if (!foundTempoEvent) {
        DBG("No tempo events found, using default 120 BPM");
        tempoEvents.push_back({0.0, 500000.0});  // 120 BPM
        initialTempo = 120.0;
    }
    
    // Always notify about the initial tempo, whether it's from the file or default
    if (onTempoChanged) {
        DBG("Setting initial tempo to: " + juce::String(initialTempo) + " BPM");
        juce::MessageManager::callAsync([this, initialTempo]() {
            if (onTempoChanged) onTempoChanged(initialTempo);
        });
    }
    
    // Store the initial tempo
    tempo.store(initialTempo);
}

void MidiSchedulerAudioSource::extractTimeSignature()
{
    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
    {
        auto* event = midiSequence.getEventPointer(i);
        auto& msg = event->message;
        
        if (msg.isMetaEvent())
        {
            const uint8_t* data = msg.getRawData();
            if (data[1] == 0x58 && data[2] == 0x04)  // Time signature meta event
            {
                timeSignatureNumerator = data[3];
                timeSignatureDenominator = 1 << data[4];  // 2^data[4]
                clocksPerClick = data[5];
                thirtySecondPer24Clocks = data[6];
                
                DBG("Found time signature: " + juce::String(timeSignatureNumerator) + "/" + 
                    juce::String(timeSignatureDenominator) + 
                    " (clocks per click: " + juce::String(clocksPerClick) + 
                    ", 32nd notes per 24 MIDI clocks: " + juce::String(thirtySecondPer24Clocks) + ")");
                
                // Usually we only care about the first time signature, unless implementing
                // time signature changes
                break;
            }
        }
    }
}

double MidiSchedulerAudioSource::getCurrentTempo(double timestamp) const
{
    // Find the last tempo event before or at this timestamp
    auto it = std::upper_bound(tempoEvents.begin(), tempoEvents.end(), 
                              TempoEvent{timestamp, 0.0});
    
    if (it == tempoEvents.begin())
        return 120.0;  // Default tempo if timestamp is before first tempo event
        
    --it;  // Move to the last event before timestamp
    return 60000000.0 / it->tempo;  // Convert microseconds per quarter note to BPM
}

void MidiSchedulerAudioSource::startPlayback() {
  isPlaying = true;
  isLooping = true;
}

void MidiSchedulerAudioSource::stopPlayback() { isPlaying = false; }

void MidiSchedulerAudioSource::setTempo(double newTempo) {
  tempo.store(newTempo);
  // Clear any MIDI tempo events and set a single tempo event at time 0
  tempoEvents.clear();
  tempoEvents.push_back({0.0, 60000000.0 / newTempo});  // Convert BPM to microseconds per quarter note
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

void MidiSchedulerAudioSource::setTransposition(int semitones) {
  if (synth != nullptr) {
    synth->setTransposition(semitones);
  }
}
