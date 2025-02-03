#pragma once
#include <JuceHeader.h>

class AudioEngine : public juce::AudioSource
{
public:
    AudioEngine()
    {
        // Initialize synthesizer
        synth.addSound(new SineWaveSound());
        for (int i = 0; i < 16; ++i)
            synth.addVoice(new SineWaveVoice());
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        synth.setCurrentPlaybackSampleRate(sampleRate);
        midiMessageCollector.reset(sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();
        
        // Get incoming MIDI messages
        juce::MidiBuffer incomingMidi;
        midiMessageCollector.removeNextBlockOfMessages(incomingMidi, bufferToFill.numSamples);
        
        // Render audio
        synth.renderNextBlock(*bufferToFill.buffer, incomingMidi,
                            bufferToFill.startSample, bufferToFill.numSamples);
    }

    void releaseResources() override
    {
        synth.allNotesOff(0, true);
    }

    void noteOn(int channel, int noteNumber, float velocity)
    {
        auto messageOn = juce::MidiMessage::noteOn(channel, noteNumber, velocity);
        messageOn.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        midiMessageCollector.addMessageToQueue(messageOn);
    }

    void noteOff(int channel, int noteNumber, float velocity)
    {
        auto messageOff = juce::MidiMessage::noteOff(channel, noteNumber, velocity);
        messageOff.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        midiMessageCollector.addMessageToQueue(messageOff);
    }

    void allNotesOff()
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            auto message = juce::MidiMessage::allNotesOff(channel);
            message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
            midiMessageCollector.addMessageToQueue(message);
        }
    }

private:
    // Synthesizer voice
    class SineWaveSound : public juce::SynthesiserSound
    {
    public:
        SineWaveSound() {}
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    class SineWaveVoice : public juce::SynthesiserVoice
    {
    public:
        SineWaveVoice() {}

        bool canPlaySound(juce::SynthesiserSound* sound) override
        {
            return dynamic_cast<SineWaveSound*>(sound) != nullptr;
        }

        void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
        {
            currentAngle = 0.0;
            level = velocity * 0.15;
            tailOff = 0.0;

            auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
            auto cyclesPerSample = cyclesPerSecond / getSampleRate();
            angleDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
        }

        void stopNote(float velocity, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                if (tailOff == 0.0)
                    tailOff = 1.0;
            }
            else
            {
                clearCurrentNote();
                angleDelta = 0.0;
            }
        }

        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
        {
            if (angleDelta != 0.0)
            {
                if (tailOff > 0.0)
                {
                    while (--numSamples >= 0)
                    {
                        auto currentSample = (float)(std::sin(currentAngle) * level * tailOff);
                        
                        for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                            outputBuffer.addSample(i, startSample, currentSample);

                        currentAngle += angleDelta;
                        ++startSample;

                        tailOff *= 0.99;

                        if (tailOff <= 0.005)
                        {
                            clearCurrentNote();
                            angleDelta = 0.0;
                            break;
                        }
                    }
                }
                else
                {
                    while (--numSamples >= 0)
                    {
                        auto currentSample = (float)(std::sin(currentAngle) * level);

                        for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                            outputBuffer.addSample(i, startSample, currentSample);

                        currentAngle += angleDelta;
                        ++startSample;
                    }
                }
            }
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

    private:
        double currentAngle = 0.0, angleDelta = 0.0, level = 0.0, tailOff = 0.0;
    };

    juce::Synthesiser synth;
    juce::MidiMessageCollector midiMessageCollector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
