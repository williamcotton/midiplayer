#include "MainComponent.h"

MainComponent::MainComponent()
{
    // Set up GUI
    addAndMakeVisible(loadButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    
    loadButton.setButtonText("Load MIDI File");
    playButton.setButtonText("Play");
    stopButton.setButtonText("Stop");
    
    playButton.setEnabled(false);
    stopButton.setEnabled(false);
    
    loadButton.onClick = [this]() { loadMidiFile(); };
    playButton.onClick = [this]() { playMidiFile(); };
    stopButton.onClick = [this]() { stopMidiFile(); };
    
    setSize(400, 200);
    startTimer(50);

    // Set up audio
    auto result = audioDeviceManager.initialiseWithDefaultDevices(2, 2);
    if (result.isNotEmpty())
    {
        DBG("Error initializing audio: " + result);
    }
    
    audioDeviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);

    // Set up synthesizer
    for (int i = 0; i < 8; ++i)  // 8 voice polyphony
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
    
    loadButton.setBounds(area.removeFromTop(buttonHeight).reduced(padding));
    area.removeFromTop(padding);
    playButton.setBounds(area.removeFromTop(buttonHeight).reduced(padding));
    area.removeFromTop(padding);
    stopButton.setBounds(area.removeFromTop(buttonHeight).reduced(padding));
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
    // This will be called when the audio device stops or when it is being restarted
}

void MainComponent::timerCallback()
{
    if (isPlaying)
    {
        auto currentTime = juce::Time::getMillisecondCounterHiRes();
        auto deltaTime = currentTime - lastTime;
        
        while (currentEvent < midiSequence.getNumEvents() &&
               midiSequence.getEventPointer(currentEvent)->message.getTimeStamp() <= playbackPosition)
        {
            auto& midimsg = midiSequence.getEventPointer(currentEvent)->message;
            DBG("Playing MIDI event: " + midimsg.getDescription());
            
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
        
        if (currentEvent >= midiSequence.getNumEvents())
        {
            DBG("Reached end of sequence");
            stopMidiFile();
        }
        
        playbackPosition += deltaTime;
        lastTime = currentTime;
    }
}

void MainComponent::loadMidiFile()
{
    DBG("Starting loadMidiFile function");
    
    auto fileChooserFlags =
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;
    
    auto* chooser = new juce::FileChooser(
        "Select a MIDI file",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");
        
    DBG("Created FileChooser");

    chooser->launchAsync(fileChooserFlags,
        [this, chooser](const juce::FileChooser& fc)
        {
            DBG("In FileChooser callback");
            auto result = fc.getResult();
            
            if (result.exists())
            {
                DBG("Selected file: " + result.getFullPathName());
                auto stream = std::make_unique<juce::FileInputStream>(result);
                
                if (stream->openedOk())
                {
                    if (midiFile.readFrom(*stream))
                    {
                        DBG("Successfully read MIDI file");
                        if (midiFile.getNumTracks() > 0)
                        {
                            midiSequence = *midiFile.getTrack(0);
                            DBG("Got track 0, events: " + juce::String(midiSequence.getNumEvents()));
                            
                            for (int i = 1; i < midiFile.getNumTracks(); ++i)
                            {
                                midiSequence.addSequence(*midiFile.getTrack(i),
                                                       0.0, 0.0,
                                                       midiFile.getLastTimestamp());
                            }
                            
                            midiSequence.updateMatchedPairs();
                            playButton.setEnabled(true);
                            stopButton.setEnabled(false);
                            currentEvent = 0;
                            playbackPosition = 0.0;
                        }
                        else
                        {
                            DBG("No tracks in MIDI file");
                        }
                    }
                    else
                    {
                        DBG("Failed to read MIDI file");
                    }
                }
                else
                {
                    DBG("Failed to create input stream");
                }
            }
            else
            {
                DBG("No file selected or file doesn't exist");
            }
            delete chooser;
        });
    
    DBG("launchAsync called");
}

void MainComponent::playMidiFile()
{
    if (currentEvent < midiSequence.getNumEvents())
    {
        DBG("Starting playback");
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