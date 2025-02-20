#include "MainComponent.h"
#include "SynthAudioSource.h" // Make sure this header is available in your project

MainComponent::MainComponent()
    : loadButton("Load MIDI File"), playButton("Play"), stopButton("Stop"),
      setLoopButton("Set Loop"), clearLoopButton("Clear Loop"),
      transpositionLabel("TranspositionLabel", "Transpose"), pianoRoll(),
      tempoSlider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
      tempoLabel("TempoLabel", "Tempo") {
  // Enable keyboard input and add ourselves as a key listener.
  setWantsKeyboardFocus(true);
  addKeyListener(this);

  // Initialize the audio device manager (0 inputs, 2 outputs)
  auto error = audioDeviceManager.initialiseWithDefaultDevices(0, 2);
  if (error.isNotEmpty()) {
    DBG("Audio device initialization error: " + error);
  }

  // Add our AudioSourcePlayer as a callback to the device manager.
  audioDeviceManager.addAudioCallback(&audioSourcePlayer);

  // Create a MixerAudioSource.
  audioMixerSource = std::make_unique<juce::MixerAudioSource>();

  // Create our multi-channel SynthAudioSource
  synthAudioSource = std::make_unique<SynthAudioSource>();

  if (synthAudioSource != nullptr) {
    if (auto *sound = synthAudioSource->getSF2Sound()) {
      presetBox.setTextWhenNothingSelected("Select Preset");
      presetBox.clear();
      for (int i = 0; i < sound->numSubsounds(); ++i) {
        presetBox.addItem(sound->subsoundName(i),
                          i + 1); // ComboBox items are 1-indexed
      }

      // Set initial preset if available
      if (presetBox.getNumItems() > 0) {
        presetBox.setSelectedId(1, juce::dontSendNotification);
        sound->useSubsound(0);
      }

      // Add an onChange callback for when the user selects a new preset.
      presetBox.onChange = [this]() {
        if (presetBox.getSelectedId() > 0) {
          // Set up channel 0 (piano) with the selected preset
          synthAudioSource->setupChannel(0, presetBox.getSelectedId() - 1);
          DBG("Changed to preset: " + presetBox.getText());
        }
      };
    }
  }

  // Set up transposition combo box
  addAndMakeVisible(transpositionBox);
  addAndMakeVisible(transpositionLabel);
  transpositionLabel.setText("Transpose", juce::dontSendNotification);
  
  transpositionBox.setTextWhenNothingSelected("No Transpose");
  for (int i = -12; i <= 12; ++i) {
    transpositionBox.addItem(juce::String(i) + " semitones", i + 13); // Make IDs 1-based
  }
  transpositionBox.setSelectedId(13, juce::dontSendNotification); // Select 0 (no transposition)
  
  transpositionBox.onChange = [this]() {
    int transposition = transpositionBox.getSelectedId() - 13; // Convert back to -12 to +12 range
    if (synthAudioSource != nullptr) {
      synthAudioSource->stopAllNotes(); // Stop all currently playing notes
      synthAudioSource->setTransposition(transposition);
    }
    pianoRoll.setTransposition(transposition);
  };

  // Create the MidiSchedulerAudioSource, passing the synth.
  midiSchedulerAudioSource =
      std::make_unique<MidiSchedulerAudioSource>(synthAudioSource.get());

  // Add the scheduler (which now handles looping and playback) to the mixer.
  audioMixerSource->addInputSource(midiSchedulerAudioSource.get(), false);

  // Set the mixer as the source for the AudioSourcePlayer.
  audioSourcePlayer.setSource(audioMixerSource.get());

  //--- Tempo Slider & Label Setup ---
  addAndMakeVisible(tempoSlider);
  addAndMakeVisible(tempoLabel);

  tempoLabel.setText("Tempo", juce::dontSendNotification);
  tempoSlider.setRange(30.0, 300.0, 1.0);
  tempoSlider.setValue(120.0, juce::dontSendNotification);
  tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
  tempoSlider.onValueChange = [this]() {
    double newTempo = tempoSlider.getValue();
    tempo = newTempo; // Update our member variable
    midiSchedulerAudioSource->setTempo(newTempo);
    DBG("Manual tempo change to: " + juce::String(newTempo) + " BPM");
  };

  // Set up tempo change callback
  midiSchedulerAudioSource->onTempoChanged = [this](double newTempo) {
    DBG("Received tempo change callback: " + juce::String(newTempo) + " BPM");
    juce::MessageManager::callAsync([this, newTempo]() {
      DBG("Setting tempo slider to: " + juce::String(newTempo) + " BPM");
      tempoSlider.setValue(newTempo, juce::sendNotification);
    });
  };

  midiSchedulerAudioSource->onPlaybackStopped = [this]() {
    // This code executes on the message thread.
    playButton.setEnabled(true);
    stopButton.setEnabled(false);
    // Optionally update any other UI state.
  };

  //--- GUI Setup ---
  addAndMakeVisible(loadButton);
  addAndMakeVisible(playButton);
  addAndMakeVisible(stopButton);
  addAndMakeVisible(setLoopButton);
  addAndMakeVisible(clearLoopButton);
  addAndMakeVisible(presetBox);
  addAndMakeVisible(pianoRoll);

  // Set button text.
  loadButton.setButtonText("Load MIDI File");
  playButton.setButtonText("Play");
  stopButton.setButtonText("Stop");
  setLoopButton.setButtonText("Set Loop");
  clearLoopButton.setButtonText("Clear Loop");

  // Button callbacks.
  loadButton.onClick = [this]() { loadMidiFile(); };
  playButton.onClick = [this]() { playMidiFile(); };
  stopButton.onClick = [this]() { stopMidiFile(); };
  setLoopButton.onClick = [this]() { setupLoopRegion(); };
  clearLoopButton.onClick = [this]() { clearLoopRegion(); };

  // Set the size of the MainComponent.
  setSize(800, 600);

  // Start our timer (for example, to update the piano roll playhead).
  startTimer(5);

  // Initialize other playback and loop-related variables.
  isPlaying = false;
  currentEvent = 0;
  playbackPosition.store(0.0);
  lastTime = juce::Time::getMillisecondCounterHiRes();
  currentLoopIteration = 0;
  loopStartBeat = 0.0;
  loopEndBeat = 0.0;
  loopCount = 0;
  isLooping = false;
}

MainComponent::~MainComponent() {
  // First, disconnect the audio callback.
  audioSourcePlayer.setSource(nullptr);

  // Remove all inputs from the mixer.
  if (audioMixerSource)
    audioMixerSource->removeAllInputs();

  // Remove our audio callback from the audio device manager.
  audioDeviceManager.removeAudioCallback(&audioSourcePlayer);

  // Remove ourselves as a key listener.
  removeKeyListener(this);

  // (Any additional cleanup for MIDI, SF2, etc., can be added here.)
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

    // First row: Load, Play, Stop, Preset
    auto topControls = area.removeFromTop(buttonHeight);
    loadButton.setBounds(topControls.removeFromLeft(120).reduced(paddingX, paddingY));
    playButton.setBounds(topControls.removeFromLeft(80).reduced(paddingX, paddingY));
    stopButton.setBounds(topControls.removeFromLeft(80).reduced(paddingX, paddingY));
    presetBox.setBounds(topControls.removeFromLeft(200).reduced(paddingX, paddingY));
    
    // Second row: Loop controls and Tempo
    auto loopControls = area.removeFromTop(buttonHeight);
    setLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(paddingX, paddingY));
    clearLoopButton.setBounds(loopControls.removeFromLeft(100).reduced(paddingX, paddingY));
    tempoLabel.setBounds(loopControls.removeFromLeft(60).reduced(paddingX, paddingY));
    tempoSlider.setBounds(loopControls.removeFromLeft(200).reduced(paddingX, paddingY));
    
    // Third row: Transposition controls
    auto transpositionControls = area.removeFromTop(buttonHeight);
    transpositionLabel.setBounds(transpositionControls.removeFromLeft(100).reduced(paddingX, paddingY));
    transpositionBox.setBounds(transpositionControls.removeFromLeft(200).reduced(paddingX, paddingY));
    
    // Remaining space for piano roll
    pianoRoll.setBounds(area.reduced(paddingX, paddingY));
}

void MainComponent::timerCallback() {
  // Instead of using a local playbackPosition member,
  // get the position from the scheduler.
  double pos = midiSchedulerAudioSource->getPlaybackPosition();
  pianoRoll.setPlaybackPosition(pos);
}

void MainComponent::loadMidiFile()
{
    DBG("Starting loadMidiFile()");
    
    // Stop any currently playing notes
    if (synthAudioSource) {
        synthAudioSource->stopPlayback();
        synthAudioSource->stopAllNotes();
    }
    
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
                            // Get the time division (PPQ) from the MIDI file
                            auto timeDivision = safeThis->midiFile.getTimeFormat();
                            if (timeDivision > 0) {
                                // Positive values indicate PPQ (ticks per quarter note)
                                DBG("MIDI file uses PPQ timing: " + juce::String(timeDivision) + " ticks per quarter note");
                                safeThis->midiSchedulerAudioSource->setPPQ(timeDivision);
                                safeThis->pianoRoll.setPPQ(timeDivision);
                            } else {
                                // Negative values indicate SMPTE timing (not supported yet)
                                DBG("Warning: SMPTE timing not supported, defaulting to 480 PPQ");
                                safeThis->midiSchedulerAudioSource->setPPQ(480);
                                safeThis->pianoRoll.setPPQ(480);
                            }

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
                            
                            // Set the MIDI sequence in the scheduler first, which will extract tempo
                            safeThis->midiSchedulerAudioSource->setMidiSequence(safeThis->midiSequence);
                            
                            // Update piano roll with time signature from scheduler
                            safeThis->pianoRoll.setTimeSignature(
                                safeThis->midiSchedulerAudioSource->getNumerator(),
                                safeThis->midiSchedulerAudioSource->getDenominator());
                            
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

void MainComponent::playMidiFile() {
  if (midiSequence.getNumEvents() > 0) {
    // Forward the MIDI sequence and tempo to the scheduler.
    midiSchedulerAudioSource->setMidiSequence(midiSequence);
    midiSchedulerAudioSource->startPlayback();

    isPlaying = true;
    playButton.setEnabled(false);
    stopButton.setEnabled(true);
  }
}

void MainComponent::stopMidiFile() {
  isPlaying = false;
  midiSchedulerAudioSource->stopPlayback();
  playbackPosition.store(0.0);
  playButton.setEnabled(true);
  stopButton.setEnabled(false);
}

void MainComponent::setupLoopRegion() {
  auto dialogWindow = std::make_unique<juce::AlertWindow>(
      "Set Loop Region", "Enter loop parameters (in beats)",
      juce::AlertWindow::QuestionIcon);

  dialogWindow->addTextEditor("startBeat", "0", "Start Beat:");
  dialogWindow->addTextEditor("endBeat", "4", "End Beat:");
  dialogWindow->addTextEditor("loopCount", "2", "Number of Loops:");

  dialogWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
  dialogWindow->addButton("Cancel", 0,
                          juce::KeyPress(juce::KeyPress::escapeKey));

  dialogWindow->enterModalState(
      true, juce::ModalCallbackFunction::create([this, dialogWindow = std::move(
                                                           dialogWindow)](
                                                    int result) mutable {
        if (result == 1) {
          double startBeat =
              dialogWindow->getTextEditorContents("startBeat").getDoubleValue();
          double endBeat =
              dialogWindow->getTextEditorContents("endBeat").getDoubleValue();
          int loops =
              dialogWindow->getTextEditorContents("loopCount").getIntValue();

          // Update the piano roll display.
          pianoRoll.setLoopRegion(startBeat, endBeat, loops);

          // Forward the loop region to the scheduler.
          midiSchedulerAudioSource->setLoopRegion(startBeat, endBeat, loops);

          // Stop playback so the new loop takes effect.
          stopMidiFile();
        }
      }));
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
