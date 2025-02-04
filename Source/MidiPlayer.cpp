/*
  ==============================================================================

    MidiPlayer.cpp
    Created: 4 Feb 2025 10:08:57am
    Author:  administrator

  ==============================================================================
*/

#include "MidiPlayer.h"

MidiPlayer::MidiPlayer()
{
    tempo = 120.0;
}

MidiPlayer::~MidiPlayer()
{
}

double MidiPlayer::convertTicksToBeats(double ticks) const
{
    return ticks / 480.0; // assuming standard MIDI PPQ
}

double MidiPlayer::convertBeatsToTicks(double beats) const
{
    return beats * 480.0;
}

double MidiPlayer::convertMillisecondsToBeats(double ms) const
{
    // Convert ms to beats based on tempo
    return (ms / 1000.0) * (tempo / 60.0);
}

double MidiPlayer::convertBeatsToMilliseconds(double beats) const
{
    // Convert beats to ms based on tempo
    return (beats * 60.0 / tempo) * 1000.0;
}
