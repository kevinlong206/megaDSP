# megaDSP Contributor Guide

## Product

megaDSP is a JUCE 8 modular effects rack delivered as VST3, Audio Unit, and
CLAP. Development is validated in REAPER with VST3 first. The rack has eight
serial slots and twelve fixed generic controls per slot.

## Non-negotiable compatibility rules

- Preserve existing `ModuleType` integer values. Append new module types.
- Current appended values include M/S Decoder 8, Tremolo 9, and Rotary Speaker
  10, Convolution Reverb 11, Dynamic EQ / De-Esser 12, Random Granulizer 13,
  and Vintage Chorus 14.
- Preserve the eight-slot, twelve-control host parameter topology and existing
  parameter IDs.
- Keep state migration compatible with schema 2 through current schema 7.
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
- `src/PluginProcessor.*`: host buses, state, presets, latency reporting, and
  rack topology operations.
- `src/PluginEditor.*`: tabbed rack shell and semantic module controls.
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
- Keep stereo effects mono-compatible. Synthetic side content must cancel on
  fold-down; do not use uncompensated Haas delays for widening.
- Preserve a focused low-frequency image in stereo processors.
- Ping Pong must alternate duplicated mono sources in a stereo host and remain
  well behaved for asymmetric stereo input.
- Avoid hidden broad catches, silent error fallbacks, feedback instability, and
  nonlinear clamps used as substitutes for stable DSP design.
- Use meaningful controls with accurate units and distinct audible roles.
- EQ controls 11 and 12 (zero-based indices 10 and 11) are explicit low HP and
  high LP topology flags in schema 5. Edge lanes only select these modes; cutoff
  remains independent and structural changes stay crossfaded and click-safe.
- Tremolo mode changes must crossfade continuously; rotary horn and drum speeds
  must retain independent physical inertia.
- Saturator gain compensation compares latency-aligned dry and pre-Output wet
  energy. It may attenuate excess nonlinear gain but must never boost quiet
  material or absorb the user's Output trim.
- Convolution Reverb uses JUCE non-uniform partitioned convolution with a
  256-sample head and one shared convolution message queue. IR file work must
  stay off the audio thread; Dry and Wet are independent smoothed linear gains,
  Low Cut/High Cut process only the wet path, and an unloaded or unavailable IR
  produces `Dry * input * Output` with no synthetic wet signal.
- Algorithmic Reverb applies independent smoothed Dry to the original input and
  Wet to the completed reverb field. Damping and input Low Cut/High Cut remain;
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

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release \
  --target megaDSPTests megaDSP_VST3 megaDSP_AU megaDSP_CLAP -j 6
ctest --test-dir build --output-on-failure
```

On non-Apple platforms, omit `megaDSP_AU`. Use the existing build and test
tooling; do not introduce a new framework for a local change.

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
  non-color-only bypass label.
- Update `README.md` when user-visible processors, controls, compatibility, or
  build instructions change.
- Do not commit generated build artifacts or installed plugin bundles.
