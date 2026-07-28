# Effects reference

Detailed control and interaction notes for each of megaDSP's fifty-three
modules. For installation and a quick overview see the [README](../README.md).

Runtime-specific graphs show **DSP telemetry waiting** until the selected slot
has processed an audio block. After capture begins, an empty event field means
the DSP reported no event activity; it is not filled from an editor clock.

## Latency behavior

**Spectral Prism**, **Pitch Bloom**, **Spectral Delay Canvas**, **Harmonic
Mirage**, **Time Mosaic**, and **Spectral Nebula** keep their dry path immediate
and contribute no host delay compensation. Their spectral or pitch-transformed
wet field begins after its internal analysis window, which acts as a short
pre-delay rather than delaying everything else in the DAW. Corrective spectral
tools and processors whose topology depends on phase alignment, oversampling,
or lookahead continue to report fixed latency and align their dry paths.

## Snapshots

The rack header contains eight complete snapshots. Snapshot 1 starts with the
current rack. Click any button to recall without copying the current rack;
clicking an unused slot activates a fresh blank rack. Shift-click saves the
current rack to an unused slot or overwrites a populated one. Edits immediately
become part of the selected snapshot. Drag a populated snapshot onto another
slot to replace the destination with a prepared copy; the source and current
selection remain unchanged.

Snapshots include module type/order, all controls and bypass states, global
Input/Output gain, and convolution IR or NAM model paths. MIDI Program Change
0-7 recalls Snapshots 1-8 on any channel at the exact event sample. A
zero-slope 20 ms input crossfade prevents switching clicks, after which up to
two outgoing effect tails continue to spill on zero input. Natural snapshot
spills are capped at eight seconds and fade over their final 500 ms. Sustained
generators such as Chord Loom and frozen spectral fields use a shorter
1.5-second spill instead of persisting indefinitely. A third outgoing tail
fades the oldest one to keep CPU bounded.

Normally, inactive snapshots remain prepared but do not process audio or
visualization. **LIVE SAFE** instead processes every populated snapshot once per
audio range, feeding silence to inactive racks so recall does not create a
temporary CPU spike. This costs predictable steady CPU; test the complete rack
set before live use. Inactive snapshots never receive or retain monitored input.

Each snapshot retains its native processing latency rather than delaying every
snapshot to the slowest one. In LIVE SAFE mode, a changed host latency is
reported only after audio callbacks stop, avoiding a PDC rebuild during playback
or monitoring at the cost of temporarily offset timing. Missing external files
leave the rest of a snapshot available and mark the affected snapshot with an
explicit error.

---

## EQ

A large knob-free response editor inspired by the directness of modern
parametric EQs. Double-click empty graph space to add a band, drag its node for
frequency and gain, and use the mouse wheel nearby for Q. Selecting an outer
node exposes **Bell**, **Low/High Shelf**, and **High/Low Pass** choices directly
below the graph. Double-click a node, or use **Remove**, to return that band to
a neutral inactive state. The right-side **Output** rail controls final EQ trim.

Pre/post spectrum traces run continuously while the EQ card is focused or
hovered.

---

## Auto Wah

Moves a resonant vocal filter from the dynamics of each pick. **Sensitivity**
sets a visible dB threshold against the live input meter, while **Pick Focus**
emphasizes the fast attack over the sustained note. **Center Frequency** and
**Sweep Range** define the motion directly in the main field; **Direction**
chooses an upward or downward response.

**Attack** controls how quickly the filter reaches the picked position and
**Release** controls its return. **Resonance** shapes the vocal emphasis,
while **Filter Type** selects Low Pass, Band Pass, Peak, or Classic Wah.
**Drive** adds progressively stronger level-referenced saturation and harmonic
push before the filter. The animated response follows
the measured envelope and actual cutoff. **Mix** retains an immediate
zero-latency dry path.

---

## Compressor

The **Threshold** control is a live input-level meter: signal fills the track behind the movable threshold marker. A 10-second input/output history and real-time gain-reduction overlay show what the compressor is doing over time, including a 10-second maximum-reduction line.

**Auto Makeup** slowly recovers average level lost to gain reduction. **Manual Trim** is always control 5; it appears secondarily when Auto Makeup is off or its retained value is nonzero. Both are automatable and enabled by default for new instances.

---

## Opto Leveler

A feedback optical leveler with a dual-memory cell: fast illumination catches
level changes while slower release memory produces program-dependent recovery.
Peak Reduction drives the optical cell, Emphasis shapes the detector, and
Speed moves between slower leveling and faster control without turning the
processor into a feed-forward compressor.

Stereo detection is linked so gain movement cannot pull the image sideways.
The fixed nonlinear path remains latency-aligned at Mix zero. The graph shows
measured input/output levels, optical memory, and gain reduction only while its
slot is selected for capture.

---

## FET Limiter

A fast feedback FET compressor with calibrated sub-millisecond attack, stepped
4:1, 8:1, 12:1, and 20:1 ratios, plus a distinct **ALL** mode rather than an
alias for the highest ratio. Input drives the gain-reduction stage; Output,
Attack, Release, detector filtering, Mix, and final trim shape the result.

Stereo-linked detection preserves image stability. Parallel Mix uses the same
fixed nonlinear latency on dry and wet paths, and the graph reports measured
level and reduction from the selected DSP slot.

---

## Saturator

Displays a transfer curve with overlaid input and output waveforms. **Drive** is automatically level-compensated by comparing latency-aligned dry and nonlinear signal energy: harder drive adds density and harmonics without a large loudness jump. Compensation only attenuates excess wet gain; **Output** remains an independent final trim.

---

## Guitar Cabinet

A fully procedural guitar speaker, enclosure, microphone, and compact room
model intended to follow an amp or preamp. It does not load or approximate a
cabinet impulse response: the response changes dynamically with cone excursion,
power compression, breakup, cabinet modes, microphone placement, and input
level.

Choose one of four generic **Speaker** families, four **Cabinet** constructions,
and **Dynamic**, **Ribbon**, or **Condenser** microphone voicings. Drag the
microphone across the cabinet graphic to move from **Center** toward **Edge**;
drag it vertically to set **Mic Distance** from 1 to 100 cm. The lower tracks
control **Speaker Drive**, **Cone Breakup**, **Cabinet Resonance**, **Damping**,
**Room**, mono-compatible **Room Width**, and final **Output**.

The animated cone, compression color, breakup ring, and room field come from
capture-gated DSP telemetry. Model changes crossfade without clearing the
active speaker state, and all choices share one fixed reported latency.

---

## Program Equalizer

Models the interacting passive networks of a classic program equalizer rather
than summing unrelated digital bells. Low Boost and Low Attenuation remain
separate controls around a shared stepped frequency, so simultaneous settings
produce the characteristic dip-and-lift contour. A broad proportional-Q high
boost and stepped high attenuation complete the passive section.

Stepped frequency changes crossfade prepared filter states. One fixed-latency,
bounded makeup stage restores passive loss, and the graph displays the actual
combined response of every interacting section.

---

## Delay

The editable tap timeline visualises the delay path. A compact two-dimensional **Rate/Depth Movement** field edits both modulation parameters together. **Sync** toggles between free **Time** and a synced **Division**; the inactive control is hidden, unfocusable, and retains its automation value. Ping Pong alternates correctly even when the host supplies a duplicated mono source through a stereo track.

---

## Limiter

Entirely controlled in its enlarged graph. **Threshold** has a dedicated left handle and **Ceiling** a right handle, so both remain selectable when set to the same value. Ten-second input/output traces and the complete gain-reduction envelope show what limiting is doing over time, with current and maximum reduction readouts. **Release** and **Lookahead** are wide horizontal tracks. **Auto Gain** removes the static loudness advantage created by lowering Threshold while retaining Ceiling as a safety maximum; it clearly reports **OFF** or **MATCHED**. Double-click any continuous limiter control to restore its musical default.

---

## Algorithmic Reverb

A 16-line energy-preserving feedback network with clustered early reflections, sparse diffusion, decorrelated motion, and click-safe Room Scale and mode transitions.

**Decay** sets mid-band T60. **Damping** shortens or extends the high-band tail. **Room Scale** displays Compact, Natural, Large, or Vast while retaining the exact 25–200% value. **Low Cut** is a true high-pass filter and **High Cut** is a low-pass filter on the signal entering the reverb. **Width** keeps low frequencies more coherent while widening the upper wet field. **Ducking** applies up to 18 dB of smooth input-responsive attenuation to the completed wet field and releases naturally into the tail. **Dry** and **Wet** are independent smoothed linear 0–100% amplitude gains; both may be fully off or on.

Modes are Hall, Chamber, and Plate. The interactive early-reflection and stereo decay field accepts a direct Decay/Room Scale handle.

---

## Convolution Reverb

Loads mono or stereo WAV, AIFF, and FLAC impulse responses from the **Load IR…** button or by dropping a file on the graph. Uses JUCE non-uniform partitioned convolution with a 256-sample head. IR file work stays off the audio thread.

The last successfully loaded IR directory is saved with plugin state. **< IR**
and **IR >** browse supported files in case-insensitive filename order and wrap
at either end; after clearing an IR, **Load IR…** and directory browsing retain
that location.

The normalised IR envelope has an unobstructed waveform area. A dedicated strip
below it owns draggable **Low Cut** (high-pass) and **High Cut** (low-pass)
handles that shape only the wet convolution signal. A separate control deck
keeps **Dry**, **Wet**, **Ducking**, and **Output Trim** together rather than
covering the waveform. Ducking applies up to 18 dB of smooth input-responsive
attenuation to the filtered wet response and releases naturally into the tail.
Dry and Wet are adjacent, independent linear 0–100% amplitude rails. Output
Trim adjusts their sum. With no available IR, Wet contributes no signal and the
result is `Dry × input × Output`. The absolute IR path is stored with plugin and
preset state and follows rack reordering; if the file moves, the graph
identifies the missing filename.

Mono IRs use a centered wet send, even for asymmetric stereo input. Stereo IRs
retain their captured left/right acoustic differences.

---

## Studio Plate

A physically informed plate field with immediate Dry and zero host delay
compensation. A prepared modal onset feeds a dense frequency-dependent FDN
tail; Decay, Damping, Size, Diffusion, modulation, and pickup geometry shape
the plate without replacing active state during structural changes.

Wet ducking acts only on the completed plate field and releases into the tail.
The synthetic pickup side component cancels on mono fold-down. The graph uses
capture-gated modal, diffusion, and pickup telemetry rather than editor-clock
animation.

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

**Size Minimum** and **Size Maximum** each span 50 ms–2 seconds (logarithmic); either handle can cross the other safely. Defaults are 80 ms min, 280 ms max. **Grain Rate** spans 0.5–30 launches per second. **Capture Range** reaches only through initialised history. **Reverse Chance** is the only control that changes playback direction. **Stereo Spread** uses constant-power placement with a mono-safe fold. Rhythmic **Delay** chooses tempo-quantised subdivisions from host BPM (fallback 120 BPM) without occupying grain voices while waiting. **Brightness** applies a stable per-grain low-pass cutoff. **Regeneration** returns wet energy to capture history with a guard against runaway. Independent smoothed **Dry** and **Wet** levels allow parallel-style blending, including dry-only, wet-only, or silence.

The graph combines Size Minimum and Maximum into one highlighted **MIN/MAX** window. Capture Range, Stereo Spread, and Rhythmic Delay Chance are edited directly on the live timeline; Voices, Grain Rate, Reverse Chance, Brightness, Regeneration, Dry, and Wet use compact tracks.

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

Captures recent tempo-locked slices and rearranges them without stopping the
transport. Choose a **Slice** value, a **Pattern**, and the **Chance** that a
permutation starts at each slice boundary. **History** controls how many recent
slices are available; **Length** controls how many output steps the event lasts.

- **Repeat** loops one captured slice.
- **Reverse** walks backward through the captured slices and plays them backward.
- **Rotate** cycles through the captured slices.
- **Shuffle** chooses a new captured slice for each step.

The sequence display shows each actual DSP-selected output step and its source
slice, with `1` meaning the newest slice in the captured history. When Stereo
selects different left and right sources, both numbers are shown. Slice Length,
Pitch, Variation, Stereo, Feedback, Mix, and Output provide secondary shaping.
Each event uses one fixed captured window, so its source does not drift forward
while the event is playing.

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

## Stage Vocal

Combines the essential cleanup, control, and finishing stages for a live vocal
in one zero-latency module. A two-stage Low Cut removes handling and stage
rumble; the stereo-linked expander lowers bleed without hard muting; and the
frequency-selective de-esser controls sibilance before smooth linked
compression. Presence and Air provide final broad tone shaping, followed by
Output trim. The live display shows measured input/output levels and separate
expander, de-esser, and compressor reduction.

| # | Control | Range | Default | Description |
|---|---------|-------|---------|-------------|
| 0 | Low Cut | 40–220 Hz | 80 Hz | Two-stage rumble filter |
| 1 | Expander Threshold | -70–-20 dB | -50 dB | Level below which stage bleed is reduced |
| 2 | Expander Range | 0–36 dB | 18 dB | Maximum downward attenuation |
| 3 | De-Ess Frequency | 3–10 kHz | 6.5 kHz | Start of the detected sibilance band |
| 4 | De-Ess Amount | 0–12 dB | 4 dB | Maximum sibilance reduction |
| 5 | Compressor Threshold | -40–0 dB | -18 dB | Level above which compression begins |
| 6 | Ratio | 1:1–10:1 | 3:1 | Compression strength |
| 7 | Attack | 0.5–50 ms | 8 ms | Compressor response to peaks |
| 8 | Release | 30–800 ms | 120 ms | Compressor recovery time |
| 9 | Presence | -6–+6 dB | +1 dB | Broad vocal-definition contour |
| 10 | Air | -6–+6 dB | +1.5 dB | High-frequency openness |
| 11 | Output | -18–+12 dB | 0 dB | Final level trim |

All audible controls are smoothed. Mono input remains mono-compatible, while
stereo-linked detection prevents gain changes from shifting the image.

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
The graph owns a full-width Time or Division axis and a clearly labelled Cloud
Density axis, plus wet passband, Width, and Ducking; Feedback remains filtered
and strictly bounded.

---

## Pitch Bloom

Shifts feedback repeats by Unison, Fifth, Octave, Octave + Fifth, or Two
Octaves, with a cents Fine control. Delay, Feedback, Bloom, Spread, wet
passband, and Ducking shape the result. Rising arcs are not inferred from those
controls: the selected slot publishes actual pitch-shifted repeat events with
their smoothed interval, repeat-clock progress, measured energy, pan, and stereo
spread. The live dry path remains immediate; the shifted repeat arrives after
the overlapping-window shifter delay.

---

## Harmonic Resonator

Turns incoming audio into a tuned bank of eight modal resonators. Root and
Harmony select Major, Minor, suspended, seventh, fifth, or octave-drone
voicings; Register moves the complete field from low fundamentals to airy
overtones. Decay controls the ring time, Tone balances low and high chord
voices, and gentle cent-domain Motion keeps sustained fields alive.

Root, Harmony, and Register changes glide between prepared resonator
frequencies rather than discontinuously replacing active state. Stereo Spread
pans alternating voices while preserving mono fold-down, Low Cut and High Cut
shape only the excitation, and the resonator field is bounded before Mix and
Output. The graph displays the actual DSP-smoothed voice frequencies and
measured wet activity.

---

## Chord Loom

Automatically captures a newly strummed guitar note or chord and sustains its
timbre until another strum arrives. Three fixed capture layers allow a new
chord to be gathered while the previous layer is still sounding; once enough
source material is available, **Transition** smoothly fades the old chord away
and brings in the new one.

The held sound is rebuilt from six overlapping, deterministic grains per layer.
Their positions, lengths, and subtle detuning vary so the result behaves like a
pad rather than an audible repeating loop. **Texture** moves from long smooth
grains toward more animated weaving, **Motion** adds bounded pitch drift,
**Octave Bloom** introduces an optional upper layer, and **Stereo Spread**
distributes grains without compromising mono fold-down. **Sensitivity** lowers
the displayed input threshold and the required attack contrast together. Live
Level and Attack meters show the signal against both detector thresholds, and
flash when a new capture is triggered. **Capture** sets the gathered source
duration, Low Cut and High Cut affect only the sustained layer, and **Dry** and
**Sustain** are independent levels.

All capture buffers, layers, grains, and telemetry storage are allocated during
prepare. The audio thread never allocates, steals a sounding grain, or reads
outside the written capture region.

---

## Volume Swell

Automatically recreates the texture of rolling up a guitar volume knob or
pedal after each chord. A fixed 30 ms lookahead detects the new attack before
it reaches the output, fades the previous chord smoothly toward **Floor**, and
then raises the new chord over **Swell Time**. Retriggering during an unfinished
swell starts from the current gain without a discontinuity.

**Curve** moves continuously from a fast initial rise through linear to a slow,
bowed-in start. **Sensitivity** adjusts both the input-level and attack-contrast
thresholds shown by the live meters. **Mix** blends the latency-aligned dry and
swollen signals, while **Output** supplies final trim. The lookahead is fixed and
reported in every setting; processing after prepare is allocation-free.

---

## Spectral Nebula

Continuously absorbs guitar, vocal, or other live spectra into an evolving
cloud rather than repeating a captured loop. **Smear** controls how gradually
the cloud absorbs and replaces notes, while **Decay** controls how long the last
sound blooms after the input stops. New notes smoothly retire unsupported old
partials instead of accumulating into a dissonant cluster. **Drift** applies
slow bounded cent-domain motion, and **Shimmer** derives a restrained,
phase-coherent octave only from stable tonal peaks.

**Focus** moves from airy noisy detail through a balanced harmonic cloud to
prominent tonal partials. Peak-locked spectral lobes preserve the relationships
within each note instead of turning adjacent FFT bins into detuned oscillators.
**Freeze** stops cloud-state updates without stopping phase motion,
so frozen material remains alive rather than becoming a static FFT frame.
**Width** scales only the side domain and therefore preserves mono fold-down.
Low Cut and High Cut shape only the synthesized cloud; **Dry** is immediate and
**Cloud** is an independent delayed spectral level.

Spectral Nebula uses the shared prepared fixed-latency STFT foundation. Its
per-bin magnitude, pitch-tracked phase advance, and scratch storage is allocated
during prepare. Phase-coherent resynthesis preserves notes between FFT bins
without imposing a repeating frame-rate pulse. Audio processing is lock-free,
allocation-free, bounded, and deterministic after reset.

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

The default Ampex-Style 15 ips setup emphasizes level-compensated saturation,
subtle compression, transient softening, gentle high-frequency rounding, and a
restrained head resonance. Wow, Flutter, Wear, and Noise default to zero so
transport defects and hiss are added deliberately rather than baked into the
core tape tone. The nonlinear print path runs at 4x oversampling. A cubic
transport delay, machine/speed voicing, hysteresis-inspired memory,
deterministic hiss, and bounded wear modulation provide optional movement and
age without runtime allocation.
The reel graphic follows captured DSP transport phase and reports actual
wow/flutter offset, dropout gain, and signal envelope rather than advancing on
the editor timer.

---

## Vinyl Simulator

Builds record wear from deterministic dust, crackle, scratches, surface noise,
rumble, groove wear, wow, and flutter rather than replaying a texture sample.
The transport uses one fixed latency-aligned delay for wow and flutter, so Mix
zero remains aligned while the modulation controls move.

Synthetic stereo texture exists only in the side domain and cancels on mono
fold-down. Noise events remain bounded and resettable, and the spinning record
view follows capture-gated measured dust, scratch, wear, transport, and signal
activity.

---

## Resonance Tamer

Finds narrow energy above a local spectral baseline and applies bounded,
frequency-smoothed reduction. Selectivity and Reaction set how precisely and
quickly it acts; Tone Bias, Low/High Limit, and Transient Preserve keep the
result musical. The graph separates measured input, baseline, detected excess,
and actual reduction.

---

## Spectral Balance

Tracks long-term perceptual-band energy and gradually guides it toward Natural,
Warm, Clear, Vocal, or Flat contours. Low Weight, Presence, Air, Detail, and
Transient Preserve shape a broad zero-mean correction rather than adding static
makeup gain. The graph shows measured energy, target anchors, and correction.

---

## Phase Coherence

Estimates internal left/right timing and phase mismatch, then corrects only
high-confidence material. Range, Crossover, maximum alignment, phase rotation,
Stereo Preserve, and Mono Below define safe boundaries. Mono and already
coherent signals remain effectively unchanged; the graph shows before/after
correlation and the correction actually applied.

---

## Loudness Rider

Uses gated K-weighted loudness to move a bounded fader toward the selected LUFS
target. Window, Reaction, fixed-latency Lookahead, Transient Hold, and Crest
Preserve separate long-term riding from compression. Its history plots measured
loudness, target, gate, and ride gain.

---

## Adaptive Clipper

Classifies fast transient and slower body energy to continuously adapt its
clipping knee. Clean, Punch, and Dense styles combine with Shape, Transient
Bias, Release, Stereo Link, prepared 2x/4x/8x oversampling, Auto Trim, and a
dBTP Ceiling. Waveform and event views report actual clipped energy and margin.

---

## Spectral Delay Canvas

Stores spectral frames and reads low, mid, and high frequency regions at
independent delay offsets. Free time or host divisions set the base, while
Feedback, Diffusion, Stereo Spread, and Freeze evolve the canvas. The display
follows measured energy moving through frequency and history.

---

## Formant Forge

Interpolates stable vocal-tract filterbanks across a directly editable vowel
field. Human, Tube, Creature, and Metallic models respond to Formant Shift,
Resonance, signal-gated Breath, Motion, and Stereo Spread without shifting the
source pitch. Live formant positions come from captured DSP state.

---

## Harmonic Mirage

Tracks confident partials and resynthesizes phase-continuous Harmonic,
Subharmonic, Hollow, or Metallic layers. Tracking, partial count, Even/Odd
weighting, Inharmonicity, Fine Drift, Response, and Transient Preserve define
the transformation. The graph distinguishes measured and generated partials.

---

## Chaos Field

Integrates deterministic Lorenz, Rossler, or double-pendulum-style attractors
and maps their captured axes to filter, fractional delay, pan, and stereo
motion. It can run freely or from host divisions; bounded signed Feedback and
continuous state keep transitions stable. The visible trail is the trajectory
used by the audio DSP.

---

## Time Mosaic

Partitions recent spectral history into frequency tiles and reconstructs them
from evolving ages. History, tile width/time, Age, Motion, Coherence, Pitch
Drift, Freeze, and Stereo Spread create deterministic rearrangements while
preserving phase within tiles. The map displays actual tile assignments and
reassignment events.

---

## Neural Amp Modeler

Loads `.nam` neural amp model files for authentic guitar amp and effect
tones powered by machine learning. Supports all standard NAM architectures
(Linear, LSTM, WaveNet, ConvNet, Container) plus the A2 Standard (8-channel)
and A2 Nano (3-channel) fast-path models.

### Controls

| # | Control | Range | Default | Description |
|---|---------|-------|---------|-------------|
| 0 | Pre Gain | -24..+24 dB | 0 dB | Level entering the neural model |
| 1 | Bass | -12..+12 dB | 0 dB | Low-shelf tone control at 120 Hz |
| 2 | Mid | -12..+12 dB | 0 dB | Broad mid control centered at 750 Hz |
| 3 | Treble | -12..+12 dB | 0 dB | High-shelf tone control at 3.2 kHz |
| 4 | Post Gain | -24..+24 dB | 0 dB | Level after the model and tone stack |

The module always outputs the modeled path; use the rack's module bypass for
dry comparison. `SlimmableContainer` models load at their full-size setting.

### Unloaded Behavior

With no model loaded, the module produces silence—no synthetic model signal is
generated.
The UI shows "No model loaded" and prompts the user to drop a `.nam` file.

### Model File Licensing

`.nam` model files are user-provided and not included with megaDSP. Models
trained by the community may have various licenses. The NAM processing engine
(NeuralAmpModelerCore) is MIT-licensed. See THIRD_PARTY_NOTICES.md for details.

### Sample Rate Handling

NAM models declare an expected sample rate. When the host rate matches, the
model runs directly. Sample-rate mismatch handling is planned for a future
update with Lanczos resampling via the upstream AudioDSPTools infrastructure.

---

## Midnight Current

Creates the dark, submerged psychedelic character of an electric keyboard
processed through an early analog synthesizer, without reproducing a particular
performance. **Filter Center** and **Filter Sweep** shape a moving resonant
low-pass current. **Motion Rate**, **Phase Depth**, and signed **Regeneration**
drive an eight-stage phase network, while **Resonance**, **Warmth**,
**Softness**, and **Drift** control its analog weight and instability.

The main response field directly edits Filter Center and Filter Sweep and shows
the cutoff and phase motion captured from the DSP. **Stereo Spread** changes
only the wet side field, so mono fold-down preserves the wet center. **Mix**
blends against an immediate dry path with no reported latency, and **Output**
provides final trim.

---

## Resonant Sculpture

Turns incoming audio into excitation for a twelve-mode virtual object.
**Material** selects String, Glass, Metal, or Membrane frequency spacing,
brightness, and decay behavior. **Shape** continuously warps the modal geometry,
while the object follows the incoming fundamental. **Pitch Offset** transposes
that tracked pitch by up to two octaves, and **Decay** controls how long the
object continues ringing. When pitch confidence falls, the last confident
fundamental is held so an existing modal tail does not wobble or collapse.

**Strike** extracts pick and consonant attacks as sharp impacts. **Bow** converts
sustained input into continuous excitation; both remain scaled by
**Excitation**. **Mutation** adds slow independent modal drift, while **Chaos**
adds deterministic irregular motion without changing the stable decay gain.
**Stereo Field** spreads alternating modes only in a mono-cancelling side
field. The animated structure deforms from captured mode energy, attack, shape,
and chaos telemetry. **Mix** retains an immediate zero-latency dry path.
