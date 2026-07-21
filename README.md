# megaDSP

[![Build and test](https://github.com/kevinlong206/megaDSP/actions/workflows/build.yml/badge.svg)](https://github.com/kevinlong206/megaDSP/actions/workflows/build.yml)

megaDSP is a modular 8-slot effects rack for VST3, AU, and CLAP hosts. It
includes parametric EQ, compressor, saturator, delay, limiter, and algorithmic
and convolution reverbs, stereo-width, M/S Decoder, premium tremolo, and
physical rotary-speaker modules, plus a stackable single-band Dynamic EQ /
De-Esser, Random Granulizer, and four-model Vintage Chorus, with mono/stereo
processing and an optional dynamics sidechain.

## Building

The project requires CMake 3.22 or newer and a C++20 compiler. JUCE and the
CLAP JUCE extensions are fetched automatically during configuration.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Build artifacts are written below `build/megaDSP_artefacts/Release`. GitHub
Actions builds and tests on macOS, Windows, and Linux. VST3 and CLAP are built
on all three platforms; AU is built on macOS because Audio Units are an
Apple-only plugin format.

The editor presents populated effects as tabs in signal-flow order. Drag tabs to
reorder processing, or use the trailing `+` tab to open the categorized module
browser. Its focused search matches module names, categories, descriptions, and
tags; use Up/Down and Enter or click a result to add it. Use a tab's X icon to
remove it and compact the remaining chain. Double-click a processor
tab, or focus it and press Space, Return, or B, to bypass it. Bypassed tabs are
greyed and explicitly labeled `BYPASSED`. Each selected module exposes a framed
Main group, contextually relevant controls, and an interactive visualization.
Hidden or unavailable controls retain their independent automation values and
are never rewritten:

- EQ: a large knob-free response editor with draggable bands, exact filter
  response, pre/post spectrum traces, latched HP/LP edge gestures, clickable
  topology pills, proximity-gated mouse-wheel Q, and an integrated Output rail
- Compressor: draggable threshold with 10-second input/output history, a
  real-time gain-reduction overlay, a 10-second maximum-reduction line, primary
  Auto Makeup recovery, and a secondary Manual Trim shown when relevant
- Saturator: transfer curve with input/output waveforms
- Delay: editable tap timeline plus a compact two-dimensional Rate/Depth
  Movement field; Sync exposes either Time or Division
- Stereo Width: direct Width and Mono Below handles with distinct Foundation,
  Dimension Field, and Mono Safe regions
- M/S Decoder: live Mid/Side-oriented stereo vectorscope
- Tremolo: clickable mode pills and a mode-aware depth editor; Vibrato uses
  cents, Harmonic alone exposes its crossover, and Sync swaps Rate for Division
- Rotary Speaker: dominant three-state Speed pills plus direct Rotor Balance,
  Motion, microphone distance/spread, and Spin-up gestures
- Limiter: a large knob-free ten-second display with draggable Threshold and
  Ceiling, input/output histories, full gain-reduction overlay, integrated
  Release/Lookahead tracks, and an Auto Gain status pill
- Dynamic EQ / De-Esser: a large knob-free frequency/range editor with
  mouse-wheel Q, exact live Bell/Low Shelf/High Shelf response, focused
  detector and threshold levels, ten-second gain history, and integrated
  compact timing/link tracks, detector, sidechain availability, and Listen
- Random Granulizer: a large knob-free grain-stream timeline with capture
  position, duration, direction, spread, filtering, playback progress, direct
  Capture Range/Stereo Spread/Rhythmic Delay Chance gestures, an integrated
  dual-handle Size Window, and compact secondary tracks
- Vintage Chorus: prominent model pills, an animated delay/stereo field showing
  every voice trajectory and phase relationship, direct Rate/Depth/Delay/Width
  editing, signed Regeneration polarity, grouped Density and Character rails,
  and compact Phase/Output rails
- Algorithmic Reverb: interactive early-reflection and stereo decay field with
  Hall, Chamber, and Plate modes, a direct Decay/Room Scale handle with its
  named and exact 25–200% readout, direct input-passband handles, and adjacent
  independent Dry/Wet level rails
- Convolution Reverb: WAV/AIFF/FLAC browsing and drag/drop, persistent IR
  selection, waveform preview, direct wet-passband handles, adjacent
  independent Dry/Wet rails, and separate Output Trim

Live analysis runs only for the selected tab. Graphs edit the same automatable
slot parameters as the contextual controls.

For the full-panel EQ, Limiter, Dynamic EQ, Random Granulizer, and Vintage
Chorus editors, focus the graph and use Left/Right to select a semantic
control. Up/Down edits it (Shift for fine changes), Space or Return activates
choices and switches, and Home restores that control's module default. Tab and
Shift+Tab leave the graph normally.

Controls match the value they edit: modes and tempo divisions use named
choices, Sync/Ping Pong/sidechain routing use switches, paired levels use rails,
and graph-native quantities use direct handles. Delay shows either free Time or
the active synced Division, never both; the inactive control is hidden,
unfocusable, and retains automation. Graph editing is limited to visible
handles, and double-clicking a control restores that module's musical default.
New modules also start from safe module-specific settings rather than setting
every control to 50%.

Saturator Drive is automatically level-compensated from the measured dry and
nonlinear signal energy, so harder drive adds density and harmonics without a
large loudness jump. Compensation only attenuates excess wet gain; Output
remains an independent final trim.

The EQ is controlled entirely in its enlarged graph. Drag bell points
horizontally for frequency and vertically for gain; drag HP/LP points vertically
for resonance. Dropping the low or high point into its labeled edge lane latches
HP or LP mode without trapping the cutoff there. The `LOW` and `HIGH` pills
switch topology directly, the mouse wheel adjusts Q only while the pointer is
near a band point, and the right-side Output rail controls final EQ trim.
Double-click a point or the Output rail to restore its musical default.

The limiter is also controlled entirely in its enlarged graph. Threshold has a
dedicated left handle and Ceiling a right handle, so both remain selectable when
set to the same value. Ten-second input/output traces and the complete
gain-reduction envelope show what limiting is doing over time, with current and
maximum reduction readouts. Release and Lookahead are wide horizontal tracks;
Auto Gain clearly reports `OFF` or `MATCHED`. Double-click any continuous
limiter control to restore its musical default.

Dynamic EQ / De-Esser is a focused, stackable single-band processor rather
than a crowded multiband effect. Drag its node horizontally from 20 Hz to
20 kHz and vertically from -18 dB cut to +12 dB boost; use the mouse wheel for
Q. A separate draggable Threshold line overlays the live focused-detector
level, while the exact active filter curve and ten-second gain history show the
result. Ratio, Attack, Release, and Stereo Link use compact tracks. Shape and
Peak/RMS detector modes, external sidechain, and Listen use clear status pills;
an unavailable sidechain is explicitly labeled and cannot be newly selected.
Negative Range performs conventional downward de-essing; positive Range
provides bounded dynamic boost. At 1:1 or 0 dB Range the audible EQ is flat
unless Listen is enabled. If external sidechain is selected without a connected
bus, detection explicitly falls back to the internal input.

Random Granulizer continuously records six seconds of recent mono or stereo
input and launches up to 16 click-free windowed grains without stealing active
voices. Size Minimum and Size Maximum each span 50 ms–2 seconds on a logarithmic
scale; every scheduled grain chooses a deterministic random duration within the
canonicalized window, so either handle can cross the other safely. The defaults
are 80 ms minimum and 280 ms maximum. Grain Rate spans 0.5–30 launches per
second. Capture Range reaches safely backward only through initialized history;
Reverse Chance is the only control that changes playback direction; and Stereo
Spread uses constant-power stereo placement with a mono-safe fold. Delay chooses useful
tempo-quantized pre-launch subdivisions from the host BPM, falling back to
120 BPM, without occupying grain voices while waiting.
Each grain receives a stable randomized low-pass cutoff below Brightness.
Bounded Regeneration returns wet energy to capture history, Mix is constant-power
with exact dry output at 0%, and Output is a post-mix -18 to +12 dB trim.

Its graphical page combines the two size parameters into one highlighted
dual-handle `MIN`/`MAX` window. Capture Range, Stereo Spread, and Rhythmic Delay
Chance are edited on the live timeline; Voices, Grain Rate, Reverse Chance,
Brightness, Regeneration, Mix, and Output use compact tracks rather than a wall
of knobs. Grain width and labels show duration, horizontal origin shows capture,
vertical placement shows spread, arrows show direction, colour shows
Brightness filtering, and a marker shows playback progress. Double-click the
size window, timeline gesture, or remaining track to
restore its musical default.

Vintage Chorus provides four genuinely separate modulation topologies. Vintage
BBD uses asymmetric modulation, soft compander/preamp saturation, pre/de-emphasis
colour, and Age-dependent bandwidth, drift, and signal-gated clock character.
Dimension uses low-depth complementary quadrature taps for stable stereo width
without a fake Haas path. Tri-Chorus uses three-phase studio modulation with a
controlled centre, while String Ensemble combines faster/deeper phase-offset
low/high modulation groups and darker filtering. Density fades one to six taps
in and out while retaining each model's topology.

All models use a preallocated stereo circular delay with cubic interpolation.
Rate, Depth, Delay, Width, bipolar Regeneration, Tone, Age, Stereo Phase, Mix,
and Output are smoothed; model changes crossfade parallel topology outputs and
Density changes fade individual taps. Regeneration is filtered and remains below
unity. Width scales only the wet mid/side field, so synthetic side cancels on
mono fold-down. Mix is constant-power with exact dry at 0%, and Age noise is
deterministic, low-level, and gated by input energy so silence remains silent.

Convolution Reverb loads mono or stereo WAV, AIFF, and FLAC impulse responses
from its `Load IR...` button or by dropping a file on the graph. The normalized
IR envelope and shaded passband remain visible while draggable Low Cut and High
Cut handles shape only the wet convolution signal. Dry and Wet are adjacent,
independent linear 0–100% amplitude rails, and Output Trim adjusts their sum.
With no available IR, Wet contributes
no signal and the result is `Dry * input * Output`. The absolute IR path is stored
with plugin and preset state and follows rack reordering; if the file moves, the
graph identifies the missing filename so it can be replaced. True-stereo IRs
intentionally retain their captured left/right acoustic differences.

The fixed header provides retained input and final-output peak meters with a
dB scale, peak hold, near-clip color zones, and a resettable CLIP indicator.
Output Trim remains beside the meter and is always the last gain stage in the
rack. Processor tabs use dedicated X icons for direct removal.

The header also offers ten persistent dark background themes: Midnight Blue,
Crimson Red, Espresso Brown, Forest Green, Royal Purple, Deep Teal, Slate,
Aubergine, Burnished Copper, and Graphite. Themes tint the main background,
header, tabs, processor panel, and visualization surfaces while retaining
high-contrast controls.

Compressor Auto Makeup slowly restores average level lost to gain reduction
without replacing the secondary Manual Trim. Limiter Auto Gain removes the
static loudness advantage created by lowering Threshold while retaining Ceiling as a
safety maximum. Both are automatable and enabled for newly added modules.
The compressor Threshold control is itself a live input-level meter: signal
fills the control track behind the movable threshold marker.

The algorithmic reverb uses a 16-line energy-preserving feedback network with
delay-proportional low, mid, and high decay, clustered early reflections, sparse
diffusion, decorrelated motion, and click-safe Room Scale and Mode transitions.
Decay sets the mid-band T60, while Damping shortens or extends the high-band tail.
Room Scale reads Compact, Natural, Large, or Vast while retaining the exact
25–200% value. Low Cut and High Cut control the signal entering the reverb, and
Width keeps low frequencies more coherent while widening the upper wet field. Dry and Wet are
independent smoothed linear 0–100% amplitude gains; both may be fully off or on.

Version 0.7 makes Ping Pong alternate correctly even when REAPER supplies a
duplicated mono source through a stereo track. The Stereo Width processor adds
frequency-dependent mid/side scaling, a mono-compatible all-pass Dimension
field, adjustable low-frequency focus, constant-power Balance, and dynamic
correlation protection. Its synthesized side field cancels in mono rather than
using a precedence delay that can comb-filter the fold-down.

The M/S Decoder expects encoded Mid on input channel 1 and encoded Side on input
channel 2, then reconstructs conventional left/right output. Its focused
controls are Width from 0–100% (35% by default), Mute Sides, and Swap Channels.
The live vectorscope shows the actual decoded stereo field with Mid, Side,
Left, and Right orientation.

The Tremolo processor provides three independent algorithms. Amplitude mode
uses a continuously shaped, stereo-phase-capable LFO. Harmonic mode splits low
and high frequencies into a complementary crossover and modulates the bands in
opposite directions. Vibrato mode performs true pitch modulation with a cubic
fractional-delay read. Rate can run freely in Hz or synchronize to musical host
divisions; pitch depth is shown in cents.

The Rotary Speaker models a single dual-rotor cabinet in depth. The horn and
drum have independent physical Doppler paths, directivity, speed, and inertia,
with Brake, Chorale, and Tremolo Speed states. Virtual microphone distance and
spread, Cabinet Color, Ambience, crossover, Drive, Rotor Balance, Motion, and
Spin-up shape the result. Mono input creates a stereo rotating field while
stereo input retains its original side information.

Control readouts use audible units where applicable: frequency controls switch
between Hz and kHz, timing controls use milliseconds or seconds, and gain
controls use dB. Composite intensity controls such as reverb Diffusion, Modulation,
and stereo Dimension remain percentages; Balance uses `L 35` / `C` / `R 35`
pan language instead of percentages or a raw signed number.

## Requirements

- CMake 3.22 or newer
- macOS 12+ with Xcode command-line tools, or Windows with Visual Studio 2022
- Git and an internet connection for the first dependency configure
- A JUCE commercial licence, or compliance with JUCE's AGPLv3 option

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release \
  --target megaDSPTests megaDSP_VST3 megaDSP_AU megaDSP_CLAP
ctest --test-dir build --output-on-failure
```

On macOS, build `megaDSP_AU` for Audio Unit output. Artifacts are written below
`build/megaDSP_artefacts`. Install the VST3 bundle in
`~/Library/Audio/Plug-Ins/VST3` on macOS or `%LOCALAPPDATA%/Programs/Common/VST3`
on Windows. macOS development bundles are ad-hoc signed after each build; set
`-DMEGADSP_ADHOC_SIGN=OFF` when applying a release signing identity.

See [`AGENTS.md`](AGENTS.md) for architecture, compatibility invariants,
validation requirements, and contributor guidance.

Host automation is intentionally tied to rack slot numbers. Reordering modules
moves module settings between slots, while existing automation remains attached
to its original slot. Version 0.23.1 uses state schema 7. Vintage Chorus remains
module value 14. It migrates schema-2
states without enabling the newer gain-compensation controls, preserves
schema-2/3 EQ edge bands as bell filters, and converts schema-4 edge rolloffs
into explicit HP/LP modes without moving their cutoff. Schema-5 Random
Granulizer states convert the old fixed Size to both new size handles, preserving
their fixed duration. Schema-6 reverb states convert each old constant-power Mix
to independent sine Wet and cosine Dry gains per Algorithmic or Convolution
Reverb slot; Algorithmic Tone is intentionally discarded while Convolution
Output is preserved. New reverbs default to 100% Dry and 10% Wet rather than a
mix-equivalent angle. It does not load pre-release 16-slot state.
