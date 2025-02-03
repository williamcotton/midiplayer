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
        if (midiProcessor != nullptr)
        {
            midiProcessor->setTempo(tempo);
        }
        DBG("Tempo changed to: " + juce::String(tempo) + " BPM");
    };
    
    // Initialize synthesizer
    synth.addSound(new SineWaveSound());
    for (int i = 0; i < 16; ++i)
        synth.addVoice(new SineWaveVoice());
        
    // Initialize MIDI processor
    midiProcessor = std::make_unique<MidiProcessorThread>();
    midiProcessor->setSynth(&synth);
    midiProcessor->setTempo(tempo);
    
    // Setup audio
    audioDeviceManager.initialiseWithDefaultDevices(0, 2);
    audioDeviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
    
    setSize(800, 600);
    startTimer(50); // For updating the piano roll display only
}

MainComponent::~MainComponent()
{
    stopMidiFile();
    audioSourcePlayer.setSource(nullptr);
    audioDeviceManager.removeAudioCallback(&audioSourcePlayer);
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

void MainComponent::timerCallback()
{
    // Update piano roll display only
    if (isPlaying && midiProcessor != nullptr)
    {
        pianoRoll.setPlaybackPosition(midiProcessor->getPlaybackPosition());
    }
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
                if (midiFile.readFrom(*stream))
                {
                    DBG("MIDI file read successfully");
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
                        
                        // Update MIDI processor and piano roll
                        midiProcessor->setMidiSequence(midiSequence);
                        pianoRoll.setMidiSequence(midiSequence);
                        
                        playButton.setEnabled(true);
                        stopButton.setEnabled(false);
                        isPlaying = false;
                    }
                }
            }
        });
}

void MainComponent::playMidiFile()
{
    if (!isPlaying)
    {
        isPlaying = true;
        playButton.setEnabled(false);
        stopButton.setEnabled(true);
        midiProcessor->startPlayback();
    }
}

void MainComponent::stopMidiFile()
{
    if (isPlaying)
    {
        isPlaying = false;
        midiProcessor->stopPlayback();
        pianoRoll.setPlaybackPosition(0.0);
        playButton.setEnabled(true);
        stopButton.setEnabled(false);
    }
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
                    midiProcessor->setLoopRegion(startBeat, endBeat, loops);
                }
            }));
}

void MainComponent::clearLoopRegion()
{
    pianoRoll.setLoopRegion(0, 0, 0);
    midiProcessor->setLoopRegion(0, 0, 0);
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
    
    return false;
}
