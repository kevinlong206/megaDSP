# megaDSP

[![Build and test](https://github.com/kevinlong206/megaDSP/actions/workflows/build.yml/badge.svg)](https://github.com/kevinlong206/megaDSP/actions/workflows/build.yml)

megaDSP aims to be the only effects plugin you need in your DAW: one VST3, AU,
or CLAP plugin with a growing collection of high-quality, intuitive effects.
Use a module on its own or combine up to eight in a serial chain to shape,
transform, and finish your audio without juggling separate plugins.

Eight complete **Snapshots** let one instance hold multiple racks. Click a
snapshot to recall it; clicking an unused snapshot activates a fresh blank rack
without copying the current one. Shift-click saves the current rack to an empty
snapshot or overwrites a populated one. Drag a populated snapshot onto another
slot to copy it without changing the source. MIDI
Program Change 0-7 recalls Snapshots 1-8 at the event sample. The selected
snapshot updates live as you edit, and up to two outgoing effect tails spill
into the new rack. Recall uses a zero-slope 20 ms transition. Snapshot spills
are capped at eight seconds and fade over their final 500 ms; sustained
generators such as Chord Loom and frozen spectral fields use a shorter
1.5-second spill so they do not linger indefinitely.

Enable **LIVE SAFE** for high-stakes monitoring or performance. megaDSP then
runs every populated snapshot continuously on silence, converting the brief CPU
spike at recall into predictable steady CPU usage. The active and transitioning
racks alone receive live input. Host latency updates wait until audio callbacks
stop, avoiding PDC rebuilds during monitoring without adding plugin latency;
timing may be temporarily offset after recalling a rack with different latency.

Creative spectral effects keep the live dry signal immediate: **Spectral
Prism**, **Pitch Bloom**, **Spectral Delay Canvas**, **Harmonic Mirage**,
**Time Mosaic**, and **Spectral Nebula** do not add delay compensation in the
DAW. Their transformed wet sound arrives after the analysis window as a natural
pre-delay. Phase-critical correction, lookahead, and nonlinear processors still
report and align the latency required for accurate processing.

Today, megaDSP includes forty-seven purpose-built processors with interactive
graphs, realtime analysis, and controls that match the musical quantities they
edit. The long-term vision is a community-built library spanning dozens—and
eventually perhaps hundreds—of useful effects in one consistent rack.

![Eight processor tabs in signal-flow order with the header output meter](docs/images/rack-overview.png)

## Built in the open

megaDSP is intended to grow with its community. Users around the world are
invited to [report bugs, request features, and share ideas](https://github.com/kevinlong206/megaDSP/issues).
Code contributions are welcome; see the [contributing guide](CONTRIBUTING.md)
for build instructions, architecture notes, and the pull request process.

The plugin is built with [JUCE 8](https://juce.com) and C++20. Development is
assisted by [GitHub Copilot](https://github.com/features/copilot), alongside
automated tests and review.

## Supported platforms and formats

| Platform | VST3 | AU | CLAP |
|---|:---:|:---:|:---:|
| macOS 12+ (universal arm64 + x86_64) | ✓ | ✓ | ✓ |
| Windows 10+ (x64) | ✓ | — | ✓ |
| Linux x64 | ✓ | — | ✓ |

## Installation

Download the latest release archive from the [Releases](https://github.com/kevinlong206/megaDSP/releases) page.
Release binaries are **unsigned**; macOS will prompt you to allow them the first time.

<details>
<summary>macOS</summary>

Extract `megaDSP-<version>-macOS-universal.tar.gz` and copy the bundles:

```
megaDSP.vst3      → ~/Library/Audio/Plug-Ins/VST3
megaDSP.component → ~/Library/Audio/Plug-Ins/Components
megaDSP.clap      → ~/Library/Audio/Plug-Ins/CLAP
```

After copying, right-click each bundle in Finder and choose **Open**, or remove the quarantine attribute:

```sh
xattr -r -d com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/megaDSP.vst3
```

</details>

<details>
<summary>Windows</summary>

Extract `megaDSP-<version>-Windows-x64.zip` and copy the bundles:

```
megaDSP.vst3 → %LOCALAPPDATA%\Programs\Common\VST3
megaDSP.clap → %LOCALAPPDATA%\Programs\Common\CLAP
```

</details>

<details>
<summary>Linux</summary>

Extract `megaDSP-<version>-Linux-x64.tar.gz` and copy the bundles:

```
megaDSP.vst3 → ~/.vst3
megaDSP.clap → ~/.clap
```

</details>

## First-rack workflow

1. Load megaDSP on a track in your DAW.
2. Click the **+** tab and choose a module from the standard category submenus. **Search Modules…** at the top opens a separate searchable list; **↑↓** and **Enter** work there.
3. Select a tab to see its interactive graph and controls.
4. Drag tabs left or right to reorder the signal chain.
5. **Double-click** a tab (or press **Space / Return / B**) to bypass it — the tab is greyed and labeled **BYPASSED**. After its short click-safe fade, the module DSP and live visualization are suspended; only latency-preserving dry routing remains.
6. Click the **×** icon on a tab to remove it.
7. Use the header **Output Trim** and peak meter to manage the final output level. The meter shows peak hold, near-clip colour zones, and a resettable **CLIP** indicator.
8. Use **Snapshots 1-8** above the module tabs to keep alternate complete racks. Click recalls without copying the current rack; an unused slot opens as a fresh blank rack. Shift-click saves to an empty slot or overwrites a populated one. Drag one populated snapshot onto another slot to copy it. Program Change 0-7 recalls populated snapshots over MIDI.

Snapshot recall never waits for modules or external files to load: populated
snapshots remain prepared while inactive. Each snapshot keeps its native DSP
latency, so recalling racks with different latency can move timing while the
amplitude transition remains click-safe. Missing IR or NAM files are shown as
snapshot errors without preventing the rest of the rack from loading.

For live use, enable **LIVE SAFE** before the performance and confirm the
project's steady CPU load with all intended snapshots populated. This mode feeds
silence through inactive snapshots to keep their processing paths hot, while
normal mode leaves them idle to conserve CPU.

![Module browser with categorized search results and keyboard navigation](docs/images/module-browser.png)

## Diagnostics and bug reports

Click **DIAG ON** in the plugin header to disable global diagnostics or open the
diagnostics folder. Diagnostics are enabled by default and remain disabled if
you turn them off. While enabled, megaDSP keeps
bounded rotating local logs of plugin lifecycle, audio configuration, topology,
state rejection, latency, and file-loading errors. It also installs fatal crash
and uncaught-exception handlers in the DAW process and writes a minimal crash
marker before allowing the existing host/OS crash handling to continue.

Logs stay on your computer and never contain audio. Before attaching them to a
[GitHub issue](https://github.com/kevinlong206/megaDSP/issues), review
`megaDSP.log` and `megaDSP-crash.txt`, then include the DAW or operating-system
crash report when one exists. Logs rotate at 1 MB and retain three older files.

## Effects

### Dynamics

| Module | What it does |
|---|---|
| **Compressor** | Draggable threshold with 10-second input/output history, real-time gain-reduction overlay, primary Auto Makeup, and secondary Manual Trim |
| **Limiter** | Knob-free graph with separate Threshold and Ceiling handles, 10-second level traces, full GR overlay, integrated Release/Lookahead tracks, and Auto Gain |
| **Dynamic EQ / De-Esser** | Single-band stackable processor — drag frequency/range/threshold in the graph, mouse-wheel Q, 10-second gain history, external sidechain with fallback, and Listen |
| **Gate / Expander** | Hysteresis, detector passband, sidechain audition, and DSP-synchronized detector/envelope/attenuation/open telemetry |
| **Transient Designer** | Level-independent Attack and Sustain shaping with DSP fast/slow and shaping-component telemetry |
| **Volume Swell** | Automatically ducks each detected chord attack, then rises along an editable curve like a guitarist's volume pedal or knob |
| **Multiband Compressor** | Three reconstructing bands with direct editing and actual per-band reduction/state telemetry |
| **Loudness Rider** | Long-term gated LUFS riding with bounded gain, lookahead, and crest preservation |
| **Adaptive Clipper** | Transient-aware oversampled clipping with true-peak ceiling control and auto trim |
| **Stage Vocal** | Zero-latency live vocal strip with rumble filtering, bounded expansion, frequency-selective de-essing, linked compression, Presence, Air, and Output trim |

### EQ & Filtering

| Module | What it does |
|---|---|
| **EQ** | Pro-Q-inspired response editor: double-click to add or remove bands, drag to shape, wheel Q, select Bell/Shelf/Cut directly, and compare pre/post spectrum traces |
| **Auto Wah** | Pick-responsive Low Pass, Band Pass, Peak, or Classic Wah filter with direct center/range editing and a visible sensitivity threshold |
| **Resonance Tamer** | Adaptive narrow-resonance suppression with transient protection and live detected-reduction traces |
| **Spectral Balance** | Long-term tonal balancing toward Natural, Warm, Clear, Vocal, or Flat target contours |

### Saturation

| Module | What it does |
|---|---|
| **Saturator** | Transfer curve with input/output waveforms; level-compensated auto-gain keeps density and harmonics audible without loudness jumps |
| **Signal Decay** | Measured degradation waveforms plus DSP-synchronized clock, stereo-wear, and dropout state |
| **Analog Tape** | Four cassette/reel machines with level-compensated oversampled saturation, transient softening, bias, head bump, and optional wow/flutter, wear, and hiss |
| **Harmonic Mirage** | Tracked partial resynthesis for harmonic, subharmonic, hollow, and metallic color |
| **Guitar Cabinet** | Fully procedural speaker, enclosure, microphone, and room modeling with dynamic cone breakup, power compression, and direct mic placement |
| **Neural Amp Modeler** | Loads standard, A2, and SlimmableContainer `.nam` models with Pre Gain, a familiar Bass/Mid/Treble guitar tone stack, and Post Gain |

### Time-Based

| Module | What it does |
|---|---|
| **Delay** | Editable tap timeline plus a 2D Rate/Depth Movement field; Sync toggles between free Time and synced Division |
| **Diffusion Delay** | Tempo-aware echoes with DSP-synchronized primary/cloud activity, energy, progress, and stereo placement |
| **Algorithmic Reverb** | 16-line Hall, Chamber, and Plate network with input high-pass/low-pass filtering, smooth wet ducking, Decay, Room Scale, and independent Dry/Wet |
| **Convolution Reverb** | WAV/AIFF/FLAC IR loading, waveform preview, draggable wet high-pass/low-pass filtering, smooth ducking, and independent Dry/Wet rails |
| **Resonant Matrix** | Tuned resonators routed through evolving signed feedback patterns for pitched, metallic spaces |
| **Pitch Bloom** | Musical interval shifting with actual repeat-event arcs for interval, progress, energy, pan, and spread |
| **Spectral Delay Canvas** | Frequency-dependent low, mid, and high spectral delay with diffusion, feedback, and freeze |
| **Chord Loom** | Automatically captures each guitar strum and sustains it as a continuously woven pad, smoothly replacing the previous held chord |
| **Spectral Nebula** | Continuously absorbs guitar or vocals into a drifting, shimmering spectral cloud with tonal/noise focus and freeze |

### Modulation

| Module | What it does |
|---|---|
| **Tremolo** | Amplitude, Harmonic, and Vibrato algorithms; Rate syncs to host divisions; Vibrato shows depth in cents |
| **Vintage Chorus** | Four distinct topologies (Vintage BBD, Dimension, Tri-Chorus, String Ensemble), animated voice-trajectory field, and six-tap Density |
| **Rotary Speaker** | Dual-rotor Doppler model with Brake/Chorale/Tremolo speeds, virtual microphone distance and spread, Cabinet Color, and Drive |
| **Studio Phaser** | 2–12 stage all-pass sweeps with DSP-synchronized notch phases and topology crossfades |
| **Studio Flanger** | Tape, Through-Zero, Jet, and BBD models with DSP-synchronized comb and model transitions |
| **Chaos Field** | Deterministic attractor-driven filter, delay, pan, and stereo motion |
| **Midnight Current** | Warm analog drive, a dark resonant filter, and deep slow phasing for submerged psychedelic movement |

### Glitch & Creative

| Module | What it does |
|---|---|
| **Random Granulizer** | 6-second circular buffer, 16-voice pool, live grain-stream timeline, dual-handle MIN/MAX size window, and pitch-stable grains |
| **Beat Permuter** | Rearranges recent tempo-locked slices into precise repeats, reverses, scatters, and rhythmic glitches |
| **Spectral Prism** | Bends, shifts, smears, and freezes sound around a visual spectral pivot |
| **Wavefold Garden** | Grows animated antialiased harmonics with envelope-responsive wavefolding |
| **Harmonic Resonator** | Tuned Major, Minor, suspended, seventh, fifth, and octave-drone resonator fields with register, decay, tone, motion, and stereo spread |
| **Formant Forge** | Direct vowel-field and formant sculpting across human, tube, creature, and metallic models |
| **Time Mosaic** | Reconstructs evolving frequency tiles from recent spectral history |
| **Resonant Sculpture** | Tracks the incoming fundamental and excites a morphing virtual object of strings, glass, metal, or membranes with bowed, struck, mutated, and chaotic modal energy |

### Stereo & Utility

| Module | What it does |
|---|---|
| **Stereo Width** | Frequency-dependent M/S scaling, mono-compatible all-pass Dimension field, Mono Below threshold, and dynamic correlation protection |
| **M/S Decoder** | Reconstructs L/R from encoded Mid/Side input; live vectorscope with Mid, Side, Left, and Right orientation |
| **Spatial Orbit** | DSP-reported Circle, Figure Eight, Pendulum, and deterministic Wander position/trails |
| **Phase Coherence** | Confidence-gated stereo timing, phase, and low-frequency coherence repair |

![Compressor graph with level history, gain reduction, threshold, and musical controls](docs/images/visual-processing.png)
![Algorithmic reverb early-reflection and decay editor in the Ambient Space instance](docs/images/reverb-modulation.png)

## Themes and instance identity

Type a clear label such as **Vocal**, **Guitar**, or **Mix Bus** in the
**INSTANCE** field. The name is saved with that plugin instance and preset, but
does not rename anything in the DAW. Empty names display **Untitled**.

The adjacent **COLOR** button opens sixteen named full-background themes.
The original ten themes retain their saved identities, with Ocean Navy,
Emerald, Burgundy, Indigo, Blue Charcoal, and Dark Olive added. Multiple
instances can use distinct names and colors in the same session.

## Documentation

- [Effects reference](docs/effects.md) — detailed per-effect control descriptions
- [Contributing](CONTRIBUTING.md) — prerequisites, build commands, architecture guide, and PR process
- [Releases](https://github.com/kevinlong206/megaDSP/releases) — download pre-built binaries for v0.23.1-beta.1

## License

JUCE is used under its [commercial or AGPLv3 licence](https://juce.com/juce-open-source-licence). See [CONTRIBUTING.md](CONTRIBUTING.md) for details.
