#include "MainComponent.h"

MainComponent::MainComponent()
{
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

    auto result = audioDeviceManager.initialiseWithDefaultDevices(2, 2);
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
        auto deltaBeats = convertMillisecondsToBeats(deltaTimeMs);
        
        // Update position in beats
        auto newPosition = playbackPosition + deltaBeats;
        
        if (pianoRoll.isPositionInLoop(playbackPosition))
        {
            if (currentLoopIteration < pianoRoll.getLoopCount())
            {
                if (newPosition >= pianoRoll.getLoopEndBeat())
                {
                    // Turn off all currently playing notes before loop
                    synth.allNotesOff(0, true);
                    
                    newPosition = pianoRoll.getLoopStartBeat();
                    
                    // Scan backwards to find the last event before loop start
                    // This ensures we catch any notes that should be playing at loop start
                    int newEvent = 0;
                    double loopStartTicks = convertBeatsToTicks(newPosition);
                    
                    for (int i = 0; i < midiSequence.getNumEvents(); ++i)
                    {
                        if (midiSequence.getEventPointer(i)->message.getTimeStamp() <= loopStartTicks)
                        {
                            newEvent = i;
                        }
                        else
                        {
                            break;
                        }
                    }
                    
                    // Now scan forward from this point to find any notes that should be playing
                    for (int i = newEvent; i < midiSequence.getNumEvents(); ++i)
                    {
                        auto* event = midiSequence.getEventPointer(i);
                        auto eventTime = convertTicksToBeats(event->message.getTimeStamp());
                        
                        // If we've gone past our new position, stop scanning
                        if (eventTime > newPosition)
                            break;
                            
                        // If this is a note that started before our loop point and has a note-off
                        // after our loop point, trigger it
                        if (event->message.isNoteOn())
                        {
                            auto noteOff = event->noteOffObject;
                            if (noteOff != nullptr)
                            {
                                auto noteOffTime = convertTicksToBeats(noteOff->message.getTimeStamp());
                                if (noteOffTime > newPosition)
                                {
                                    synth.noteOn(event->message.getChannel(),
                                               event->message.getNoteNumber(),
                                               event->message.getVelocity() / 127.0f);
                                }
                            }
                        }
                    }
                    
                    currentEvent = findEventAtTime(convertBeatsToTicks(newPosition));
                    currentLoopIteration++;
                }
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
    auto fileChooserFlags =
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;
    
    auto* chooser = new juce::FileChooser(
        "Select a MIDI file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");
        
    chooser->launchAsync(fileChooserFlags,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            
            if (result.exists())
            {
                auto stream = std::make_unique<juce::FileInputStream>(result);
                
                if (stream->openedOk())
                {
                    if (midiFile.readFrom(*stream))
                    {
                        if (midiFile.getNumTracks() > 0)
                        {
                            midiSequence = *midiFile.getTrack(0);
                            
                            for (int i = 1; i < midiFile.getNumTracks(); ++i)
                            {
                                midiSequence.addSequence(*midiFile.getTrack(i),
                                                       0.0, 0.0,
                                                       midiFile.getLastTimestamp());
                            }
                            
                            midiSequence.updateMatchedPairs();
                            pianoRoll.setMidiSequence(midiSequence);
                            playButton.setEnabled(true);
                            stopButton.setEnabled(false);
                            currentEvent = 0;
                            playbackPosition = 0.0;  // Now in beats
                            lastTime = juce::Time::getMillisecondCounterHiRes();
                        }
                    }
                }
            }
            delete chooser;
        });
}

void MainComponent::playMidiFile()
{
    if (currentEvent < midiSequence.getNumEvents())
    {
        isPlaying = true;
        lastTime = juce::Time::getMillisecondCounterHiRes();
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
    }
}

void MainComponent::stopMidiFile()
{
    isPlaying = false;
    synth.allNotesOff(0, true);
    
    currentEvent = 0;
    playbackPosition = 0.0;
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
    currentLoopIteration = 0;
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
