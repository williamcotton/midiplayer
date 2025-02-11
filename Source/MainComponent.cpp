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
    
    tempoLabel.setText("Tempo (BPM)", juce::dontSendNotification);
    tempoSlider.setRange(30.0, 300.0, 1.0);
    tempoSlider.setValue(120.0, juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    tempoSlider.onValueChange = [this] { 
        tempo = tempoSlider.getValue();
        DBG("Tempo changed to: " + juce::String(tempo) + " BPM");
    };
    
    setSize(800, 600);
    startTimer(50);

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
    auto padding = 10;

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
    loadButton.setBounds(topControls.removeFromLeft(120).reduced(padding, 0));
    playButton.setBounds(topControls.removeFromLeft(80).reduced(padding, 0));
    stopButton.setBounds(topControls.removeFromLeft(80).reduced(padding, 0));
    
    auto loopControls = area.removeFromTop(buttonHeight);
    setLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(padding, 0));
    clearLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(padding, 0));
    
    auto tempoControls = loopControls.removeFromLeft(250);
    tempoLabel.setBounds(tempoControls.removeFromLeft(100).reduced(padding, 0));
    tempoSlider.setBounds(tempoControls.reduced(padding, 0));
    
    pianoRoll.setBounds(area.reduced(padding));
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();
    synth.renderNextBlock(*bufferToFill.buffer, juce::MidiBuffer(),
                         bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

bool MainComponent::isPositionInLoop(double beat) const
{
    return isLooping && beat >= loopStartBeat && beat < loopEndBeat;
}

void MainComponent::timerCallback()
{
    if (isPlaying)
    {
        auto currentTime = juce::Time::getMillisecondCounterHiRes();
        auto deltaTimeMs = currentTime - lastTime;
        auto deltaBeats = convertMillisecondsToBeats(deltaTimeMs);
        
        // Update position in beats
        auto newPosition = playbackPosition + deltaBeats;
        
        // Check for looping first
        if (isPositionInLoop(playbackPosition))
        {
            if ((currentLoopIteration + 1) < loopCount) // loopCount
            {
                if (newPosition >= loopEndBeat) // loopEndBeat
                {
                    // Turn off all currently playing notes before loop
                    synth.allNotesOff(0, true);
                    
                    newPosition = loopStartBeat; // loopStartBeat
                    double loopStartTicks = convertBeatsToTicks(newPosition);
                    
                    // Find the first event that's at or after our loop start point
                    currentEvent = 0;
                    while (currentEvent < midiSequence.getNumEvents())
                    {
                        auto eventTime = midiSequence.getEventPointer(currentEvent)->message.getTimeStamp();
                        if (eventTime >= loopStartTicks)
                            break;
                        currentEvent++;
                    }
                    
                    // Step back one event to catch any notes that might start exactly at the loop point
                    if (currentEvent > 0)
                        currentEvent--;
                        
                    // Scan for notes that should be playing at loop start
                    for (int i = 0; i < currentEvent; ++i)
                    {
                        auto* event = midiSequence.getEventPointer(i);
                        if (event->message.isNoteOn())
                        {
                            auto noteOff = event->noteOffObject;
                            if (noteOff != nullptr)
                            {
                                auto noteOnTime = convertTicksToBeats(event->message.getTimeStamp());
                                auto noteOffTime = convertTicksToBeats(noteOff->message.getTimeStamp());
                                
                                if (noteOnTime <= newPosition && noteOffTime > newPosition)
                                {
                                    synth.noteOn(event->message.getChannel(),
                                               event->message.getNoteNumber(),
                                               event->message.getVelocity() / 127.0f);
                                }
                            }
                        }
                    }
                    
                    currentLoopIteration++;
                }
            }
        }
        // Only check for sequence end if we're not looping
        else
        {
            // Find the last event timestamp in ticks
            double lastEventTime = 0.0;
            for (int i = 0; i < midiSequence.getNumEvents(); ++i)
            {
                lastEventTime = std::max(lastEventTime, midiSequence.getEventPointer(i)->message.getTimeStamp());
            }
            double lastEventBeat = convertTicksToBeats(lastEventTime);
            
            // If we've reached the end of the sequence and we're not looping
            if (newPosition >= lastEventBeat + 1.0)
            {
                stopMidiFile();
                return;
            }
        }
        
        // Process MIDI events
        while (currentEvent < midiSequence.getNumEvents())
        {
            auto eventTime = convertTicksToBeats(
                midiSequence.getEventPointer(currentEvent)->message.getTimeStamp());
                
            if (eventTime <= newPosition)
            {
                auto& midimsg = midiSequence.getEventPointer(currentEvent)->message;
                
                if (midimsg.isNoteOn())
                {
                    synth.noteOn(midimsg.getChannel(),
                               midimsg.getNoteNumber(),
                               midimsg.getVelocity() / 127.0f);
                }
                else if (midimsg.isNoteOff())
                {
                    synth.noteOff(midimsg.getChannel(),
                                midimsg.getNoteNumber(),
                                midimsg.getVelocity() / 127.0f,
                                true);
                }
                
                currentEvent++;
            }
            else
            {
                break;
            }
        }
        
        // Update position for next callback
        playbackPosition = newPosition;
        lastTime = currentTime;
        
        // Update piano roll display
        pianoRoll.setPlaybackPosition(playbackPosition);
    }
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
        
    auto weakThis = juce::Component::SafePointer<MainComponent>(this);
    DBG("Launching async file chooser...");
    
    fileChooser->launchAsync(fileChooserFlags,
        [weakThis](const juce::FileChooser& fc)
        {
            DBG("FileChooser callback started");
            if (auto* comp = weakThis.getComponent())
            {
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
                
                if (stream != nullptr)
                {
                    DBG("Stream opened successfully");
                    
                    // Store the current number of tracks before reading
                    const int prevNumTracks = comp->midiFile.getNumTracks();
                    DBG("Previous number of tracks: " + juce::String(prevNumTracks));
                    
                    if (comp->midiFile.readFrom(*stream))
                    {
                        DBG("MIDI file read successfully");
                        const int newNumTracks = comp->midiFile.getNumTracks();
                        DBG("New number of tracks: " + juce::String(newNumTracks));
                        
                        if (newNumTracks > 0)
                        {
                            try {
                                DBG("Getting first track...");
                                comp->midiSequence = *comp->midiFile.getTrack(0);
                                DBG("First track copied, events: " + juce::String(comp->midiSequence.getNumEvents()));
                                
                                for (int i = 1; i < newNumTracks; ++i)
                                {
                                    DBG("Adding track " + juce::String(i));
                                    comp->midiSequence.addSequence(*comp->midiFile.getTrack(i),
                                                           0.0, 0.0,
                                                           comp->midiFile.getLastTimestamp());
                                    DBG("Track " + juce::String(i) + " added, total events now: " + 
                                        juce::String(comp->midiSequence.getNumEvents()));
                                }
                                
                                DBG("Updating matched pairs...");
                                comp->midiSequence.updateMatchedPairs();
                                
                                DBG("Setting sequence in piano roll...");
                                comp->pianoRoll.setMidiSequence(comp->midiSequence);
                                
                                DBG("Updating UI state...");
                                comp->playButton.setEnabled(true);
                                comp->stopButton.setEnabled(false);
                                comp->currentEvent = 0;
                                comp->playbackPosition = 0.0;
                                comp->lastTime = juce::Time::getMillisecondCounterHiRes();
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
                
                DBG("Clearing file chooser...");
                comp->fileChooser = nullptr;
                DBG("File chooser cleared");
            }
            else {
                DBG("Component is no longer valid");
            }
            DBG("FileChooser callback completed");
        });
    DBG("loadMidiFile() setup completed");
}

void MainComponent::playMidiFile()
{
    if (currentEvent < midiSequence.getNumEvents())
    {
        isPlaying = true;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
        
        // Reset loop iteration counter when starting playback
        currentLoopIteration = 0;
        
        // If we're starting from a position within the loop region,
        // make sure we process any notes that should be playing
        if (isPositionInLoop(playbackPosition))
        {
            // double loopStartTicks  = convertBeatsToTicks(playbackPosition);
            
            // Find notes that should be playing at this position
            for (int i = 0; i < currentEvent; ++i)
            {
                auto* event = midiSequence.getEventPointer(i);
                auto eventTime = convertTicksToBeats(event->message.getTimeStamp());
                
                if (event->message.isNoteOn())
                {
                    auto noteOff = event->noteOffObject;
                    if (noteOff != nullptr)
                    {
                        auto noteOffTime = convertTicksToBeats(noteOff->message.getTimeStamp());
                        if (eventTime <= playbackPosition && noteOffTime > playbackPosition)
                        {
                            synth.noteOn(event->message.getChannel(),
                                       event->message.getNoteNumber(),
                                       event->message.getVelocity() / 127.0f);
                        }
                    }
                }
            }
        }
    }
}

void MainComponent::stopMidiFile()
{
    isPlaying = false;
    // Turn off all notes with a proper release
    synth.allNotesOff(0, true);
    // Also explicitly turn off all notes to be extra safe
    for (int channel = 1; channel <= 16; ++channel)
    {
        for (int note = 0; note < 128; ++note)
        {
            synth.noteOff(channel, note, 0.0f, true);
        }
    }
    
    currentEvent = 0;
    playbackPosition = 0.0;
    currentLoopIteration = 0;  // Reset loop iteration counter when stopping
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
                if (result == 1)  // "OK" was pressed
                {
                    double startBeat = dialogWindow->getTextEditorContents("startBeat").getDoubleValue();
                    double endBeat = dialogWindow->getTextEditorContents("endBeat").getDoubleValue();
                    int loops = dialogWindow->getTextEditorContents("loopCount").getIntValue();
                    
                    pianoRoll.setLoopRegion(startBeat, endBeat, loops);

                    loopStartBeat = startBeat;
                    loopEndBeat = endBeat;
                    loopCount = loops;
                    isLooping = (loopCount > 0);
                    currentLoopIteration = 0;
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
