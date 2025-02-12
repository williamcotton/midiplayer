#include "MainComponent.h"

MainComponent::MainComponent()
{
    // Enable keyboard input
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    
    addAndMakeVisible(loadButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(pianoRoll);
    addAndMakeVisible(setLoopButton);
    addAndMakeVisible(clearLoopButton);
    
    loadButton.setButtonText("Load MIDI File");
    playButton.setButtonText("Play");
    stopButton.setButtonText("Stop");
    setLoopButton.setButtonText("Set Loop");
    clearLoopButton.setButtonText("Clear Loop");
    
    playButton.setEnabled(false);
    stopButton.setEnabled(false);
    
    loadButton.onClick = [this]() { loadMidiFile(); };
    playButton.onClick = [this]() { playMidiFile(); };
    stopButton.onClick = [this]() { stopMidiFile(); };
    setLoopButton.onClick = [this]() { setupLoopRegion(); };
    clearLoopButton.onClick = [this]() { clearLoopRegion(); };
    
    // Setup tempo control
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    
    tempoLabel.setText("Tempo", juce::dontSendNotification);
    tempoSlider.setRange(30.0, 300.0, 1.0);
    tempoSlider.setValue(120.0, juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    tempoSlider.onValueChange = [this] { 
        tempo = tempoSlider.getValue();
        DBG("Tempo changed to: " + juce::String(tempo) + " BPM");
    };
    
    setSize(800, 600);
    startTimer(5);

    auto result = audioDeviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty())
    {
        DBG("Error initializing audio: " + result);
    }
    
    audioDeviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);

    for (int i = 0; i < 8; ++i)
    {
        synth.addVoice(new SineWaveVoice());
    }
    synth.addSound(new SineWaveSound());
}

MainComponent::~MainComponent()
{
    audioSourcePlayer.setSource(nullptr);
    audioDeviceManager.removeAudioCallback(&audioSourcePlayer);
    stopTimer();
    stopMidiFile();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto buttonHeight = 40;
    auto paddingX = 5;
    auto paddingY = 5;

#if JUCE_IOS
    // Account for iOS safe area at the top
    if (auto *display =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
      auto safeInsets = display->safeAreaInsets;
      area.removeFromTop(safeInsets.getTop());
      area.removeFromBottom(safeInsets.getBottom());
    }
#endif

    auto topControls = area.removeFromTop(buttonHeight);
    loadButton.setBounds(topControls.removeFromLeft(120).reduced(paddingX, paddingY));
    playButton.setBounds(topControls.removeFromLeft(80).reduced(paddingX, paddingY));
    stopButton.setBounds(topControls.removeFromLeft(80).reduced(paddingX, paddingY));
    tempoLabel.setBounds(topControls.removeFromLeft(100).reduced(paddingX, paddingY));
    
    auto loopControls = area.removeFromTop(buttonHeight);
    setLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(paddingX, paddingY));
    clearLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(paddingX, paddingY));
    
    auto tempoControls = loopControls.removeFromLeft(250);
    tempoSlider.setBounds(tempoControls.reduced(paddingX, paddingY));
    
    pianoRoll.setBounds(area.reduced(paddingX, paddingY));
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

int MainComponent::findEventIndexForBeat(double beat) 
{
    int low = 0;
    int high = midiSequence.getNumEvents();
    while (low < high) {
        int mid = (low + high) / 2;
        double eventBeat = convertTicksToBeats(
            midiSequence.getEventPointer(mid)->message.getTimeStamp());
        if (eventBeat < beat)
        low = mid + 1;
        else
        high = mid;
    }
    return low;
}

void MainComponent::processSegment(const juce::AudioSourceChannelInfo& bufferInfo,
                                   int segmentStartSample,
                                   int segmentNumSamples,
                                   double segmentStartBeat,
                                   double segmentEndBeat)
{
    juce::MidiBuffer midiBuffer;
    
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    const double sampleRate = device->getCurrentSampleRate();
    const double currentTempo = tempo; // local copy
    const double secondsPerBeat = 60.0 / currentTempo;
    
    // Use binary search (or iterate over all events) to find the first event at or after segmentStartBeat.
    int eventIndex = findEventIndexForBeat(segmentStartBeat);
    
    // Process events until an event’s beat exceeds segmentEndBeat.
    while (eventIndex < midiSequence.getNumEvents())
    {
        double eventBeat = convertTicksToBeats(midiSequence.getEventPointer(eventIndex)->message.getTimeStamp());
        if (eventBeat > segmentEndBeat)
            break;
        
        // Compute the event's offset (in samples) relative to the segment.
        int offset = segmentStartSample + (int)((eventBeat - segmentStartBeat) * secondsPerBeat * sampleRate);
        // Use a strict ">" check so that an event exactly at the boundary is processed.
        if (offset > segmentStartSample + segmentNumSamples)
            break;
        
        midiBuffer.addEvent(midiSequence.getEventPointer(eventIndex)->message, offset - segmentStartSample);
        ++eventIndex;
    }
    
    juce::AudioSourceChannelInfo segmentBuffer;
    segmentBuffer.buffer = bufferInfo.buffer;
    segmentBuffer.startSample = bufferInfo.startSample + segmentStartSample;
    segmentBuffer.numSamples = segmentNumSamples;
    
    synth.renderNextBlock(*segmentBuffer.buffer, midiBuffer, segmentBuffer.startSample, segmentBuffer.numSamples);
}


void MainComponent::reTriggerSustainedNotesAt(double boundaryBeat)
{
    // Define a small tolerance (in beats) to catch events that are just after the boundary.
    const double epsilon = 0.05;  // Adjust as needed
    
    // Iterate over all MIDI events.
    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
    {
        auto* evt = midiSequence.getEventPointer(i);
        if (evt->message.isNoteOn() && evt->noteOffObject != nullptr)
        {
            double noteOnBeat = convertTicksToBeats(evt->message.getTimeStamp());
            double noteOffBeat = convertTicksToBeats(evt->noteOffObject->message.getTimeStamp());
            
            // Allow a small tolerance on the noteOnBeat check.
            if (noteOnBeat <= (boundaryBeat + epsilon) && noteOffBeat > boundaryBeat)
            {
                synth.noteOn(evt->message.getChannel(),
                             evt->message.getNoteNumber(),
                             evt->message.getVelocity() / 127.0f);
            }
        }
    }
}


void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    // If looping is enabled and the current playback position is beyond the loop's end,
    // reset it to the loop start. This avoids processing negative durations.
    if (isLooping && (playbackPosition.load() > loopEndBeat))
    {
        playbackPosition.store(loopStartBeat);
    }

    if (!isPlaying)
        return;
    
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    
    const double sampleRate = device->getCurrentSampleRate();
    const int numSamples = bufferToFill.numSamples;
    
    // Get a local copy of tempo and compute seconds per beat.
    const double currentTempo = tempo; // in BPM
    const double secondsPerBeat = 60.0 / currentTempo;
    
    // Calculate how many beats this block covers.
    const double blockBeats = (numSamples / sampleRate) / secondsPerBeat;
    
    // Read the current playback position (in beats).
    double currentBeat = playbackPosition.load();
    
    // Compute the “file end” as one beat after the last event’s beat.
    double lastEventBeat = 0.0;
    if (midiSequence.getNumEvents() > 0)
    {
        auto* lastEvent = midiSequence.getEventPointer(midiSequence.getNumEvents() - 1);
        lastEventBeat = convertTicksToBeats(lastEvent->message.getTimeStamp());
    }
    const double fileEndBeat = lastEventBeat + 1.0;
    
    // --- Non-looping case: if we are not in loop mode, check for end-of-file.
    if (!isLooping && (currentBeat + blockBeats) >= fileEndBeat)
    {
        double beatsToEnd = fileEndBeat - currentBeat;
        int samplesToEnd = juce::jmin(numSamples, (int)(beatsToEnd * secondsPerBeat * sampleRate));
        
        processSegment(bufferToFill, 0, samplesToEnd, currentBeat, fileEndBeat);
        playbackPosition.store(fileEndBeat);
        
        isPlaying = false;
        synth.allNotesOff(0, true);
        juce::MessageManager::callAsync([this]() {
            playButton.setEnabled(true);
            stopButton.setEnabled(false);
        });
        
        return;
    }
    
    // --- Looping case: if we are looping and this block crosses loopEndBeat.
    if (isLooping && (currentBeat + blockBeats) >= loopEndBeat)
    {
        // Process from currentBeat up to loopEndBeat.
        const double beatsToLoopEnd = loopEndBeat - currentBeat;
        const int samplesToLoopEnd = juce::jmin(numSamples, (int)(beatsToLoopEnd * secondsPerBeat * sampleRate));
        processSegment(bufferToFill, 0, samplesToLoopEnd, currentBeat, loopEndBeat);
        
        int remainingSamples = numSamples - samplesToLoopEnd;
        
        if (currentLoopIteration < loopCount - 1)
        {
            // For non-final iterations, clear active voices, re-trigger sustained notes at loopStartBeat,
            // then process the remainder of the block as if starting at loopStartBeat.
            synth.allNotesOff(0, true);
            reTriggerSustainedNotesAt(loopStartBeat);
            
            const double newSegmentStartBeat = loopStartBeat;
            const double newSegmentEndBeat = newSegmentStartBeat + (remainingSamples / sampleRate) / secondsPerBeat;
            processSegment(bufferToFill, samplesToLoopEnd, remainingSamples, newSegmentStartBeat, newSegmentEndBeat);
            playbackPosition.store(newSegmentEndBeat);
            
            currentLoopIteration++;            
        }
        else
        {
            // FINAL loop iteration:
            // Process the remainder of the block normally from loopEndBeat onward.
            const double extraBeats = (remainingSamples / sampleRate) / secondsPerBeat;
            double newBeatPosition = loopEndBeat + extraBeats;
            processSegment(bufferToFill, samplesToLoopEnd, remainingSamples, loopEndBeat, newBeatPosition);
            playbackPosition.store(newBeatPosition);
            
            // Turn off looping so that subsequent blocks process events normally.
            isLooping = false;
            
            // Reset the loop counter so that the loop region remains active for subsequent playbacks.
            currentLoopIteration = 0;
            
            // Clear lingering voices and re-trigger sustained notes based on the loop end.
            synth.allNotesOff(0, true);
            reTriggerSustainedNotesAt(loopEndBeat);
        }
        
        return;
    }
    // --- Default (non-crossing) case: process the entire block normally.
    processSegment(bufferToFill, 0, numSamples, currentBeat, currentBeat + blockBeats);
    playbackPosition.store(currentBeat + blockBeats);
}



void MainComponent::releaseResources()
{
}

bool MainComponent::isPositionInLoop(double beat) const
{
    return isLooping && beat >= loopStartBeat && beat < loopEndBeat;
}

void MainComponent::timerCallback() {
  // Simply update the piano roll's playhead using the current playback
  // position.
  pianoRoll.setPlaybackPosition(playbackPosition.load());
}

void MainComponent::loadMidiFile()
{
    DBG("Starting loadMidiFile()");
    auto fileChooserFlags =
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;
    
    DBG("Creating FileChooser...");
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select a MIDI file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");
        
    // Create a safe pointer for this MainComponent
    juce::Component::SafePointer<MainComponent> safeThis(this);
    DBG("Launching async file chooser...");
    
    fileChooser->launchAsync(fileChooserFlags,
        [safeThis](const juce::FileChooser& fc)
        {
            DBG("FileChooser callback started");
            
            // Always check if safeThis is still valid before proceeding
            if (safeThis == nullptr)
            {
                DBG("Component is no longer valid - aborting callback");
                return;
            }
            
            DBG("Component is valid");
            auto result = fc.getResult();
            DBG("Got file result: " + result.getFullPathName());
            
            std::unique_ptr<juce::InputStream> stream;
            
            #if JUCE_ANDROID
            DBG("Creating Android stream...");
            juce::URL::InputStreamOptions options(juce::URL::ParameterHandling::inAddress);
            stream = fc.getURLResult().createInputStream(options);
            #else
            if (result.exists()) {
                DBG("Creating file input stream...");
                stream = std::make_unique<juce::FileInputStream>(result);
                if (stream != nullptr) {
                    DBG("Stream created successfully, size: " + juce::String(stream->getTotalLength()));
                } else {
                    DBG("Failed to create stream!");
                }
            } else {
                DBG("File does not exist: " + result.getFullPathName());
            }
            #endif
            
            // Check component validity again before proceeding with stream processing
            if (safeThis == nullptr)
            {
                DBG("Component became invalid during stream creation - aborting");
                return;
            }
            
            if (stream != nullptr)
            {
                DBG("Stream opened successfully");
                
                // Store the current number of tracks before reading
                const int prevNumTracks = safeThis->midiFile.getNumTracks();
                DBG("Previous number of tracks: " + juce::String(prevNumTracks));
                
                if (safeThis->midiFile.readFrom(*stream))
                {
                    // Check component validity after potentially long file read
                    if (safeThis == nullptr)
                    {
                        DBG("Component became invalid during MIDI file read - aborting");
                        return;
                    }
                    
                    DBG("MIDI file read successfully");
                    const int newNumTracks = safeThis->midiFile.getNumTracks();
                    DBG("New number of tracks: " + juce::String(newNumTracks));
                    
                    if (newNumTracks > 0)
                    {
                        try {
                            DBG("Getting first track...");
                            safeThis->midiSequence = *safeThis->midiFile.getTrack(0);
                            DBG("First track copied, events: " + juce::String(safeThis->midiSequence.getNumEvents()));
                            
                            for (int i = 1; i < newNumTracks; ++i)
                            {
                                // Check validity in the loop as it might take time
                                if (safeThis == nullptr)
                                {
                                    DBG("Component became invalid during track processing - aborting");
                                    return;
                                }
                                
                                DBG("Adding track " + juce::String(i));
                                safeThis->midiSequence.addSequence(*safeThis->midiFile.getTrack(i),
                                                       0.0, 0.0,
                                                       safeThis->midiFile.getLastTimestamp());
                                DBG("Track " + juce::String(i) + " added, total events now: " + 
                                    juce::String(safeThis->midiSequence.getNumEvents()));
                            }
                            
                            // Final validity check before UI updates
                            if (safeThis == nullptr)
                            {
                                DBG("Component became invalid before UI updates - aborting");
                                return;
                            }
                            
                            DBG("Updating matched pairs...");
                            safeThis->midiSequence.updateMatchedPairs();
                            
                            DBG("Setting sequence in piano roll...");
                            safeThis->pianoRoll.setMidiSequence(safeThis->midiSequence);
                            
                            DBG("Updating UI state...");
                            safeThis->playButton.setEnabled(true);
                            safeThis->stopButton.setEnabled(false);
                            safeThis->currentEvent = 0;
                            safeThis->playbackPosition = 0.0;
                            safeThis->lastTime = juce::Time::getMillisecondCounterHiRes();
                            DBG("MIDI file loading completed successfully");
                        }
                        catch (const std::exception& e) {
                            DBG("Exception during MIDI processing: " + juce::String(e.what()));
                        }
                        catch (...) {
                            DBG("Unknown exception during MIDI processing");
                        }
                    }
                    else {
                        DBG("No tracks found in MIDI file");
                    }
                }
                else {
                    DBG("Failed to read MIDI file");
                }
            }
            else {
                DBG("Stream is null");
            }
            
            // Final cleanup - check if component is still valid
            if (safeThis != nullptr)
            {
                DBG("Clearing file chooser...");
                safeThis->fileChooser = nullptr;
                DBG("File chooser cleared");
            }
            else
            {
                DBG("Component invalid during cleanup - skipping file chooser clear");
            }
            
            DBG("FileChooser callback completed");
        });
    DBG("loadMidiFile() setup completed");
}

void MainComponent::playMidiFile()
{
    if (midiSequence.getNumEvents() > 0)
    {
        isPlaying = true;
        playbackPosition.store(0.0);
        currentLoopIteration = 0;
        isLooping = true;
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
        // Audio scheduling will begin from beat 0.
    }
}

void MainComponent::stopMidiFile()
{
    isPlaying = false;
    synth.allNotesOff(0, true);
    for (int channel = 1; channel <= 16; ++channel)
    {
        for (int note = 0; note < 128; ++note)
            synth.noteOff(channel, note, 0.0f, true);
    }
    
    playbackPosition.store(0.0);
    currentLoopIteration = 0;  // Reset the loop counter for the next playback
    
    // Do not set isLooping to false if you want the loop region to persist.
    
    playButton.setEnabled(true);
    stopButton.setEnabled(false);
}


void MainComponent::setupLoopRegion()
{
    auto dialogWindow = std::make_unique<juce::AlertWindow>(
        "Set Loop Region",
        "Enter loop parameters (in beats)",
        juce::AlertWindow::QuestionIcon);

    dialogWindow->addTextEditor("startBeat", "0", "Start Beat:");
    dialogWindow->addTextEditor("endBeat", "4", "End Beat:");
    dialogWindow->addTextEditor("loopCount", "2", "Number of Loops:");

    dialogWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialogWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialogWindow->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, dialogWindow = std::move(dialogWindow)](int result) mutable
            {
                if (result == 1)  // "OK" pressed
                {
                    double startBeat = dialogWindow->getTextEditorContents("startBeat").getDoubleValue();
                    double endBeat = dialogWindow->getTextEditorContents("endBeat").getDoubleValue();
                    int loops = dialogWindow->getTextEditorContents("loopCount").getIntValue();
                    
                    // Update the piano roll display.
                    pianoRoll.setLoopRegion(startBeat, endBeat, loops);

                    // Update internal loop parameters.
                    loopStartBeat = startBeat;
                    loopEndBeat = endBeat;
                    loopCount = loops;
                    isLooping = (loopCount > 0);
                    currentLoopIteration = 0;

                    // Stop the playback
                    stopMidiFile();
                    
                    // If the current playback position is beyond the new loop's end, reset it.
                    double currentPos = playbackPosition.load();
                    if (currentPos > loopEndBeat)
                        playbackPosition.store(loopStartBeat);
                }
            }
        ));
}



void MainComponent::clearLoopRegion()
{
    pianoRoll.setLoopRegion(0, 0, 0);

    loopStartBeat = 0;
    loopEndBeat = 0;
    loopCount = 0;
    isLooping = false;
    currentLoopIteration = 0;  // Reset loop iteration counter when clearing loop
}

int MainComponent::findEventAtTime(double timeStamp)
{
    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
    {
        if (midiSequence.getEventPointer(i)->message.getTimeStamp() >= timeStamp)
            return i;
    }
    return midiSequence.getNumEvents();
}

bool MainComponent::keyPressed(const juce::KeyPress& key, Component* originatingComponent)
{
    // Check if spacebar was pressed
    if (key == juce::KeyPress::spaceKey)
    {
        // Toggle playback
        if (isPlaying)
            stopMidiFile();
        else if (playButton.isEnabled()) // Only start if we have a file loaded
            playMidiFile();
            
        return true; // Key was handled
    }
    
    return false; // Key wasn't handled
}
