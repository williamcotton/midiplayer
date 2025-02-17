# Multi-Channel MIDI and SoundFont Implementation Summary

## Overview
This document outlines the changes needed to support multiple MIDI channels with different SoundFont presets while maintaining real-time audio performance.

## Key Changes

### 1. Shared SoundFont Resource Management
Instead of loading the SF2 file multiple times, share a single SF2 sound instance across multiple synths:

```cpp
class SynthAudioSource {
private:
    // Single shared SF2 sound instance
    std::unique_ptr<sfzero::SF2Sound> sharedSF2Sound;
    
    // Separate synth instances per channel
    std::array<std::unique_ptr<sfzero::Synth>, 16> channelSynths;
    
    // Track active channels
    std::bitset<16> activeChannels;
};
```

**Benefits:**
- Reduced memory usage
- Single load of SF2 data
- Independent synth instances for polyphony

### 2. Audio Thread Safety Optimizations
Pre-allocate all buffers and avoid real-time allocations:

```cpp
class SynthAudioSource {
private:
    // Pre-allocated mixing resources
    juce::AudioBuffer<float> mixingBuffer;
    std::array<juce::MidiBuffer, 16> channelMidiBuffers;
    int numChannels = 2;  // Cache channel count
};

void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    // Pre-allocate all buffers
    mixingBuffer.setSize(2, samplesPerBlockExpected, false, false, true);
    for (auto& buffer : channelMidiBuffers) {
        buffer.clear();
    }
    // ... other preparation ...
}
```

### 3. Efficient Multi-Channel Mixing
Implement thread-safe mixing of multiple MIDI channels:

```cpp
void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Clear existing buffers (no allocation)
    for (auto& buffer : channelMidiBuffers) {
        buffer.clear();
    }

    // Sort MIDI events by channel
    for (const auto metadata : midiBuffer) {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel() - 1;
        if (channel >= 0 && channel < 16) {
            channelMidiBuffers[channel].addEvent(msg, metadata.samplePosition);
            activeChannels.set(channel);
        }
    }

    outputBuffer.clear(startSample, numSamples);
    
    // Mix active channels
    for (int channel = 0; channel < 16; ++channel) {
        if (activeChannels[channel]) {
            mixingBuffer.clear();
            channelSynths[channel]->renderNextBlock(mixingBuffer, 
                                                  channelMidiBuffers[channel], 
                                                  0, numSamples);
            
            for (int i = 0; i < numChannels; ++i) {
                outputBuffer.addFrom(i, startSample, 
                                   mixingBuffer, i, 0, 
                                   numSamples);
            }
        }
    }
}
```

### 4. Channel Preset Management
Handle per-channel preset selection:

```cpp
void SynthAudioSource::setChannelPreset(int channel, int presetIndex) {
    if (channel >= 0 && channel < 16) {
        // Stop any playing notes
        channelSynths[channel]->allNotesOff(0, true);
        
        // Update preset
        if (sharedSF2Sound) {
            sharedSF2Sound->useSubsound(presetIndex);
        }
    }
}
```

## Important Considerations

### Memory Management
- Single SF2 sound instance shared across channels
- Pre-allocated buffers for mixing
- Clear separation between initialization and real-time processing

### Real-time Safety
- No allocations in audio callback
- Pre-allocated MIDI buffers
- Cached channel count
- Reusable mixing buffer

### Potential Issues
1. **Preset Switching**: Changing a preset on one channel affects others using the same preset
   - Could be addressed with more complex preset state management
   - Trade-off between memory usage and independence

2. **Resource Management**: Need careful handling of shared SF2 resource
   - Ensure proper cleanup
   - Thread-safe access to shared resource

3. **UI Considerations**: Need to manage multiple preset selectors
   - Could optimize to show only active channels
   - Consider UI layout for multiple channels

## Implementation Steps

1. Update `SynthAudioSource` class with shared resource management
2. Implement pre-allocated buffer system
3. Add per-channel synth instances
4. Update mixing implementation
5. Add channel preset management
6. Update UI to handle multiple channels

## Future Enhancements
- Per-channel volume control
- Independent preset state per channel
- Dynamic channel activation based on MIDI content
- More efficient preset switching mechanism
- Channel muting/soloing

## Performance Monitoring
Consider adding:
- CPU usage monitoring per channel
- Memory usage tracking
- Voice allocation monitoring
- Buffer underrun detection

This implementation provides a foundation for multi-channel MIDI playback while maintaining real-time performance and efficient resource usage.

## Full Conversation

Yes, we can modify the system to support multiple instruments across MIDI channels. Here's how we can adapt the code:

1. First, let's modify the SynthAudioSource to maintain multiple SF2 sounds and track which channels are in use:

```cpp:Source/SynthAudioSource.h
// ... existing code ...
private:
    // Replace single SF2 synth/sound with arrays
    std::array<std::unique_ptr<sfzero::Synth>, 16> channelSynths;  // One per MIDI channel
    std::array<std::unique_ptr<sfzero::SF2Sound>, 16> channelSounds;
    std::bitset<16> activeChannels;  // Track which channels have data

    // ... rest of existing code ...
```

2. Then update the SynthAudioSource implementation:

```cpp:Source/SynthAudioSource.cpp
// ... existing code ...

SynthAudioSource::SynthAudioSource() {
    // Create the base SF2 sound from binary data
    auto tempFile = juce::File::createTempFile(".sf2");
    tempFile.replaceWithData(BinaryData::Korg_Triton_Piano_sf2,
                            BinaryData::Korg_Triton_Piano_sf2Size);

    // Initialize synths for all channels
    for (int channel = 0; channel < 16; ++channel) {
        channelSynths[channel] = std::make_unique<sfzero::Synth>();
        
        // Add voices to each synth
        for (int i = 0; i < 8; ++i) {
            channelSynths[channel]->addVoice(new sfzero::Voice());
        }
        
        // Create a new SF2Sound for this channel
        channelSounds[channel] = std::make_unique<sfzero::SF2Sound>(tempFile);
        channelSounds[channel]->loadRegions();
        channelSounds[channel]->loadSamples(nullptr);
        
        // Add the sound to the synth
        channelSynths[channel]->clearSounds();
        channelSynths[channel]->addSound(channelSounds[channel].get());
    }
    
    tempFile.deleteFile();
}

void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    currentSampleRate = sampleRate;
    for (auto& synth : channelSynths) {
        synth->setCurrentPlaybackSampleRate(sampleRate);
    }
}

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Create temporary buffers for each active channel
    juce::AudioBuffer<float> tempBuffer(outputBuffer.getNumChannels(), numSamples);
    std::array<juce::MidiBuffer, 16> channelBuffers;

    // Sort MIDI events by channel
    for (const auto metadata : midiBuffer) {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel() - 1; // MIDI channels are 1-based
        if (channel >= 0 && channel < 16) {
            channelBuffers[channel].addEvent(msg, metadata.samplePosition);
            activeChannels.set(channel);
        }
    }

    // Process each active channel
    outputBuffer.clear(startSample, numSamples);
    
    for (int channel = 0; channel < 16; ++channel) {
        if (activeChannels[channel]) {
            tempBuffer.clear();
            channelSynths[channel]->renderNextBlock(tempBuffer, channelBuffers[channel], 
                                                  0, numSamples);
            
            // Add this channel's output to the main buffer
            for (int i = 0; i < outputBuffer.getNumChannels(); ++i) {
                outputBuffer.addFrom(i, startSample, tempBuffer, i, 0, numSamples);
            }
        }
    }
}

// Add method to set different sounds for different channels
void SynthAudioSource::setChannelSound(int channel, int soundIndex) {
    if (channel >= 0 && channel < 16 && channelSounds[channel]) {
        channelSounds[channel]->useSubsound(soundIndex);
    }
}

// Update the getter to return the sound for a specific channel
sfzero::SF2Sound* SynthAudioSource::getSF2Sound(int channel) const {
    if (channel >= 0 && channel < 16) {
        return channelSounds[channel].get();
    }
    return nullptr;
}
```

3. Finally, update the MainComponent to handle multiple channel presets:

```cpp:Source/MainComponent.cpp
// ... in MainComponent constructor ...

if (synthAudioSource != nullptr) {
    // Create a combo box for each active MIDI channel
    for (int channel = 0; channel < 16; ++channel) {
        if (auto* sound = synthAudioSource->getSF2Sound(channel)) {
            auto* channelPresetBox = new juce::ComboBox();
            channelPresetBoxes.add(channelPresetBox);
            addAndMakeVisible(channelPresetBox);
            
            channelPresetBox->setTextWhenNothingSelected("Ch " + juce::String(channel + 1) + " Preset");
            
            for (int i = 0; i < sound->numSubsounds(); ++i) {
                channelPresetBox->addItem(sound->subsoundName(i), i + 1);
            }
            
            // Set initial preset
            if (channelPresetBox->getNumItems() > 0) {
                channelPresetBox->setSelectedId(1, juce::dontSendNotification);
                sound->useSubsound(0);
            }
            
            // Add onChange callback
            channelPresetBox->onChange = [this, sound, channel]() {
                auto* box = channelPresetBoxes[channel];
                if (box->getSelectedId() > 0) {
                    synthAudioSource->getSF2Synth(channel)->allNotesOff(0, true);
                    synthAudioSource->setChannelSound(channel, box->getSelectedId() - 1);
                }
            };
        }
    }
}
```

You'll also need to add to the MainComponent header:

```cpp:Source/MainComponent.h
private:
    // ... existing members ...
    juce::OwnedArray<juce::ComboBox> channelPresetBoxes;
```

This implementation:
1. Creates separate synth and sound instances for each MIDI channel
2. Routes MIDI messages to the appropriate channel's synth
3. Mixes the output from all active channels
4. Allows different instrument presets to be selected for each channel

You'll need to update the resized() method to layout the new preset boxes appropriately. The exact layout will depend on your UI design preferences.

Remember that this implementation will use more memory since it creates 16 copies of the SF2 sound. If memory usage is a concern, you could optimize by only creating synths/sounds for channels that actually contain MIDI data, or by sharing the SF2 sound data across channels while maintaining separate synth instances.


---

Here's how we can modify the code to share SF2 sound data while maintaining separate synth instances. The key is to have a single SF2Sound instance that's shared across multiple synths:

```cpp:Source/SynthAudioSource.h
#pragma once

#include "../Modules/SFZero/SFZero.h"
#include <JuceHeader.h>

class SynthAudioSource : public juce::AudioSource {
public:
    SynthAudioSource();
    ~SynthAudioSource() override;

    // ... existing methods ...

    // Modified to get synth for specific channel
    sfzero::Synth* getSF2Synth(int channel) {
        if (channel >= 0 && channel < 16) {
            return channelSynths[channel].get();
        }
        return nullptr;
    }

    // Get the shared SF2 sound
    sfzero::SF2Sound* getSF2Sound() const { return sharedSF2Sound.get(); }

    // Set preset for specific channel
    void setChannelPreset(int channel, int presetIndex);

private:
    // Single shared SF2 sound instance
    std::unique_ptr<sfzero::SF2Sound> sharedSF2Sound;
    
    // Separate synth instances per channel
    std::array<std::unique_ptr<sfzero::Synth>, 16> channelSynths;
    
    // Track which channels are active
    std::bitset<16> activeChannels;

    // ... rest of existing members ...
};
```

```cpp:Source/SynthAudioSource.cpp
SynthAudioSource::SynthAudioSource() {
    // Create single shared SF2 sound from binary data
    auto tempFile = juce::File::createTempFile(".sf2");
    tempFile.replaceWithData(BinaryData::Korg_Triton_Piano_sf2,
                            BinaryData::Korg_Triton_Piano_sf2Size);

    // Initialize the shared sound
    sharedSF2Sound = std::make_unique<sfzero::SF2Sound>(tempFile);
    sharedSF2Sound->loadRegions();
    sharedSF2Sound->loadSamples(nullptr);

    // Initialize synths for all channels
    for (int channel = 0; channel < 16; ++channel) {
        channelSynths[channel] = std::make_unique<sfzero::Synth>();
        
        // Add voices to each synth
        for (int i = 0; i < 8; ++i) {
            channelSynths[channel]->addVoice(new sfzero::Voice());
        }
        
        // Add the shared sound to each synth
        channelSynths[channel]->clearSounds();
        channelSynths[channel]->addSound(sharedSF2Sound.get());
    }
    
    tempFile.deleteFile();
}

void SynthAudioSource::setChannelPreset(int channel, int presetIndex) {
    if (channel >= 0 && channel < 16) {
        // Stop any playing notes on this channel
        if (auto* synth = channelSynths[channel].get()) {
            synth->allNotesOff(0, true);
        }
        
        // Create a new sound instance for this channel using the shared sound's data
        if (sharedSF2Sound) {
            sharedSF2Sound->useSubsound(presetIndex);
        }
    }
}

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Create a temporary buffer for mixing
    juce::AudioBuffer<float> tempBuffer(outputBuffer.getNumChannels(), numSamples);
    std::array<juce::MidiBuffer, 16> channelBuffers;

    // Sort MIDI events by channel
    for (const auto metadata : midiBuffer) {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel() - 1; // MIDI channels are 1-based
        if (channel >= 0 && channel < 16) {
            channelBuffers[channel].addEvent(msg, metadata.samplePosition);
            activeChannels.set(channel);
        }
    }

    // Clear the output buffer
    outputBuffer.clear(startSample, numSamples);
    
    // Process each active channel
    for (int channel = 0; channel < 16; ++channel) {
        if (activeChannels[channel]) {
            tempBuffer.clear();
            
            // Render this channel's synth
            channelSynths[channel]->renderNextBlock(tempBuffer, 
                                                  channelBuffers[channel], 
                                                  0, numSamples);
            
            // Mix into the main output buffer
            for (int i = 0; i < outputBuffer.getNumChannels(); ++i) {
                outputBuffer.addFrom(i, startSample, tempBuffer, i, 0, numSamples);
            }
        }
    }
}
```

Then update the MainComponent to use the shared sound:

```cpp:Source/MainComponent.cpp
// In MainComponent constructor
if (synthAudioSource != nullptr) {
    if (auto* sound = synthAudioSource->getSF2Sound()) {
        // Create preset boxes for each MIDI channel
        for (int channel = 0; channel < 16; ++channel) {
            auto* channelPresetBox = new juce::ComboBox();
            channelPresetBoxes.add(channelPresetBox);
            addAndMakeVisible(channelPresetBox);
            
            channelPresetBox->setTextWhenNothingSelected("Ch " + juce::String(channel + 1) + " Preset");
            
            // Add all available presets to each channel's combo box
            for (int i = 0; i < sound->numSubsounds(); ++i) {
                channelPresetBox->addItem(sound->subsoundName(i), i + 1);
            }
            
            // Set initial preset
            if (channelPresetBox->getNumItems() > 0) {
                channelPresetBox->setSelectedId(1, juce::dontSendNotification);
                synthAudioSource->setChannelPreset(channel, 0);
            }
            
            // Add onChange callback
            channelPresetBox->onChange = [this, channel]() {
                auto* box = channelPresetBoxes[channel];
                if (box->getSelectedId() > 0) {
                    synthAudioSource->setChannelPreset(channel, box->getSelectedId() - 1);
                }
            };
        }
    }
}
```

The main benefits of this approach are:

1. Memory efficiency: The SF2 sample data is loaded only once and shared across all synth instances
2. Each MIDI channel still gets its own synth instance, allowing for independent voice allocation and processing
3. The preset selection mechanism remains the same from the user's perspective

The main tradeoff is that changing a preset on one channel might briefly interrupt sound on other channels using the same preset, as the shared sound's subsound is changed. If this is a concern, you could implement a more sophisticated system that maintains separate subsound states for each channel, but that would add complexity to the code.

You might also want to optimize the UI to only show preset boxes for channels that actually contain MIDI data, which you can determine by analyzing the MIDI file when it's loaded.

---

Let's trace through the audio mixing chain from top to bottom:

1. At the highest level, in `MainComponent`, we have an `AudioMixerSource` that can mix multiple audio sources:

```cpp:Source/MainComponent.cpp
MainComponent::MainComponent() {
    // Create a MixerAudioSource
    audioMixerSource = std::make_unique<juce::MixerAudioSource>();

    // Create our SynthAudioSource
    synthAudioSource = std::make_unique<SynthAudioSource>();

    // Create the MidiSchedulerAudioSource, passing the synth
    midiSchedulerAudioSource = std::make_unique<MidiSchedulerAudioSource>(synthAudioSource.get());

    // Add the scheduler to the mixer
    audioMixerSource->addInputSource(midiSchedulerAudioSource.get(), false);

    // Set the mixer as the source for the AudioSourcePlayer
    audioSourcePlayer.setSource(audioMixerSource.get());
}
```

2. The `MidiSchedulerAudioSource` processes MIDI events and delegates actual audio rendering to the `SynthAudioSource`:

```cpp:Source/MidiSchedulerAudioSource.cpp
void MidiSchedulerAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    // ... MIDI event processing ...

    // Render audio through the synth
    synth->renderNextBlock(*bufferToFill.buffer, midiBuffer,
                          bufferToFill.startSample, numSamples);
}
```

3. Finally, in `SynthAudioSource`, we mix the audio from different MIDI channels:

```cpp:Source/SynthAudioSource.cpp
void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Create temporary buffer for mixing
    juce::AudioBuffer<float> tempBuffer(outputBuffer.getNumChannels(), numSamples);
    std::array<juce::MidiBuffer, 16> channelBuffers;

    // Sort MIDI events by channel
    for (const auto metadata : midiBuffer) {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel() - 1;
        if (channel >= 0 && channel < 16) {
            channelBuffers[channel].addEvent(msg, metadata.samplePosition);
            activeChannels.set(channel);
        }
    }

    // Clear the output buffer
    outputBuffer.clear(startSample, numSamples);
    
    // Mix each active channel
    for (int channel = 0; channel < 16; ++channel) {
        if (activeChannels[channel]) {
            tempBuffer.clear();
            
            // Render this channel's synth
            channelSynths[channel]->renderNextBlock(tempBuffer, 
                                                  channelBuffers[channel], 
                                                  0, numSamples);
            
            // Mix into the main output buffer
            for (int i = 0; i < outputBuffer.getNumChannels(); ++i) {
                outputBuffer.addFrom(i, startSample, tempBuffer, i, 0, numSamples);
            }
        }
    }
}
```

So the mixing hierarchy is:

```
AudioDeviceManager
    └── AudioSourcePlayer
        └── MixerAudioSource
            └── MidiSchedulerAudioSource
                └── SynthAudioSource
                    └── Multiple SF2 Synths (one per MIDI channel)
```

The actual mixing happens in two places:

1. Inside `SynthAudioSource::renderNextBlock`, where the output from each MIDI channel's synth is mixed together using `outputBuffer.addFrom()`

2. Inside `juce::MixerAudioSource`, which could mix multiple audio sources if we added them (though currently we only have one - the MidiSchedulerAudioSource)

The `addFrom` method is doing a simple additive mix - it adds the samples from the source buffer to the destination buffer. If you wanted to add volume control per channel, you could multiply the samples by a gain factor during this mixing process, like this:

```cpp
// Example of mixing with volume control
float channelGain = 0.8f;  // 80% volume for this channel
for (int i = 0; i < outputBuffer.getNumChannels(); ++i) {
    outputBuffer.addFrom(i, startSample, 
                        tempBuffer, i, 0, 
                        numSamples, 
                        channelGain);
}
```

This would be a good place to add per-channel volume controls or other effects in the future.

---



Yes, good catch! Creating temporary buffers and arrays in the audio thread is risky as it could cause allocations. Here's how we can optimize it:

```cpp:Source/SynthAudioSource.h
class SynthAudioSource : public juce::AudioSource {
public:
    // ... other members ...

private:
    // Pre-allocated buffer for mixing
    juce::AudioBuffer<float> mixingBuffer;
    std::array<juce::MidiBuffer, 16> channelMidiBuffers;
    
    // Cache the number of channels we're configured for
    int numChannels = 2;  // Default to stereo
    
    // ... other members ...
};
```

```cpp:Source/SynthAudioSource.cpp
void SynthAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    currentSampleRate = sampleRate;
    
    // Pre-allocate mixing buffer
    mixingBuffer.setSize(2, samplesPerBlockExpected, false, false, true);
    
    // Clear all MIDI buffers
    for (auto& buffer : channelMidiBuffers) {
        buffer.clear();
    }
    
    // Prepare all synths
    for (auto& synth : channelSynths) {
        synth->setCurrentPlaybackSampleRate(sampleRate);
    }
    
    numChannels = 2;  // Store channel count for later use
}

void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Clear all MIDI buffers (reuse existing buffers)
    for (auto& buffer : channelMidiBuffers) {
        buffer.clear();
    }

    // Sort MIDI events by channel (reusing existing buffers)
    for (const auto metadata : midiBuffer) {
        auto msg = metadata.getMessage();
        int channel = msg.getChannel() - 1;
        if (channel >= 0 && channel < 16) {
            channelMidiBuffers[channel].addEvent(msg, metadata.samplePosition);
            activeChannels.set(channel);
        }
    }

    // Clear the output buffer
    outputBuffer.clear(startSample, numSamples);
    
    // Process each active channel
    for (int channel = 0; channel < 16; ++channel) {
        if (activeChannels[channel]) {
            // Clear mixing buffer (avoiding reallocation)
            mixingBuffer.clear();
            
            // Render this channel's synth
            channelSynths[channel]->renderNextBlock(mixingBuffer, 
                                                  channelMidiBuffers[channel], 
                                                  0, numSamples);
            
            // Mix into the main output buffer
            for (int i = 0; i < numChannels; ++i) {
                outputBuffer.addFrom(i, startSample, 
                                   mixingBuffer, i, 0, 
                                   numSamples);
            }
        }
    }
}

void SynthAudioSource::releaseResources() {
    // Free the mixing buffer when not needed
    mixingBuffer.setSize(0, 0);
    
    // Clear all MIDI buffers
    for (auto& buffer : channelMidiBuffers) {
        buffer.clear();
    }
}
```

Key improvements:
1. Pre-allocate the mixing buffer in `prepareToPlay`
2. Reuse MIDI buffers instead of creating new ones
3. Cache the number of channels to avoid calling `getNumChannels()` in the audio thread
4. Clear and reuse existing buffers instead of creating new ones
5. Properly clean up resources in `releaseResources`

This version should be much safer for real-time audio processing as it avoids any memory allocation in the audio thread. All buffers are pre-allocated and reused.

You might also want to add some safety checks:

```cpp:Source/SynthAudioSource.cpp
void SynthAudioSource::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                     const juce::MidiBuffer& midiBuffer,
                                     int startSample, int numSamples) {
    // Safety checks
    if (mixingBuffer.getNumSamples() < numSamples || 
        mixingBuffer.getNumChannels() < numChannels) {
        // Buffer too small - this shouldn't happen if prepareToPlay was called correctly
        outputBuffer.clear(startSample, numSamples);
        return;
    }

    // ... rest of the implementation ...
}
```

This ensures that if something goes wrong with the buffer allocation, we fail safely rather than causing undefined behavior.
