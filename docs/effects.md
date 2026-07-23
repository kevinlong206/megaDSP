# Effects reference

Detailed control and interaction notes for each of megaDSP's twenty-nine modules. For installation and a quick overview see the [README](../README.md).

Runtime-specific graphs show **DSP telemetry waiting** until the selected slot
has processed an audio block. After capture begins, an empty event field means
the DSP reported no event activity; it is not filled from an editor clock.

---

## EQ

A large knob-free response editor inspired by the directness of modern
parametric EQs. Double-click empty graph space to add a band, drag its node for
frequency and gain, and use the mouse wheel nearby for Q. Selecting an outer
node exposes **Bell**, **Low/High Shelf**, and **High/Low Pass** choices directly
below the graph. Double-click a node, or use **Remove**, to return that band to
a neutral inactive state. The right-side **Output** rail controls final EQ trim.

Pre/post spectrum traces run continuously while the tab is selected.

---

## Compressor

The **Threshold** control is a live input-level meter: signal fills the track behind the movable threshold marker. A 10-second input/output history and real-time gain-reduction overlay show what the compressor is doing over time, including a 10-second maximum-reduction line.

**Auto Makeup** slowly recovers average level lost to gain reduction. **Manual Trim** is always control 5; it appears secondarily when Auto Makeup is off or its retained value is nonzero. Both are automatable and enabled by default for new instances.

---

## Saturator

Displays a transfer curve with overlaid input and output waveforms. **Drive** is automatically level-compensated by comparing latency-aligned dry and nonlinear signal energy: harder drive adds density and harmonics without a large loudness jump. Compensation only attenuates excess wet gain; **Output** remains an independent final trim.

---

## Delay

The editable tap timeline visualises the delay path. A compact two-dimensional **Rate/Depth Movement** field edits both modulation parameters together. **Sync** toggles between free **Time** and a synced **Division**; the inactive control is hidden, unfocusable, and retains its automation value. Ping Pong alternates correctly even when the host supplies a duplicated mono source through a stereo track.

---

## Limiter

Entirely controlled in its enlarged graph. **Threshold** has a dedicated left handle and **Ceiling** a right handle, so both remain selectable when set to the same value. Ten-second input/output traces and the complete gain-reduction envelope show what limiting is doing over time, with current and maximum reduction readouts. **Release** and **Lookahead** are wide horizontal tracks. **Auto Gain** removes the static loudness advantage created by lowering Threshold while retaining Ceiling as a safety maximum; it clearly reports **OFF** or **MATCHED**. Double-click any continuous limiter control to restore its musical default.

---

## Algorithmic Reverb

A 16-line energy-preserving feedback network with clustered early reflections, sparse diffusion, decorrelated motion, and click-safe Room Scale and mode transitions.

**Decay** sets mid-band T60. **Damping** shortens or extends the high-band tail. **Room Scale** displays Compact, Natural, Large, or Vast while retaining the exact 25–200% value. **Low Cut** and **High Cut** control the signal entering the reverb. **Width** keeps low frequencies more coherent while widening the upper wet field. **Dry** and **Wet** are independent smoothed linear 0–100% amplitude gains; both may be fully off or on.

Modes are Hall, Chamber, and Plate. The interactive early-reflection and stereo decay field accepts a direct Decay/Room Scale handle.

---

## Convolution Reverb

Loads mono or stereo WAV, AIFF, and FLAC impulse responses from the **Load IR…** button or by dropping a file on the graph. Uses JUCE non-uniform partitioned convolution with a 256-sample head. IR file work stays off the audio thread.

The normalised IR envelope and shaded passband remain visible while draggable **Low Cut** and **High Cut** handles shape only the wet convolution signal. **Dry** and **Wet** are adjacent, independent linear 0–100% amplitude rails. **Output Trim** adjusts their sum. With no available IR, Wet contributes no signal and the result is `Dry × input × Output`. The absolute IR path is stored with plugin and preset state and follows rack reordering; if the file moves, the graph identifies the missing filename.

True-stereo IRs intentionally retain their captured left/right acoustic differences.

---

## Stereo Width

Provides frequency-dependent mid/side scaling, a mono-compatible all-pass **Dimension** field, adjustable low-frequency focus, constant-power **Balance** (displayed as `L 35` / `C` / `R 35` pan language), and dynamic correlation protection. The synthesised side field cancels in mono rather than using a precedence delay that can comb-filter on fold-down.

The graph visually separates Foundation, Dimension Field, and Mono Safe regions. Direct **Width** and **Mono Below** handles are the primary controls.

---

## M/S Decoder

Expects encoded Mid on input channel 1 and encoded Side on input channel 2, then reconstructs conventional left/right output. Controls: **Width** from 0–100% (35% by default), **Mute Sides**, and **Swap Channels**. The live vectorscope shows the actual decoded stereo field with Mid, Side, Left, and Right orientation.

---

## Tremolo

Three independent algorithms:

- **Amplitude** — continuously shaped, stereo-phase-capable LFO.
- **Harmonic** — splits low and high frequencies at a complementary crossover and modulates the bands in opposite directions. Exposes the **Crossover** control.
- **Vibrato** — true pitch modulation via cubic fractional-delay read; depth shown in cents.

**Rate** runs freely in Hz or synchronises to musical host divisions. Inactive controls (e.g. Crossover in non-Harmonic modes, Pitch Depth vs Tremolo Depth) are hidden, unfocusable, and retain their automation values.

---

## Rotary Speaker

Models a single dual-rotor cabinet. Horn and drum have independent physical Doppler paths, directivity, speed, and inertia. The dominant three-state **Speed** pills (Brake, Chorale, Tremolo) are the primary control. The rotor/microphone graphic owns Rotor Balance, Motion, microphone Distance/Spread, and Spin-up gestures. **Cabinet Color** and **Ambience** retain their exact DSP meanings. Mono input creates a stereo rotating field; stereo input retains its original side information.

---

## Dynamic EQ / De-Esser

A focused, stackable single-band processor. Drag the node horizontally from 20 Hz to 20 kHz and vertically from −18 dB cut to +12 dB boost; use the mouse wheel for Q. A separate draggable **Threshold** line overlays the live focused-detector level. The exact active filter curve and 10-second gain history show the result. Ratio, Attack, Release, and Stereo Link use compact tracks. Shape and Peak/RMS detector modes, external sidechain, and **Listen** use clear status pills.

An unavailable external sidechain is explicitly labelled and cannot be newly selected; if selected without a connected bus, detection falls back to the internal input.

Negative Range performs conventional downward de-essing; positive Range provides bounded dynamic boost. At 1:1 or 0 dB Range the audible EQ is flat unless Listen is enabled.

---

## Random Granulizer

Continuously records six seconds of recent mono or stereo input and launches up to 16 click-free windowed grains without stealing active voices. Uses a resettable xorshift PRNG; never allocates or locks on the audio thread.

**Size Minimum** and **Size Maximum** each span 50 ms–2 seconds (logarithmic); either handle can cross the other safely. Defaults are 80 ms min, 280 ms max. **Grain Rate** spans 0.5–30 launches per second. **Capture Range** reaches only through initialised history. **Reverse Chance** is the only control that changes playback direction. **Stereo Spread** uses constant-power placement with a mono-safe fold. Rhythmic **Delay** chooses tempo-quantised subdivisions from host BPM (fallback 120 BPM) without occupying grain voices while waiting. **Brightness** applies a stable per-grain low-pass cutoff. **Regeneration** returns wet energy to capture history with a guard against runaway. **Mix** is constant-power with exact dry output at 0%.

The graph combines Size Minimum and Maximum into one highlighted **MIN/MAX** window. Capture Range, Stereo Spread, and Rhythmic Delay Chance are edited directly on the live timeline; Voices, Grain Rate, Reverse Chance, Brightness, Regeneration, Mix, and Output use compact tracks.

---

## Vintage Chorus

Four genuinely separate modulation topologies, not presets over one tap pattern:

- **Vintage BBD** — asymmetric modulation, soft compander/preamp saturation, pre/de-emphasis colour, Age-dependent bandwidth, drift, and signal-gated clock character.
- **Dimension** — low-depth complementary quadrature taps for stable stereo width without a Haas path.
- **Tri-Chorus** — three-phase studio modulation with a controlled centre.
- **String Ensemble** — faster/deeper phase-offset low/high modulation groups with darker filtering.

**Density** fades one to six taps in and out while retaining each model's topology. Model transitions crossfade parallel topology outputs. All continuous controls — Rate, Depth, Delay, Width, bipolar Regeneration, Tone, Age, Stereo Phase, Mix, Output — are smoothed. Feedback is filtered and sub-unity at both polarities. **Width** modifies a mono-cancelling wet side field. Mix 0% is exactly dry apart from Output. Age noise is deterministic, low-level, and gated by input energy.

The animated graph shows every voice trajectory and phase relationship, direct Rate/Depth/Delay/Width editing, signed Regeneration polarity, grouped Density and Character rails, and compact Phase/Output rails.

---

## Beat Permuter

Captures recent tempo-locked slices and rearranges them into repeat, reverse,
scatter, and stutter patterns. The visual sequence editor keeps the rhythmic
result legible while probability and variation controls preserve musical timing.

---

## Spectral Prism

Uses a spectral pivot to bend, shift, smear, and freeze frequency content. Its
graph presents the transformation directly so spectral movement can be shaped
without relying on raw FFT values.

---

## Resonant Matrix

Routes tuned resonators through evolving signed feedback patterns. Pitch,
spacing, damping, feedback, and stereo motion create playable metallic textures
and pitched spaces while bounded feedback keeps transitions controlled.

---

## Wavefold Garden

Creates animated antialiased harmonics with dynamic wavefolding. Drive,
fold shape, envelope response, motion, tone, and mix turn level and gesture
into evolving nonlinear color rather than static distortion.

---

## Gate / Expander

Uses a filtered peak/RMS detector, separate opening and closing thresholds,
Hold, and smooth downward expansion. The graph directly edits Threshold and
Range and shows the selected DSP slot's detector level, gain envelope,
attenuation, and open state alongside input and output. A two-handle detector
passband, external-sidechain status, Listen, and Stereo Link remain close to the
history they affect.

---

## Transient Designer

Separates fast attack energy from slower sustain without tying the result to
absolute input level. Large bipolar Attack and Sustain handles sit on a live
component view sourced from the DSP's actual fast/slow envelopes and attack/
sustain shaping terms; Sensitivity, Speed, and Focus shape detection. Focus
changes what triggers the broadband gain movement rather than applying a static
EQ.

---

## Multiband Compressor

Splits audio into three reconstructing Linkwitz-Riley bands. Two horizontal
crossover handles and three vertical threshold handles define the result
directly on the spectrum, with DSP-reported gain-reduction histories and active
state for each band. Ratio, timing, Auto Makeup, Stereo Link, Mix, and Output
are shared to keep the processor readable.

---

## Studio Phaser

Cascades 2, 4, 6, 8, or 12 stable all-pass stages with click-safe topology
changes. The response graph shows the moving notches and directly edits Center,
Sweep, Rate, and Depth. Motion runs freely in hertz or follows musical host
divisions; signed Feedback and Stereo Phase broaden the available character.
Notch motion and topology-transition status use the DSP's captured LFO phases
and crossfade weights rather than an editor clock.

---

## Studio Flanger

Provides distinct Tape, Through-Zero, Jet, and BBD paths over one direct comb
display. Delay and Depth determine visible tooth spacing and movement. Rate can
run freely or sync to tempo, while signed Feedback, Stereo Phase, and Tone
shape the model. Every model uses one fixed reported latency so automation
never changes host timing. Tooth spacing and model-transition status follow the
captured delay trajectory and model crossfade actually used by the DSP.

---

## Diffusion Delay

Feeds a precise primary repeat into a modulated all-pass diffusion field.
Increasing Diffusion turns repeats into a denser stereo cloud without changing
the requested repeat time. While this slot is selected, the graph's moving
repeat and cloud marks are published by the audio DSP: progress follows the
processed repeat clock, brightness follows measured energy, and vertical
placement follows measured stereo balance. Silence produces no activity marks.
The graph owns Time or Division, cloud extent, wet passband, Width, and Ducking;
Feedback remains filtered and strictly bounded.

---

## Pitch Bloom

Shifts feedback repeats by Unison, Fifth, Octave, Octave + Fifth, or Two
Octaves, with a cents Fine control. Delay, Feedback, Bloom, Spread, wet
passband, and Ducking shape the result. Rising arcs are not inferred from those
controls: the selected slot publishes actual pitch-shifted repeat events with
their smoothed interval, repeat-clock progress, measured energy, pan, and stereo
spread. The dry path is aligned to the fixed overlapping-window shifter latency.

---

## Frequency Lab

Translates the spectrum by a signed hertz amount rather than resampling it by a
musical ratio. Coarse Shift, Fine, LFO motion, and channel Stereo Offset are
shown as source-to-destination lines over the spectrogram. A linear-phase
Hilbert path provides fixed latency; filtered signed Feedback remains bounded.
The spectrogram remains truthful through the rack's generic measured input and
output histories, while the marker and channel lines use the current
DSP-smoothed translated hertz and LFO state.

---

## Spatial Orbit

Moves mono or stereo material through Circle, Figure Eight, Pendulum, and
deterministic Wander paths. The top-down field directly edits Azimuth Span,
Distance, and Width while showing current position. Air Damping, bounded
Doppler, and Mono Below provide distance and foundation cues without sacrificing
mono compatibility. The source and trail use captured x/y/distance/path state;
Wander therefore displays the same deterministic resettable trajectory heard in
the audio rather than a separate UI random path.

---

## Signal Decay

Combines dithered bit-depth reduction, band-limited sample-and-hold, clock
Jitter, smoothly windowed Dropouts, deterministic Noise, Wow, Flutter, and
Stereo Wear. The original/degraded waveform and error lane make the mechanisms
visually distinct. Transparent endpoints and one fixed aligned modulation
latency keep parallel Mix predictable.
The waveforms remain the rack's generic measured input/output histories (no
module-specific copy is needed). DSP telemetry adds actual clock phases, stereo
wear, dropout gain, and dropout-start events; no editor-clock animation implies
audio synchronization.

---

## Analog Tape

Models four distinct transports: Worn Cassette, Consumer Reel, Ampex-Style
Deck, and Studer-Style Deck. Input and Drive set tape level and density; Bias,
Tape Speed, Head Bump, Wow, Flutter, Wear, and energy-gated Noise shape the
machine before the latency-aligned Mix and Output stages.

The nonlinear print path runs at 4x oversampling. A cubic transport delay,
machine/speed voicing, hysteresis-inspired memory, deterministic hiss, and
bounded wear modulation provide movement and age without runtime allocation.
The reel graphic follows captured DSP transport phase and reports actual
wow/flutter offset, dropout gain, and signal envelope rather than advancing on
the editor timer.
