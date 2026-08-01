# megaDSP Contributor Guide

## Product

megaDSP is a JUCE 8 modular effects rack delivered as VST3, Audio Unit, and
CLAP. Development is validated in REAPER with VST3 first. The rack has eight
serial slots and twelve fixed generic controls per slot.

## Non-negotiable compatibility rules

- Preserve existing `ModuleType` integer values. Append new module types.
- Current appended values continue through Wavefold Garden 18, Gate / Expander
  19, Transient Designer 20, Multiband Compressor 21, Studio Phaser 22, Studio
  Flanger 23, Diffusion Delay 24, Pitch Bloom 25, Harmonic Resonator 26, Spatial
  Orbit 27, Signal Decay 28, Analog Tape 29, Resonance Tamer 30, Spectral
  Balance 31, Phase Coherence 32, Loudness Rider 33, Adaptive Clipper 34,
  Spectral Delay Canvas 35, Formant Forge 36, Harmonic Mirage 37, Chaos Field
  38, Time Mosaic 39, Guitar Cabinet 40, Chord Loom 41, Volume Swell 42,
  Spectral Nebula 43, Midnight Current 44, Auto Wah 45, Resonant Sculpture 46,
  Neural Amp Modeler 47, Stage Vocal 48, Vinyl Simulator 49, Opto Leveler 50,
  FET Limiter 51, Program Equalizer 52, Studio Plate 53, Velvet Drive 54,
  Wormhole Convolution 55, Bitcrusher 56, Portal Permuter 57, and IR Mosaic
  58.
- Preserve the eight-slot, twelve-control host parameter topology and existing
  parameter IDs.
- Keep state migration compatible with schema 2 through current schema 13.
- Module type parameters are not automatable; saved integer module values must
  continue to restore exactly.
- Processing after `prepare()` must be allocation-free, lock-free, finite, and
  safe for mono or stereo buffers.
- Do not launch REAPER during automated validation. Install the validated VST3
  and ask for a full REAPER restart when it was already running.

## Architecture

- `src/Parameters.*`: fixed host parameter layout, descriptors sourced from the
  registry, and state migration.
- `src/modules/ModuleRegistry.*`: stable module enum, presentation metadata,
  control labels, defaults, formatting/parsing, contextual predicates,
  factories, and capabilities.
- `src/modules/*`: module-focused DSP declarations and implementations plus
  shared DSP helpers.
- `src/DspModules.h`: compatibility umbrella for all DSP module declarations.
- `src/EffectRack.*`: slot ownership, serial routing, bypass interpolation,
  latency alignment, visualization capture, and tail aggregation.
- `src/SnapshotState.*`: independent fixed rack states for eight prepared
  snapshots, including external asset references and schema serialization.
- `src/PluginProcessor.*`: host buses, state, presets, latency reporting, and
  rack topology operations plus sample-accurate snapshot recall and spill.
- `src/PluginEditor.*`: header identity, snapshots, sticky chain navigator,
  vertically scrolling card rack, and semantic module controls.
- `src/ui/*`: reusable effect graph shell, module view interface, and factory.
- `src/ui/modules/*`: module-specific graph rendering and interactions.
- `tests/DspModuleTests.cpp`: focused DSP module tests.
- `tests/TestMain.cpp`: rack, host-contract, state, and integration tests.

Every new processor must be wired through Parameters, `src/modules`, EffectRack,
registry presentation metadata, the UI module-view factory, defaults,
parsing/formatting, and tests.

## DSP expectations

- Smooth every audible continuous parameter and make structural changes
  click-safe.
- All eight populated snapshots own prepared rack graphs. MIDI Program Change
  0-7 recall must be sample-accurate and perform no allocation, locking, file
  access, construction, or destruction on the audio thread. Recall uses a
  zero-slope 20 ms input crossfade, keeps at most two zero-input spill tails,
  and fades the oldest on overflow. Snapshot spill is distinct from host tail
  reporting: natural tails are capped at eight seconds with a final 500 ms
  fade, while sustained generators and frozen spectral fields spill for 1.5
  seconds. Normally inactive snapshots do not process.
  LIVE SAFE processes every populated graph exactly once per range, feeding
  silence to inactive graphs and reusing their prepared outputs for transitions
  and spills so recall does not increase graph count. It must not send per-control
  host notifications on recall. Snapshot latency remains native; LIVE SAFE
  defers PDC notification until callbacks stop rather than adding common latency.
- Keep stereo effects mono-compatible. Synthetic side content must cancel on
  fold-down; do not use uncompensated Haas delays for widening.
- Preserve a focused low-frequency image in stereo processors.
- Ping Pong must alternate duplicated mono sources in a stereo host and remain
  well behaved for asymmetric stereo input.
- Avoid hidden broad catches, silent error fallbacks, feedback instability, and
  nonlinear clamps used as substitutes for stable DSP design.
- Use meaningful controls with accurate units and distinct audible roles.
- EQ controls 11 and 12 (zero-based indices 10 and 11) encode three stable
  outer-band shapes: Bell, Shelf, and Cut. Legacy 0/1 values remain Bell/Cut.
  Shape changes crossfade parallel filters; node removal returns a band to a
  neutral Bell, and cutoff frequency remains independent of shape.
- Tremolo mode changes must crossfade continuously; rotary horn and drum speeds
  must retain independent physical inertia.
- Saturator gain compensation compares latency-aligned dry and pre-Output wet
  energy. It may attenuate excess nonlinear gain but must never boost quiet
  material or absorb the user's Output trim.
- Convolution Reverb uses JUCE non-uniform partitioned convolution with a
  256-sample head and one shared convolution message queue. IR file work must
  stay off the audio thread; Dry and Wet are independent smoothed linear gains,
  Low Cut/High Cut process only the wet path, and an unloaded or unavailable IR
  produces `Dry * input * Output` with no synthetic wet signal. Mono IR wet
  sends are centered; stereo IRs preserve their encoded channel differences.
  Ducking attenuates only the completed wet path and releases into the tail.
- Algorithmic Reverb applies independent smoothed Dry to the original input and
  Wet to the completed reverb field. Damping and input Low Cut/High Cut remain;
  Ducking attenuates only the completed wet field and releases into the tail;
  there is no independent broad output Tone stage.
- Dynamic EQ is a stackable single focused band. Its detector must remain
  frequency-selective, external-sidechain selection must fall back to internal
  input when unavailable, and Ratio 1:1 or Range 0 dB must remain transparent
  outside Listen. Range bounds both dynamic cut and boost; shape, detector,
  sidechain, and Listen transitions must remain click-safe.
- Random Granulizer owns a prepared six-second circular history, a fixed pool
  of 16 voices, and fixed pending-event storage. Grain scheduling uses its
  resettable xorshift PRNG; it must never allocate, lock, steal an active voice,
  or read unwritten history on the audio thread. Delay events wait outside the
  voice pool and use host BPM or a stable 120 BPM fallback. The two logarithmic
  50 ms–2 second size handles form a canonicalized range, and each grain chooses
  a deterministic bounded duration from it; playback speed is unity forward or
  reverse. Per-grain filters remain stable, and feedback gain design—not only
  the final guard—must prevent runaway.
- Vintage Chorus owns prepared stereo circular delays and six fixed voice
  slots. Reads use cubic interpolation and always remain behind the write head.
  Vintage BBD, Dimension, Tri-Chorus, and String Ensemble are distinct
  topologies, not presets over one tap pattern. Model transitions crossfade
  parallel topology outputs, voice changes fade taps, and all continuous
  controls are smoothed. Feedback is filtered and sub-unity at both polarities;
  Age character is deterministic and signal-energy gated; Width modifies a
  mono-cancelling wet side field; Mix 0% is exactly dry apart from Output.
- Gate / Expander uses separate open/close thresholds, detector Low/High Cut,
  Hold, and bounded dB-domain attenuation. External sidechain must fall back to
  internal input when unavailable; Listen auditions only the detector path.
- Transient Designer detection may be frequency-focused, but gain shaping is
  broadband. Neutral Attack/Sustain and Mix 0% remain transparent; Clip Guard
  is never a substitute for bounded gain design.
- Multiband Compressor uses reconstructing crossover bands and one independent
  detector per band. Ratio 1:1 must reconstruct flat; crossover automation is
  click-safe and Auto Makeup never boosts an inactive band.
- Studio Phaser and Studio Flanger keep their modulation phase across Sync and
  structural changes. Stage/model changes crossfade prepared paths; signed
  feedback remains filtered and sub-unity. Flanger reports one fixed maximum
  latency and aligns dry audio in every model.
- Diffusion Delay keeps the requested primary repeat time independent of
  diffusion density. Pitch Bloom uses ratio pitch shifting, not Hz-domain
  translation. Pitch Bloom keeps immediate dry and treats shifter delay as wet
  pre-delay. Both use filtered bounded feedback and conservative measured tails.
  Their event telemetry is emitted only for active visualization slots and
  visualizes measured DSP activity rather than control-derived events.
- Harmonic Resonator uses smoothly retuned, bounded modal filters to build
  musical chord fields without allocating on the audio thread. Chord Loom uses
  three fixed capture layers and overlapping randomized grains; new strums
  crossfade held layers without reading unwritten capture memory or producing
  an obvious repeated loop. Volume Swell uses one fixed reported lookahead in
  every setting, fades the previous level down without a discontinuity, and
  begins the configured rise before the detected attack reaches output.
  Spectral Nebula uses the shared fixed-latency STFT foundation, stores bounded
  per-bin cloud magnitude and pitch-tracked phase advance, peak-locks spectral
  lobes, replaces unsupported old-note energy while input is active, and limits
  shimmer to stable tonal peaks. It resynthesizes off-bin notes without
  frame-rate amplitude pulsing, stops state updates during Freeze without
  allocating, and scales only the side domain for mono-safe Width.
  Spatial Orbit preserves incoming side
  information and mono fold-down while applying bounded Doppler. Signal Decay
  uses a resettable deterministic PRNG and a fixed aligned modulation latency.
- Analog Tape uses its prepared 4x oversampled nonlinear path and one fixed,
  latency-aligned transport delay for every machine and speed. Its deterministic
  hiss stays energy-gated, feedback-free wear remains bounded, and the reel,
  modulation, dropout, and signal visuals use capture-gated DSP telemetry rather
  than editor-clock animation.
- Vinyl Simulator uses deterministic dust, crackle, scratch, surface-noise, and
  rumble generators plus one fixed latency-aligned transport delay for wow and
  flutter. Synthetic stereo texture cancels on fold-down, Mix zero remains
  latency-aligned dry, and the record view uses capture-gated measured activity.
- Opto Leveler uses a feedback detector and dual-memory optical cell with
  program-dependent recovery. FET Limiter uses a feedback topology with
  calibrated sub-millisecond attack and a distinct ALL-ratio mode. Both keep
  their fixed nonlinear latency aligned at Mix zero, link stereo detection, and
  publish only capture-gated measured dynamics telemetry.
- Program Equalizer models low boost and attenuation as separate interacting
  passive networks followed by one fixed-latency bounded makeup stage. Stepped
  frequency changes crossfade, and its graph shows the actual combined response.
- Studio Plate keeps immediate Dry and zero host PDC while a prepared modal/FDN
  wet field supplies plate onset and dense frequency-dependent decay. Structural
  changes preserve the tail, completed-wet ducking does not affect Dry, and its
  synthetic pickup side field cancels on mono fold-down.
- Resonance Tamer and Spectral Balance share only the prepared fixed-latency
  STFT foundation; their detection, smoothing, targets, and correction behavior
  remain module-specific. Amount-zero or Mix-zero paths stay latency-aligned,
  and spectral telemetry is published only for active visualization slots.
- Phase Coherence applies internal stereo correction only with sufficient
  confidence and leaves mono input unchanged. Loudness Rider uses gated
  long-term gain riding rather than a compressor curve. Adaptive Clipper keeps
  all prepared oversampling choices aligned to one fixed reported latency.
- Spectral Delay Canvas and Time Mosaic preallocate their maximum spectral
  histories during `prepare()`. Freeze stops history writes without allocating;
  age/delay transitions crossfade smoothly and event telemetry reflects actual
  DSP frame activity. Spectral Prism, Pitch Bloom, Spectral Delay Canvas,
  Harmonic Mirage, Time Mosaic, and Spectral Nebula keep immediate dry, treat
  analysis delay as wet pre-delay, and contribute zero host PDC. Corrective
  spectral modules and all phase-critical parallel paths retain latency-aligned
  dry and report their processing latency.
- Formant Forge keeps formant shifting independent of source pitch and gates its
  deterministic breath by signal energy. Harmonic Mirage uses phase-continuous
  oscillators and releases uncertain tracking. Chaos Field uses deterministic,
  bounded attractor integration and stable sub-unity feedback.
- Guitar Cabinet uses the shared non-uniform convolution engine with 23 embedded
  root-level cabinet IRs plus user WAV, AIFF, and FLAC loading. File decoding
  and engine preparation stay off the audio thread; bundled selection and user
  paths restore with snapshots. Mono IRs process stereo channels independently,
  stereo IRs preserve encoded differences, and bounded pink-noise-weighted
  calibration keeps IR levels comparable. Mix zero remains immediate dry, and
  telemetry is capture-gated.
- Wormhole Convolution owns two independently loaded IR lanes processed in
  parallel. Each lane applies its persisted start/end crop and reverse transform
  before prepared-engine commit, has an independent wet level, and crossfades
  structural replacements. Shared Low Cut and High Cut affect only the summed
  wet field; Dry remains immediate. File decoding, transforms, construction,
  and destruction stay off the audio thread, and both paths restore with
  snapshots.
- Bitcrusher continuously smooths bit depth, sample clock, drive, reconstruction,
  and output controls. Its Balanced, Truncate, Floor, and Companded quantizers
  morph click-safely; deterministic dither, stable bounded first-order noise
  shaping, pre-hold anti-aliasing, bounded clock jitter, and step slew remain
  allocation-free. Requested Sample Rate is capped by the host rate, Mix zero
  is exact immediate dry, and reset restores the deterministic random sequence.
- Portal Permuter captures tempo-locked input slices in fixed prepared history
  and rearranges them before one focused IR region. Scheduling is deterministic,
  slice transitions are click-safe, feedback is filtered, softly bounded inside
  the loop, and capped at 55%, and IR
  crop/reverse reconstruction stays off the audio thread. Dry remains immediate
  and convolution tails continue across permutation boundaries.
- IR Mosaic constructs four deterministic eight-tile shuffled IRs off the audio
  thread, applies short chunk-edge fades, and energy-normalizes every kernel to
  one standard loudness. Its free Re-shuffle Speed knob or synchronized
  Division selects new spaces through a deterministic sequence. Incoming and
  outgoing convolution tails use a bounded equal-power crossfade, so only one
  path normally runs and two run during transitions. Width scales only the wet
  side field, missing IRs remain immediate dry, and processing reports zero
  host PDC.
- Midnight Current combines smoothed analog softening, bounded saturation, a
  resonant moving low-pass filter, and an eight-stage slow phase network. Its
  signed regeneration remains filtered and sub-unity, Mix zero is exact
  immediate dry with zero host PDC, and Stereo Spread scales only a
  mono-cancelling wet side field. Its response view uses capture-gated DSP
  cutoff, phase, drive, and energy telemetry.
- Auto Wah uses a fast/slow detector pair to emphasize pick attacks while
  retaining level-following musicality. Sensitivity is a visible dB threshold;
  Center Frequency is the midpoint of the bidirectional octave sweep. Low Pass,
  Band Pass, Peak, and Classic Wah responses crossfade without resetting the
  state-variable filter. Drive uses bounded reference-level compensation so its
  upper range adds obvious harmonic push without unbounded output. Mix zero is
  exact immediate dry, and telemetry reports measured level, threshold, pick
  activity, envelope position, and actual filter frequency.
- Resonant Sculpture uses twelve bounded complex modal oscillators with
  damping-aware excitation. String, Glass, Metal, and Membrane changes
  crossfade material ratios, brightness, and decay without clearing modal
  state. Its prepared downsampled autocorrelation tracker follows monophonic
  input fundamentals without delaying audio; Pitch Offset transposes the
  tracked modal root, and uncertain tracking holds the last confident pitch.
  Strike follows measured transients; Bow follows sustained input;
  Mutation and Chaos alter frequency only and cannot increase decay gain.
  Synthetic stereo mode energy cancels on fold-down, Mix zero is exact
  immediate dry, and its deforming structure uses capture-gated mode telemetry.
- Neural Amp Modeler explicitly registers SlimmableContainer parsing before
  model loading and runs those models at full size. Its five active controls
  are Pre Gain, Bass, Mid, Treble, and Post Gain; it has no internal dry/wet
  blend. All tone and gain controls are smoothed and unloaded models are silent.
- Stage Vocal is a zero-latency integrated live strip. Its stereo-linked
  feedback FET stage catches peaks at a fixed 8:1 before a slower optical stage
  applies dual-memory, program-dependent phrase leveling. Each compressor has
  independent Amount, Attack, and Release controls; zero Amount is transparent.
  De-essing remains frequency-selective while both compressors are broadband.
  Low Cut, Presence, Air, and Output retain their exact musician-facing
  meanings, and telemetry reports input/output plus FET, de-ess, and optical
  reduction.
- Velvet Drive is a purely modeled boutique guitar amplifier with no NAM,
  captures, IRs, or external assets. Its cascaded preamp stages, interactive
  tone stack, phase inverter, power-stage feedback, linked supply sag, and
  analytic speaker loads remain distinct parts of the model. Structural input,
  voice, and speaker changes crossfade without clearing state; duplicated mono
  remains channel-matched, and the module reports one fixed oversampling
  latency for every setting.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release \
  --target megaDSPTests megaDSP_VST3 megaDSP_AU megaDSP_CLAP -j 6
ctest --test-dir build --output-on-failure
```

On non-Apple platforms, omit `megaDSP_AU`. Use the existing build and test
tooling; do not introduce a new framework for a local change.

After every code change, rebuild `megaDSP_VST3` and reinstall the freshly built
bundle in the platform's local VST3 plugin directory. Never leave the installed
development copy stale. Do not launch REAPER; if it was already running, ask
for a full restart after installation.

Before installing a release:

1. Run pluginval at strictness 10 with GUI tests skipped.
2. Run pluginval at strictness 5 with editor tests enabled.
3. Verify bundle signatures and version metadata.
4. Install the exact validated VST3 bundle.

Test stereo DSP with duplicated mono, asymmetric stereo, hard-panned input,
mono fold-down, one-channel fallback, automation steps, and supported sample
rates. Test host compatibility with absolute parameter counts and fixed enum
values rather than assertions derived only from mutable constants.

## Editing

- Make surgical changes and follow the surrounding JUCE/C++20 style.
- Prefer existing helpers and fixed prepared storage.
- For every module, prefer fewer high-value controls over exposing every
  implementation parameter. Move related behavior into direct visual
  manipulation, graphs, handles, ranges, meters, and useful automatic behavior
  whenever that is clearer than adding another control.
- Contextual presentation must never mutate a hidden or unavailable parameter.
  Keep its attachment and automation value alive, hide it from keyboard focus,
  and reveal the same retained value when its mode becomes relevant again.
- Make the primary workflow immediately understandable without documentation.
  Keep controls grouped logically, visible when relevant, and supported by live
  visual feedback that shows what the processor is doing.
- Label every control in plain, musically accurate language. Labels, tooltips,
  displayed values, parsing, defaults, automation, DSP behavior, and visual
  readouts must all describe the same quantity.
- Use the control type that matches the interaction: pills or menus for named
  choices, switches for binary state, range handles for minimum/maximum windows,
  level controls for gain, and graph nodes for parameters that are naturally
  edited in two dimensions. Do not use a rotary knob or generic slider merely
  because it is convenient to implement.
- Map controls perceptually and in the expected musical direction. Frequency
  and broad time ranges should normally use logarithmic/exponential scaling;
  gain should use dB where level is being set; pitch should use semitones or
  musical intervals; timing should use ms, seconds, Hz, BPM, or note divisions
  as appropriate; ratios should be shown as ratios. Reserve percentages for
  genuinely dimensionless amounts such as Mix, Width, probability, or Depth.
- Never expose normalized 0–1 values, raw coefficients, sample counts, or other
  DSP implementation values when a musician-facing unit exists. Show Hz/kHz,
  ms/s, dB, semitones, ratios, degrees, or named modes as appropriate, with
  useful precision and ranges that correlate directly with the audible result.
- Ensure drag distance, wheel direction, handle position, and displayed values
  correlate intuitively with the parameter being changed. Double-click reset
  behavior must restore the same musical default documented by the control.
- Keep the EQ knob-free: its enlarged graph owns band frequency/gain/Q,
  topology selection, reset gestures, and Output trim.
- Keep the limiter knob-free: its enlarged graph owns Threshold, Ceiling,
  Release, Lookahead, Auto Gain, resets, and ten-second level/GR feedback.
- Keep Dynamic EQ knob-free: its enlarged graph owns Frequency, Q, Range,
  Threshold, Ratio, timing, shape, detector, sidechain, Listen, Stereo Link,
  exact active response, focused level, and ten-second gain feedback. Band,
  Range, and Threshold dominate; timing/link tracks stay compact, and an
  unavailable external sidechain must be explicit rather than silently active.
- Keep Random Granulizer knob-free: its enlarged grain-stream timeline owns all
  twelve host controls, integrates both size handles into one highlighted
  `MIN`/`MAX` range track, directly edits Capture Range, Stereo Spread, and
  Rhythmic Delay Chance, and displays recent capture position, duration,
  direction, spread, filtering, and progress through lock-free fixed snapshots.
- Keep Vintage Chorus knob-free: its graph owns all twelve controls with four
  prominent model pills, direct Rate/Depth/Delay/Width gestures, grouped
  Density/Character/Mix tracks, compact Phase/Output rails, double-click resets,
  and an animated field showing voice trajectories and Regeneration polarity.
- Compressor Auto Makeup is primary recovery. Control 5 is always the same
  additional `Manual Trim`; show it secondarily when Auto Makeup is off or its
  retained value is nonzero.
- Delay Sync exposes exactly one editable Time or Division control. Its compact
  Movement field owns Rate and Depth together, while Feedback, Tone, and Mix
  remain the principal repeat controls.
- Algorithmic Reverb control 1 is `Room Scale`: Compact/Natural/Large/Vast plus
  the exact 25–200% value. Both reverbs keep independent adjacent Dry/Wet rails
  at 100%/10% defaults; Convolution adds separate Output Trim and direct
  passband handles.
- Stereo Width directly edits Width and Mono Below, uses `L`/`C`/`R` pan
  language, and visually separates Foundation, Dimension Field, and Mono Safe.
- Tremolo mode and Sync context only presentation: Vibrato exposes Pitch Depth
  in cents, Amplitude/Harmonic expose Tremolo Depth, Harmonic alone exposes
  Crossover, and inactive values remain untouched.
- Rotary Speaker makes Speed dominant. Its rotor/microphone graphic owns Rotor
  Balance, Motion, Mic Distance/Spread, and Spin-up; Cabinet Color and Ambience
  retain their exact DSP meanings.
- Keep tab bypass available by double-click and keyboard, with an explicit
  non-color-only bypass label. After the click-safe fade reaches fully dry,
  bypassed slots must suspend module DSP, telemetry, capture, meters, and
  visualization timers while retaining only latency-preserving dry routing.
- Update `README.md` when user-visible processors, controls, compatibility, or
  build instructions change.
- Do not commit generated build artifacts or installed plugin bundles.
- Keep the configured semantic version and short Git commit consistent in the
  editor build label, rotating diagnostics headers, and fatal crash markers.
