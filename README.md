# MultiChainer

MultiChainer is a JUCE-based multiband sidechain audio plugin with a cross-platform **HTML/CSS/JS** user interface. It splits incoming audio into **three linear-phase frequency bands** (Low/Mid/High), applies **MIDI-triggered ducking** independently per band, then recombines the bands with phase-coherent summation.

The goal: **clean multiband sidechaining without the “EQ colored my audio” problem**.

## Features

- **3-band linear-phase crossover**
  - Two draggable crossovers define Low/Mid/High bands
  - Linear-phase (FIR) filtering to keep phase relationships intact and reduce coloration
- **MIDI-triggered ducking per band**
  - Per-band MIDI channel + note range triggering
  - Per-band depth (dB), delay (ms), curve controls, and smoothing
  - Sample-accurate scheduling from MIDI events
- **Web UI (HTML/CSS/JS)**
  - Embedded in the plugin using JUCE WebBrowserComponent
  - Spectrum view (FFT) + draggable crossover sliders
  - Per-band parameter panels

## Tech Overview

- **Framework:** JUCE 7+
- **Plugin Formats:** AU + VST3 (macOS)
- **Parameters/State:** AudioProcessorValueTreeState (APVTS)
- **DSP:**
  - FIR-based linear-phase crossover (coefficients swapped atomically)
  - Per-band envelope generator driven by MIDI note-on events
  - Gain smoothing to avoid clicks
- **UI:**
  - Local bundled assets (no external CDNs)
  - JSON message bridge (JS <-> C++)

## Build (macOS)

### Prerequisites
- Xcode (latest stable)
- CMake 3.24+ (or newer)
- JUCE (as a submodule or local checkout)

### Build steps (CMake)
```bash
git clone <this-repo>
cd MultiChainer

# If JUCE is a submodule:
git submodule update --init --recursive

mkdir -p build
cd build
cmake .. -G "Xcode" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release