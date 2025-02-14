#include "MainComponent.h"

MainComponent::MainComponent()
{
    // Enable keyboard input
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    
    DBG("Initializing audio device manager...");
    
    // Initialize audio device manager with default device
    auto error = audioDeviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
    {
        DBG("Error initializing with default devices: " + error);
        
        // If default initialization fails, try with specific settings
        auto setup = audioDeviceManager.getAudioDeviceSetup();
        setup.outputChannels = 2;
        setup.useDefaultOutputChannels = true;
        setup.bufferSize = 1024;  // Larger buffer size for stability
        setup.sampleRate = 44100.0;
        
        error = audioDeviceManager.initialise(0, 2, nullptr, true);
        if (error.isNotEmpty())
        {
            DBG("Error initializing audio: " + error);
        }
        
        error = audioDeviceManager.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
        {
            DBG("Error configuring audio device: " + error);
        }
    }
    
    // Get the configured device and log its details
    if (auto* device = audioDeviceManager.getCurrentAudioDevice())
    {
        DBG("Audio device initialized successfully");
        DBG("Device name: " + device->getName());
        DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
        DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
        
        // Set the sample rate for the synths based on the actual device sample rate
        synth.setCurrentPlaybackSampleRate(device->getCurrentSampleRate());
        sf2Synth.setCurrentPlaybackSampleRate(device->getCurrentSampleRate());
    }
    else
    {
        DBG("Failed to get audio device - attempting fallback initialization");
        
        // Fallback to null device for iOS simulator
        #if JUCE_IOS
        audioDeviceManager.initialiseWithDefaultDevices(0, 2);
        synth.setCurrentPlaybackSampleRate(44100.0);
        sf2Synth.setCurrentPlaybackSampleRate(44100.0);
        DBG("Using fallback audio configuration for iOS");
        #endif
    }
    
    audioDeviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
    
    // Initialize SF2 synth
    formatManager.registerBasicFormats();
    
    // Add voices for polyphony
    for (int i = 0; i < 128; ++i) {
        sf2Synth.addVoice(std::make_unique<sfzero::Voice>().release());
    }

    // Create and load the SF2 sound
    auto tempFile = juce::File::createTempFile(".sf2");
    tempFile.replaceWithData(BinaryData::Korg_Triton_Piano_sf2, BinaryData::Korg_Triton_Piano_sf2Size);
    
    // Create SF2Sound with the temp file
    sf2Sound = new sfzero::SF2Sound(tempFile);  // ReferenceCountedObjectPtr will handle the reference counting
    DBG("SF2 sound created with file: " + tempFile.getFullPathName());
    
    sf2Sound->loadRegions();
    DBG("Regions loaded: " + juce::String(sf2Sound->getNumRegions()));
    
    sf2Sound->loadSamples(&formatManager);
    DBG("Samples loaded");
    
    // Clean up temp file
    tempFile.deleteFile();

    sf2Synth.clearSounds();
    sf2Synth.addSound(sf2Sound);  // The synth will increment the reference count
    DBG("SF2 sound added to synth");

    // Initialize GUI components
    addAndMakeVisible(&loadButton);
    addAndMakeVisible(&playButton);
    addAndMakeVisible(&stopButton);
    addAndMakeVisible(&setLoopButton);
    addAndMakeVisible(&clearLoopButton);
    addAndMakeVisible(&pianoRoll);
    
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

    // Initialize sine wave synth
    for (int i = 0; i < 8; ++i)
    {
        synth.addVoice(std::make_unique<SineWaveVoice>().release());
    }
    synth.addSound(std::make_unique<SineWaveSound>().release());
}

MainComponent::~MainComponent()
{
    // First stop any playback
    isPlaying = false;
    
    // Remove audio callback first to prevent audio thread accessing synth
    audioSourcePlayer.setSource(nullptr);
    audioDeviceManager.removeAudioCallback(&audioSourcePlayer);
    
    // Stop all notes
    sf2Synth.allNotesOff(0, true);
    synth.allNotesOff(0, true);
    
    // Wait briefly for any pending audio processing
    juce::Thread::sleep(50);
    
    // Clear sounds AFTER all notes are off and audio processing is stopped
    sf2Synth.clearSounds();  // This will decrement the reference count
    synth.clearSounds();
    
    // Clear our reference to sf2Sound which will trigger its cleanup
    sf2Sound = nullptr;
    
    // Remove keyboard listener
    removeKeyListener(this);
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
    DBG("prepareToPlay called - Sample rate: " + juce::String(sampleRate) + 
        " Block size: " + juce::String(samplesPerBlockExpected));
        
    // Initialize both synths with proper sample rate
    synth.setCurrentPlaybackSampleRate(sampleRate);
    sf2Synth.setCurrentPlaybackSampleRate(sampleRate);
    
    // Clear any lingering notes
    if (useSF2Synth) {
        sf2Synth.allNotesOff(0, true);
    } else {
        synth.allNotesOff(0, true);
    }
    
    DBG("Synths prepared for playback");
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

void MainComponent::processSegment(
    const juce::AudioSourceChannelInfo& bufferInfo,
    int segmentStartSample,
    int segmentNumSamples,
    double segmentStartBeat,
    double segmentEndBeat)
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    if (!device) return;
    
    const double sampleRate = device->getCurrentSampleRate();
    juce::MidiBuffer midiBuffer;
    
    int eventIndex = findEventIndexForBeat(segmentStartBeat);
    
    // Process events until an event's beat exceeds segmentEndBeat
    while (eventIndex < midiSequence.getNumEvents())
    {
        auto* eventData = midiSequence.getEventPointer(eventIndex);
        double eventBeat = convertTicksToBeats(eventData->message.getTimeStamp());
        
        if (eventBeat >= segmentEndBeat)
            break;
            
        if (eventBeat >= segmentStartBeat)
        {
            double eventOffsetInBeats = eventBeat - segmentStartBeat;
            double eventTimeMs = convertBeatsToMilliseconds(eventOffsetInBeats);
            int eventSampleOffset = static_cast<int>((eventTimeMs * sampleRate) / 1000.0);
            
            if (eventSampleOffset >= 0 && eventSampleOffset < segmentNumSamples)
            {
                midiBuffer.addEvent(eventData->message, 
                                  bufferInfo.startSample + segmentStartSample + eventSampleOffset);
                                  
                if (eventData->message.isNoteOn())
                {
                    DBG("Note On - Channel: " + juce::String(eventData->message.getChannel()) +
                        " Note: " + juce::String(eventData->message.getNoteNumber()) +
                        " Velocity: " + juce::String(eventData->message.getVelocity()));
                }
                else if (eventData->message.isNoteOff())
                {
                    DBG("Note Off - Channel: " + juce::String(eventData->message.getChannel()) +
                        " Note: " + juce::String(eventData->message.getNoteNumber()));
                }
            }
        }
        ++eventIndex;
    }

    // Render audio for the appropriate synth
    if (useSF2Synth) {            
        sf2Synth.renderNextBlock(*bufferInfo.buffer,
                                midiBuffer,
                                bufferInfo.startSample + segmentStartSample,
                                segmentNumSamples);
    } else {
        synth.renderNextBlock(*bufferInfo.buffer,
                             midiBuffer,
                             bufferInfo.startSample + segmentStartSample,
                             segmentNumSamples);
    }
}


void MainComponent::reTriggerSustainedNotesAt(double boundaryBeat)
{
    // Define a small tolerance (in beats) to catch events that are just after the boundary
    const double epsilon = 0.05;  // Adjust as needed
    
    // Iterate over all MIDI events
    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
    {
        auto* evt = midiSequence.getEventPointer(i);
        if (evt->message.isNoteOn() && evt->noteOffObject != nullptr)
        {
            double noteOnBeat = convertTicksToBeats(evt->message.getTimeStamp());
            double noteOffBeat = convertTicksToBeats(evt->noteOffObject->message.getTimeStamp());
            
            // Allow a small tolerance on the noteOnBeat check
            if (noteOnBeat <= (boundaryBeat + epsilon) && noteOffBeat > boundaryBeat)
            {
                if (useSF2Synth) {
                    sf2Synth.noteOn(evt->message.getChannel(),
                                  evt->message.getNoteNumber(),
                                  evt->message.getVelocity() / 127.0f);
                } else {
                    synth.noteOn(evt->message.getChannel(),
                               evt->message.getNoteNumber(),
                               evt->message.getVelocity() / 127.0f);
                }
            }
        }
    }
}


void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Always clear the buffer first
    bufferToFill.clearActiveBufferRegion();
    
    // If looping is enabled and the current playback position is beyond the loop's end,
    // reset it to the loop start. This avoids processing negative durations.
    if (isLooping && (playbackPosition.load() > loopEndBeat))
    {
        playbackPosition.store(loopStartBeat);
    }

    auto* device = audioDeviceManager.getCurrentAudioDevice();
    if (device == nullptr || !isPlaying)
        return;
        
    const double sampleRate = device->getCurrentSampleRate();
    const int numSamples = bufferToFill.numSamples;
    
    // Get a local copy of tempo and compute seconds per beat
    const double currentTempo = tempo;
    const double secondsPerBeat = 60.0 / currentTempo;
    
    // Calculate how many beats this block covers
    const double blockBeats = (numSamples / sampleRate) / secondsPerBeat;
    
    // Read the current playback position
    double currentBeat = playbackPosition.load();
    
    // Compute the "file end" as one beat after the last event's beat
    double lastEventBeat = 0.0;
    if (midiSequence.getNumEvents() > 0)
    {
        auto* lastEvent = midiSequence.getEventPointer(midiSequence.getNumEvents() - 1);
        lastEventBeat = convertTicksToBeats(lastEvent->message.getTimeStamp());
    }
    const double fileEndBeat = lastEventBeat + 1.0;
    
    // --- Non-looping case: if we are not in loop mode, check for end-of-file
    if (!isLooping && (currentBeat + blockBeats) >= fileEndBeat)
    {
        double beatsToEnd = fileEndBeat - currentBeat;
        int samplesToEnd = juce::jmin(numSamples, (int)(beatsToEnd * secondsPerBeat * sampleRate));
        
        processSegment(bufferToFill, 0, samplesToEnd, currentBeat, fileEndBeat);
        playbackPosition.store(fileEndBeat);
        
        isPlaying = false;
        if (useSF2Synth) {
            sf2Synth.allNotesOff(0, true);
        } else {
            synth.allNotesOff(0, true);
        }
        juce::MessageManager::callAsync([this]() {
            playButton.setEnabled(true);
            stopButton.setEnabled(false);
        });
        
        return;
    }
    
    // --- Looping case: if we are looping and this block crosses loopEndBeat
    if (isLooping && (currentBeat + blockBeats) >= loopEndBeat)
    {
        // Process from currentBeat up to loopEndBeat
        const double beatsToLoopEnd = loopEndBeat - currentBeat;
        const int samplesToLoopEnd = juce::jmin(numSamples, (int)(beatsToLoopEnd * secondsPerBeat * sampleRate));
        processSegment(bufferToFill, 0, samplesToLoopEnd, currentBeat, loopEndBeat);
        
        int remainingSamples = numSamples - samplesToLoopEnd;
        
        if (currentLoopIteration < loopCount - 1)
        {
            // For non-final iterations, clear active voices, re-trigger sustained notes at loopStartBeat
            if (useSF2Synth) {
                sf2Synth.allNotesOff(0, true);
            } else {
                synth.allNotesOff(0, true);
            }
            
            // Re-trigger notes at loop start
            reTriggerSustainedNotesAt(loopStartBeat);
            
            const double newSegmentStartBeat = loopStartBeat;
            const double newSegmentEndBeat = newSegmentStartBeat + (remainingSamples / sampleRate) / secondsPerBeat;
            processSegment(bufferToFill, samplesToLoopEnd, remainingSamples, newSegmentStartBeat, newSegmentEndBeat);
            playbackPosition.store(newSegmentEndBeat);
            
            currentLoopIteration++;            
        }
        else
        {
            // FINAL loop iteration - handle transition out of loop
            const double extraBeats = (remainingSamples / sampleRate) / secondsPerBeat;
            
            // Re-trigger notes that should continue past loop end
            reTriggerSustainedNotesAt(loopEndBeat);
            
            // Process the remaining samples starting exactly at loop end
            processSegment(bufferToFill, samplesToLoopEnd, remainingSamples, loopEndBeat, loopEndBeat + extraBeats);
            playbackPosition.store(loopEndBeat + extraBeats);
            
            // Turn off looping so that subsequent blocks process events normally
            isLooping = false;
            
            // Reset the loop counter so that the loop region remains active for subsequent playbacks
            currentLoopIteration = 0;
        }
        
        return;
    }
    
    // --- Default (non-crossing) case: process the entire block normally
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
        // Stop any currently playing notes
        if (useSF2Synth) {
            sf2Synth.allNotesOff(0, true);
        } else {
            synth.allNotesOff(0, true);
        }
        
        isPlaying = true;
        playbackPosition.store(0.0);
        currentLoopIteration = 0;
        
        // Restore looping state if a loop region is defined
        if (loopCount > 0 && loopEndBeat > loopStartBeat) {
            isLooping = true;
        }
        
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
        
        // Trigger initial sustained notes
        reTriggerSustainedNotesAt(0.0);
    }
}

void MainComponent::stopMidiFile()
{
    isPlaying = false;
    
    // Stop all notes on the SF2 synth
    if (useSF2Synth) {
        sf2Synth.allNotesOff(0, true);
    } else {
        synth.allNotesOff(0, true);
    }
    
    playbackPosition.store(0.0);
    currentLoopIteration = 0;
    
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
