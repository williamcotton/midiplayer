# MidiPlayer

A modern, feature-rich MIDI file player built with JUCE framework. This application provides an interface for playing and visualizing MIDI.

<img src="https://github.com/user-attachments/assets/c1abc783-717e-4195-beb0-29d591039fe0](https://github.com/user-attachments/assets/d09ec69c-354a-4a23-8656-36f7198df610" width="200">

## Features

- **Interactive Piano Roll Display**: Visual representation of MIDI notes with color-coded channels
- **High-Quality Sound Synthesis**: Uses General MIDI (GM) SF2 soundfonts for authentic 90s-style playback
- **Multi-Channel Support**: Full 16-channel MIDI support with proper channel routing
- **Real-time Controls**:
  - Play/Pause/Return to Start
  - Tempo adjustment
  - Note transposition (-12 to +12 semitones)
  - Instrument preset selection
  - Loop region setting with customizable repeat count

## Technical Details

- Built with JUCE Framework
- Uses SFZero module for SF2 soundfont playback
- Default General MIDI (GM) soundfont included
- Supports standard MIDI files (.mid, .midi)
- Cross-platform compatibility (macOS, iOS, Android)

## Controls

- **Space Bar**: Play/Pause toggle
- **Load MIDI File**: Opens file browser to select a MIDI file
- **Transport Controls**: 
  - Play/Pause (▶/⏸)
  - Return to Start (⏮)
- **Loop Controls**: Set and clear loop regions with customizable iteration count
- **Tempo Slider**: Adjust playback speed (30-300 BPM)
- **Transposition**: Shift notes up or down by up to 12 semitones
- **Preset Selection**: Choose from available GM instruments

## Piano Roll Features

- Color-coded notes by channel and pitch
- Auto-scrolling during playback
- Manual scroll with automatic playback following
- Visual loop region display
- Piano keyboard reference on the left edge
- Beat/bar grid with time signature support

## Platform Support

- macOS (Native)
- iOS (Touch-optimized interface)
- Android

## Building

This project requires:
- JUCE Framework
- SFZero Module (included)
- C++ development environment
- Platform-specific SDK (Xcode for macOS/iOS, Android Studio for Android)

Use the provided `.jucer` file to generate project files for your platform.

## License

MIT License
