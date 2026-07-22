# Effects reference

Detailed control and interaction notes for each of megaDSP's eighteen modules. For installation and a quick overview see the [README](../README.md).

---

## EQ

A large knob-free response editor. Drag bell points horizontally for frequency and vertically for gain. Drag HP/LP points vertically for resonance; drop a low or high point into its labeled edge lane to latch HP or LP mode without trapping the cutoff there. The **LOW** and **HIGH** pills switch topology directly. The mouse wheel adjusts Q only while the pointer is near a band point. The right-side **Output** rail controls final EQ trim. Double-click a point or the Output rail to restore its musical default.

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
