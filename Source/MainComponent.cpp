#include "MainComponent.h"
#include "MidiPlayer.h"

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

    // MidiPlayer
    midiPlayer = std::make_unique<MidiPlayer>();
    
    // Setup tempo control
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    
    tempoLabel.setText("Tempo (BPM)", juce::dontSendNotification);
    tempoSlider.setRange(30.0, 300.0, 1.0);
    tempoSlider.setValue(120.0, juce::dontSendNotification);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    tempoSlider.onValueChange = [this] { 
        midiPlayer->tempo = tempoSlider.getValue();
        DBG("Tempo changed to: " + juce::String(midiPlayer->tempo) + " BPM");
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

void MainComponent::timerCallback()
{
    if (isPlaying)
    {
        auto currentTime = juce::Time::getMillisecondCounterHiRes();
        auto deltaTimeMs = currentTime - lastTime;
        auto deltaBeats = midiPlayer->convertMillisecondsToBeats(deltaTimeMs);
        
        // Update position in beats
        auto newPosition = playbackPosition + deltaBeats;
        
        // Check for looping first
        if (pianoRoll.isPositionInLoop(playbackPosition))
        {
            if ((currentLoopIteration + 1) < pianoRoll.getLoopCount())
            {
                if (newPosition >= pianoRoll.getLoopEndBeat())
                {
                    // Turn off all currently playing notes before loop
                    synth.allNotesOff(0, true);
                    
                    newPosition = pianoRoll.getLoopStartBeat();
                    double loopStartTicks = midiPlayer->convertBeatsToTicks(newPosition);
                    
                    // Find the first event that's at or after our loop start point
                    currentEvent = 0;
                    while (currentEvent < midiPlayer->midiSequence.getNumEvents())
                    {
                        auto eventTime = midiPlayer->midiSequence.getEventPointer(currentEvent)->message.getTimeStamp();
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
                        auto* event = midiPlayer->midiSequence.getEventPointer(i);
                        if (event->message.isNoteOn())
                        {
                            auto noteOff = event->noteOffObject;
                            if (noteOff != nullptr)
                            {
                                auto noteOnTime = midiPlayer->convertTicksToBeats(event->message.getTimeStamp());
                                auto noteOffTime = midiPlayer->convertTicksToBeats(noteOff->message.getTimeStamp());
                                
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
            for (int i = 0; i < midiPlayer->midiSequence.getNumEvents(); ++i)
            {
                lastEventTime = std::max(lastEventTime, midiPlayer->midiSequence.getEventPointer(i)->message.getTimeStamp());
            }
            double lastEventBeat = midiPlayer->convertTicksToBeats(lastEventTime);
            
            // If we've reached the end of the sequence and we're not looping
            if (newPosition >= lastEventBeat + 1.0)
            {
                stopMidiFile();
                return;
            }
        }
        
        // Process MIDI events
        while (currentEvent < midiPlayer->midiSequence.getNumEvents())
        {
            auto eventTime = midiPlayer->convertTicksToBeats(
                midiPlayer->midiSequence.getEventPointer(currentEvent)->message.getTimeStamp());
                
            if (eventTime <= newPosition)
            {
                auto& midimsg = midiPlayer->midiSequence.getEventPointer(currentEvent)->message;
                
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
    auto fileChooserFlags =
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;
    
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select a MIDI file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");
        
    chooser->launchAsync(fileChooserFlags,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            std::unique_ptr<juce::InputStream> stream;
            
            #if JUCE_ANDROID
            juce::URL::InputStreamOptions options(juce::URL::ParameterHandling::inAddress);
            stream = fc.getURLResult().createInputStream(options);
            #else
            if (result.exists()) {
                stream = std::make_unique<juce::FileInputStream>(result);
            }
            #endif
            
            if (stream != nullptr)
            {
                DBG("Stream opened successfully");
                if (midiPlayer->midiFile.readFrom(*stream))
                {
                    DBG("MIDI file read successfully");
                    if (midiPlayer->midiFile.getNumTracks() > 0)
                    {
                        midiPlayer->midiSequence = *midiPlayer->midiFile.getTrack(0);
                        
                        for (int i = 1; i < midiPlayer->midiFile.getNumTracks(); ++i)
                        {
                            midiPlayer->midiSequence.addSequence(*midiPlayer->midiFile.getTrack(i),
                                                   0.0, 0.0,
                                                   midiPlayer->midiFile.getLastTimestamp());
                        }
                        
                        midiPlayer->midiSequence.updateMatchedPairs();
                        pianoRoll.setMidiSequence(midiPlayer->midiSequence);
                        playButton.setEnabled(true);
                        stopButton.setEnabled(false);
                        currentEvent = 0;
                        playbackPosition = 0.0;
                        lastTime = juce::Time::getMillisecondCounterHiRes();
                    }
                }
            }
        });
}

void MainComponent::playMidiFile()
{
    if (currentEvent < midiPlayer->midiSequence.getNumEvents())
    {
        isPlaying = true;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
        
        // Reset loop iteration counter when starting playback
        currentLoopIteration = 0;
        
        // If we're starting from a position within the loop region,
        // make sure we process any notes that should be playing
        if (pianoRoll.isPositionInLoop(playbackPosition))
        {
            // double loopStartTicks  = convertBeatsToTicks(playbackPosition);
            
            // Find notes that should be playing at this position
            for (int i = 0; i < currentEvent; ++i)
            {
                auto* event = midiPlayer->midiSequence.getEventPointer(i);
                auto eventTime = midiPlayer->convertTicksToBeats(event->message.getTimeStamp());
                
                if (event->message.isNoteOn())
                {
                    auto noteOff = event->noteOffObject;
                    if (noteOff != nullptr)
                    {
                        auto noteOffTime = midiPlayer->convertTicksToBeats(noteOff->message.getTimeStamp());
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
                    currentLoopIteration = 0;
                }
            }
        ));
    }

void MainComponent::clearLoopRegion()
{
    pianoRoll.setLoopRegion(0, 0, 0);
    currentLoopIteration = 0;  // Reset loop iteration counter when clearing loop
}

int MainComponent::findEventAtTime(double timeStamp)
{
    for (int i = 0; i < midiPlayer->midiSequence.getNumEvents(); ++i)
    {
        if (midiPlayer->midiSequence.getEventPointer(i)->message.getTimeStamp() >= timeStamp)
            return i;
    }
    return midiPlayer->midiSequence.getNumEvents();
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
