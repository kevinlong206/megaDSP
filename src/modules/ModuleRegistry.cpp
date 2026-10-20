#include "ModuleRegistry.h"

#include "../DspModules.h"

#include <cmath>

namespace megadsp
{
namespace
{
using Names = std::array<const char*, controlsPerSlot>;
using Metadata = std::array<ControlMetadata, controlsPerSlot>;

constexpr ControlMetadata unused {
    "-", ControlKind::rotary, false, "", ""
};

constexpr Metadata emptyMetadata {
    unused, unused, unused, unused, unused, unused,
    unused, unused, unused, unused, unused, unused
};
constexpr Metadata eqMetadata {{
    { "Low Frequency", ControlKind::horizontal, false, "Low Band",
      "Sets where the low band acts." },
    { "Low Gain", ControlKind::level, false, "Low Band",
      "Boosts or cuts the low band." },
    { "Low Q", ControlKind::rotary, false, "Low Band",
      "Sets the low band's width." },
    { "Mid Frequency", ControlKind::horizontal, false, "Mid Band",
      "Sets where the mid band acts." },
    { "Mid Gain", ControlKind::level, false, "Mid Band",
      "Boosts or cuts the mid band." },
    { "Mid Q", ControlKind::rotary, false, "Mid Band",
      "Sets the mid band's width." },
    { "High Frequency", ControlKind::horizontal, false, "High Band",
      "Sets where the high band acts." },
    { "High Gain", ControlKind::level, false, "High Band",
      "Boosts or cuts the high band." },
    { "High Q", ControlKind::rotary, false, "High Band",
      "Sets the high band's width." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the EQ." },
    { "Low Shape", ControlKind::choice, false, "Low Band",
      "Selects Bell, Low Shelf, or High Pass." },
    { "High Shape", ControlKind::choice, false, "High Band",
      "Selects Bell, High Shelf, or Low Pass." }
}};
constexpr Metadata compressorMetadata {{
    { "Threshold", ControlKind::level, true, "Compression",
      "Sets where compression begins; drag right for a higher threshold. The thin track shows live input level." },
    { "Ratio", ControlKind::rotary, true, "Compression",
      "Sets how strongly signals above the threshold are reduced." },
    { "Attack", ControlKind::rotary, false, "Timing",
      "Sets how quickly compression reacts." },
    { "Release", ControlKind::rotary, false, "Timing",
      "Sets how quickly compression lets go." },
    { "Knee", ControlKind::rotary, false, "Shape",
      "Softens the transition into compression." },
    { "Manual Trim", ControlKind::level, false, "Recovery",
      "Adds 0 to 24 dB after compression in addition to Auto Makeup." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the compressed and dry signals." },
    { "Detector", ControlKind::toggle, false, "Detector",
      "Chooses the internal signal or external sidechain as detector." },
    { "Auto Makeup", ControlKind::toggle, true, "Recovery",
      "Slowly restores average level lost to compression. It does not change gain reduction; Manual Trim remains an additional output gain." },
    unused, unused, unused
}};
constexpr Metadata saturatorMetadata {{
    { "Drive", ControlKind::rotary, true, "Character",
      "Pushes more level into the saturation curve." },
    { "Tone", ControlKind::horizontal, true, "Character",
      "Darkens or brightens the saturated signal." },
    { "Bias", ControlKind::horizontal, false, "Character",
      "Offsets the saturation curve for asymmetric harmonics." },
    { "Output", ControlKind::level, false, "Output",
      "Trims level after saturation." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the saturated and dry signals." },
    { "Mode", ControlKind::choice, false, "Character",
      "Selects the saturation curve." },
    unused, unused, unused, unused, unused, unused
}};
constexpr Metadata delayMetadata {{
    { "Time", ControlKind::horizontal, true, "Timing",
      "Sets free-running delay time from 1 millisecond to 2 seconds." },
    { "Division", ControlKind::choice, true, "Timing",
      "Selects the tempo-synced note value." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Locks delay time to the host tempo." },
    { "Feedback", ControlKind::horizontal, true, "Repeats",
      "Sets how long repeats continue through the colored feedback loop." },
    { "Style", ControlKind::choice, true, "Character",
      "Selects Studio, Tape, BBD, or Vintage Digital delay behavior." },
    { "Character", ControlKind::horizontal, true, "Character",
      "Increases model-specific drive, bandwidth loss, and age." },
    { "Stereo Mode", ControlKind::choice, true, "Stereo",
      "Selects Mono, Dual, Ping Pong, or Cross routing." },
    { "Spread", ControlKind::horizontal, true, "Stereo",
      "Offsets dual delay times and controls stereo crossfeed width." },
    { "Movement", ControlKind::horizontal, true, "Movement",
      "Adds deterministic model-specific chorus, wow, drift, or clock wander." },
    { "Tone", ControlKind::horizontal, true, "Repeats",
      "Darkens or brightens the evolving feedback path." },
    { "Ducking", ControlKind::horizontal, false, "Output",
      "Moves completed repeats behind the input and releases into the gaps." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends immediate dry signal with the completed delay output." }
}};
constexpr Metadata stereoWidthMetadata {{
    { "Width", ControlKind::horizontal, true, "Stereo Image",
      "Scales the stereo side signal from mono to twice its original width." },
    { "Dimension", ControlKind::rotary, true, "Dimension Field",
      "Creates mono-compatible high-frequency spaciousness with phase decorrelation." },
    { "Mono Below", ControlKind::rotary, true, "Foundation",
      "Narrows low frequencies below this crossover for a focused centre." },
    { "Focus", ControlKind::horizontal, false, "Dimension",
      "Sets the lowest frequency used by the decorrelated dimension field." },
    { "Balance", ControlKind::horizontal, false, "Output",
      "Moves the stereo image left or right with constant-power gain; C is centred." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the widened and original signals." },
    { "Output", ControlKind::level, false, "Output",
      "Adjusts level after stereo processing." },
    { "Mono Safe", ControlKind::toggle, true, "Mono Safe",
      "Dynamically limits added side energy to preserve positive stereo correlation." },
    unused, unused, unused, unused
}};
constexpr Metadata midSideDecoderMetadata {{
    { "Width", ControlKind::horizontal, true, "Decode",
      "Scales the encoded Side channel from mono to its full incoming level." },
    { "Swap Channels", ControlKind::toggle, true, "Routing",
      "Swaps the decoded left and right output channels." },
    { "Mute Sides", ControlKind::toggle, true, "Routing",
      "Mutes the encoded Side signal before decoding." },
    unused, unused, unused, unused, unused, unused, unused, unused, unused
}};
constexpr Metadata tremoloMetadata {{
    { "Mode", ControlKind::choice, true, "Main",
      "Selects amplitude tremolo, split-band harmonic tremolo, or true pitch vibrato." },
    { "Rate", ControlKind::rotary, true, "Timing",
      "Sets the free-running modulation rate." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Locks the modulation rate to the host tempo." },
    { "Division", ControlKind::choice, false, "Timing",
      "Selects the tempo-synced modulation cycle." },
    { "Tremolo Depth", ControlKind::rotary, true, "Modulation",
      "Sets amplitude or harmonic tremolo intensity from 0 to 100%." },
    { "Pitch Depth", ControlKind::rotary, true, "Modulation",
      "Sets vibrato pitch deviation from 0 to 100 cents." },
    { "Shape", ControlKind::horizontal, false, "Modulation",
      "Morphs the LFO from sine through triangle toward a rounded pulse." },
    { "Stereo Phase", ControlKind::horizontal, false, "Stereo",
      "Offsets the right-channel LFO phase." },
    { "Crossover", ControlKind::rotary, false, "Harmonic",
      "Splits the low and high bands in Harmonic mode." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the modulated and dry signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after modulation." },
    unused
}};
constexpr Metadata envelopeModulatorMetadata {{
    { "Mode", ControlKind::choice, true, "Modulation",
      "Selects amplitude tremolo, 700 Hz split-band harmonic tremolo, or pitch vibrato." },
    { "Quiet Speed", ControlKind::horizontal, true, "Level to Speed",
      "Sets modulation speed at and below Quiet Level. Drag the QUIET graph node vertically; this can be faster than Loud Speed for reverse response." },
    { "Loud Speed", ControlKind::horizontal, true, "Level to Speed",
      "Sets modulation speed at and above Loud Level. Drag the LOUD graph node vertically. REVERSE swaps the two speeds." },
    { "Quiet Level", ControlKind::level, true, "Level to Speed",
      "Sets the lower input-level threshold in dBFS. Drag the QUIET graph node horizontally." },
    { "Loud Level", ControlKind::level, true, "Level to Speed",
      "Sets the upper input-level threshold in dBFS. Drag the LOUD graph node horizontally. Crossed host values are ordered without changing automation." },
    { "Attack", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the detector follows a louder pick or phrase, from 1 to 200 ms." },
    { "Release", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the detector follows a decaying note, from 30 ms to 3 seconds." },
    { "Tremolo Depth", ControlKind::horizontal, true, "Modulation",
      "Sets the amount of amplitude or harmonic tremolo. Its retained value is unchanged in Vibrato mode." },
    { "Pitch Depth", ControlKind::horizontal, true, "Modulation",
      "Bounds vibrato pitch deviation from 0 to 100 cents, including while the modulation speed changes. Its retained value is unchanged in tremolo modes." },
    { "Waveform", ControlKind::choice, true, "Modulation",
      "Selects Sine, Triangle, or Rounded Pulse with a continuous waveform crossfade." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends immediate dry audio with the modulation. Zero is exactly dry apart from Output." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after modulation from -12 to +12 dB." }
}};
constexpr Metadata rotarySpeakerMetadata {{
    { "Speed", ControlKind::choice, true, "Main",
      "Selects Brake, slow Chorale, or fast Tremolo rotor speed." },
    { "Drive", ControlKind::rotary, true, "Character",
      "Drives the cabinet preamplifier and speaker model." },
    { "Rotor Balance", ControlKind::horizontal, false, "Rotor",
      "Balances the high-frequency horn and low-frequency drum; Equal is centred." },
    { "Crossover", ControlKind::rotary, false, "Rotor Mix",
      "Sets the split between drum and horn." },
    { "Motion", ControlKind::rotary, false, "Rotor",
      "Scales the exact physical Doppler pitch movement from 0 to 100%." },
    { "Mic Distance", ControlKind::rotary, false, "Microphones",
      "Sets virtual microphone distance from 20 to 200 cm." },
    { "Mic Spread", ControlKind::horizontal, false, "Microphones",
      "Sets the angle between the virtual stereo microphones from 0 to 180 degrees." },
    { "Spin-up", ControlKind::rotary, false, "Rotor",
      "Scales the independent horn and drum acceleration times from 0.40x to 2.50x." },
    { "Cabinet Color", ControlKind::rotary, false, "Cabinet",
      "Sets wooden cabinet resonance and leakage coloration." },
    { "Ambience", ControlKind::rotary, false, "Cabinet",
      "Adds short cabinet-room reflections." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the rotary cabinet and dry signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the rotary cabinet." }
}};
constexpr Metadata limiterMetadata {{
    { "Threshold", ControlKind::level, true, "Level",
      "Sets the level driven into the limiter." },
    { "Ceiling", ControlKind::level, true, "Level",
      "Sets the maximum output level." },
    { "Release", ControlKind::rotary, true, "Timing",
      "Sets how quickly limiting lets go." },
    { "Lookahead", ControlKind::rotary, false, "Timing",
      "Lets the limiter anticipate fast peaks." },
    { "Auto Gain", ControlKind::toggle, true, "Level",
      "Matches bypass loudness by removing threshold drive; Ceiling remains a safety maximum." },
    unused, unused, unused, unused, unused, unused, unused
}};
constexpr Metadata reverbMetadata {{
    { "Decay", ControlKind::rotary, true, "Space",
      "Sets how long the reverb tail lasts." },
    { "Room Scale", ControlKind::rotary, true, "Space",
      "Changes the apparent room scale from Compact to Vast; the readout also shows the exact 25-200% scale." },
    { "Dry", ControlKind::level, true, "Levels",
      "Sets the independent level of the unprocessed signal." },
    { "Wet", ControlKind::level, true, "Levels",
      "Sets the independent level of the completed reverb signal." },
    { "Mode", ControlKind::choice, true, "Space",
      "Selects Hall, Chamber, or Plate character." },
    { "Pre-delay", ControlKind::rotary, false, "Space",
      "Delays the reverb onset while leaving the dry signal immediate." },
    { "Diffusion", ControlKind::rotary, false, "Space",
      "Controls diffusion from sparse to dense." },
    { "Ducking", ControlKind::level, true, "Levels",
      "Lowers the completed wet reverb while input is present, then smoothly restores the tail." },
    { "Width", ControlKind::horizontal, false, "Stereo",
      "Sets wet stereo spread; 100% is natural width." },
    { "Damping", ControlKind::rotary, false, "Decay Color",
      "Makes high frequencies decay faster." },
    { "Low Cut", ControlKind::rotary, false, "Decay Color",
      "Removes low frequencies entering the reverb." },
    { "High Cut", ControlKind::rotary, false, "Decay Color",
      "Limits the brightest frequencies in the tail." }
}};
constexpr Metadata convolutionReverbMetadata {{
    { "Low Cut", ControlKind::rotary, false, "Passband",
      "Removes low frequencies from the convolved signal." },
    { "High Cut", ControlKind::rotary, false, "Passband",
      "Removes high frequencies from the convolved signal." },
    { "Wet", ControlKind::level, true, "Levels",
      "Sets the independent level of the filtered convolved signal." },
    { "Output Trim", ControlKind::level, false, "Output",
      "Adjusts level from -18 to +18 dB after the convolution reverb." },
    { "Dry", ControlKind::level, true, "Levels",
      "Sets the independent level of the latency-aligned original signal." },
    { "Ducking", ControlKind::level, true, "Levels",
      "Lowers the filtered wet convolution while input is present, then smoothly restores the tail." },
    unused, unused, unused, unused, unused, unused
}};
constexpr Metadata dynamicEqualizerMetadata {{
    { "Frequency", ControlKind::horizontal, true, "Band",
      "Sets the focused detector and dynamic filter frequency." },
    { "Q", ControlKind::rotary, true, "Band",
      "Sets the focused band's width or shelf resonance." },
    { "Range", ControlKind::level, true, "Band",
      "Limits dynamic cut below zero or dynamic boost above zero." },
    { "Threshold", ControlKind::level, true, "Dynamics",
      "Sets the focused detector level where dynamic EQ begins." },
    { "Ratio", ControlKind::rotary, true, "Dynamics",
      "Sets how strongly detector level above Threshold drives Range." },
    { "Attack", ControlKind::rotary, false, "Timing",
      "Sets how quickly the dynamic filter engages." },
    { "Release", ControlKind::rotary, false, "Timing",
      "Sets how quickly the dynamic filter returns to flat." },
    { "Shape", ControlKind::choice, true, "Band",
      "Selects Bell, Low Shelf, or High Shelf response." },
    { "Detector", ControlKind::choice, false, "Detector",
      "Selects peak or RMS focused-level detection." },
    { "External Sidechain", ControlKind::toggle, false, "Detector",
      "Uses the external sidechain when connected, otherwise safely falls back to input." },
    { "Listen", ControlKind::toggle, false, "Detector",
      "Auditions the focused detector signal." },
    { "Stereo Link", ControlKind::horizontal, true, "Stereo",
      "Blends independent channel detection into fully linked detection." }
}};
constexpr Metadata randomGranulizerMetadata {{
    { "Voices", ControlKind::horizontal, true, "Density",
      "Sets the maximum number of simultaneous grain voices from 1 to 16." },
    { "Size Minimum", ControlKind::horizontal, true, "Size Window",
      "Sets the shorter edge of the randomized 50 ms to 2 second grain-size window." },
    { "Grain Rate", ControlKind::horizontal, true, "Density",
      "Sets the average launch rate from 0.5 to 30 grains per second." },
    { "Capture Range", ControlKind::horizontal, true, "Capture",
      "Sets capture reach from the present to 6 seconds back in recent input." },
    { "Size Maximum", ControlKind::horizontal, true, "Size Window",
      "Sets the longer edge of the randomized 50 ms to 2 second grain-size window." },
    { "Reverse Chance", ControlKind::horizontal, false, "Character",
      "Sets the probability that a grain reads backward." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Spreads grains across the stereo field with constant power." },
    { "Rhythmic Delay Chance", ControlKind::horizontal, false, "Rhythm",
      "Sets the dimensionless chance of random tempo-quantized pre-launch delay." },
    { "Brightness", ControlKind::horizontal, false, "Character",
      "Sets the brightest randomized per-grain low-pass cutoff from 500 Hz to 20 kHz." },
    { "Regeneration", ControlKind::horizontal, true, "Texture",
      "Returns 0 to 80% controlled wet grain energy to capture history." },
    { "Dry", ControlKind::level, true, "Output",
      "Sets the independent level of the original input from 0 to 100%." },
    { "Wet", ControlKind::level, true, "Output",
      "Sets the independent level of the granular signal from 0 to 100%." }
}};
constexpr Metadata vintageChorusMetadata {{
    { "Model", ControlKind::choice, true, "Model",
      "Selects Vintage BBD, Dimension, Tri-Chorus, or String Ensemble topology." },
    { "Rate", ControlKind::horizontal, true, "Motion",
      "Sets the modulation rate from 0.05 to 8 Hz." },
    { "Depth", ControlKind::horizontal, true, "Motion",
      "Sets delay modulation depth." },
    { "Delay", ControlKind::horizontal, true, "Motion",
      "Sets the central delay from 2 to 30 milliseconds." },
    { "Density", ControlKind::horizontal, true, "Density",
      "Fades between one and six delay voices without changing model identity." },
    { "Width", ControlKind::horizontal, true, "Stereo",
      "Scales the mono-compatible wet side field from mono to double width." },
    { "Regeneration", ControlKind::horizontal, true, "Character",
      "Returns a stable filtered wet signal from -75% to +75% with signed polarity." },
    { "Tone", ControlKind::horizontal, false, "Character",
      "Sets the wet and regeneration bandwidth from 800 Hz to 18 kHz." },
    { "Age", ControlKind::horizontal, false, "Character",
      "Adds signal-gated BBD clock character, compander softness, bandwidth loss, and modulation drift." },
    { "Stereo Phase", ControlKind::horizontal, false, "Stereo",
      "Offsets right-channel modulation from 0 to 180 degrees." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry and chorus with constant power; zero is exactly dry." },
    { "Output", ControlKind::horizontal, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata beatPermuterMetadata {{
    { "Slice", ControlKind::choice, true, "Timing",
      "Sets the tempo-locked length of each captured and emitted slice." },
    { "Chance", ControlKind::horizontal, true, "Timing",
      "Sets the chance that each slice boundary starts a new permutation." },
    { "Pattern", ControlKind::choice, true, "Sequence",
      "Selects Repeat, Reverse, Rotate, or Shuffle behavior." },
    { "History", ControlKind::horizontal, true, "Sequence",
      "Sets how many recent slices the pattern can choose from." },
    { "Length", ControlKind::horizontal, true, "Sequence",
      "Sets how many output steps each triggered permutation lasts." },
    { "Slice Length", ControlKind::horizontal, false, "Shape",
      "Sets the click-free audible portion of every emitted slice." },
    { "Pitch", ControlKind::horizontal, true, "Shape",
      "Transposes permuted audio from -12 to +12 semitones." },
    { "Variation", ControlKind::horizontal, false, "Shape",
      "Adds pattern-dependent movement while preserving the selected timing." },
    { "Stereo", ControlKind::horizontal, false, "Shape",
      "Offsets left and right source slices while preserving mono fold-down." },
    { "Feedback", ControlKind::horizontal, false, "Shape",
      "Returns up to 75% of the filtered permuted signal to capture history." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the latency-matched dry and permuted signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry/wet blend." }
}};
constexpr Metadata spectralPrismMetadata {{
    { "Warp", ControlKind::horizontal, true, "Prism",
      "Compresses frequencies on one side of Pivot while expanding the other." },
    { "Pivot", ControlKind::horizontal, true, "Prism",
      "Sets the anchor frequency for spectral warping." },
    { "Shift", ControlKind::horizontal, true, "Prism",
      "Transposes the remapped spectrum by up to two octaves." },
    { "Smear", ControlKind::horizontal, true, "Texture",
      "Extends spectral magnitudes through time." },
    { "Freeze", ControlKind::toggle, true, "Texture",
      "Holds the current spectral field while phases continue coherently." },
    { "Motion Rate", ControlKind::horizontal, false, "Motion",
      "Sets the free-running spectral motion rate." },
    { "Motion Depth", ControlKind::horizontal, false, "Motion",
      "Modulates Warp around its current setting." },
    { "Phase Diffusion", ControlKind::horizontal, false, "Texture",
      "Spreads partial phases without abrupt random jumps." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Builds a complementary mono-cancelling wet side field." },
    { "Transient Preserve", ControlKind::horizontal, true, "Texture",
      "Protects detected attacks from smear and phase diffusion." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the latency-aligned dry and spectral signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry/wet blend." }
}};
constexpr Metadata resonantMatrixMetadata {{
    { "Tune", ControlKind::horizontal, true, "Harmony",
      "Sets the matrix fundamental from 27.5 to 440 Hz." },
    { "Scale", ControlKind::choice, true, "Harmony",
      "Selects the interval set used by all eight resonators." },
    { "Span", ControlKind::horizontal, true, "Harmony",
      "Distributes the resonators across one to four octaves." },
    { "Topology", ControlKind::choice, true, "Matrix",
      "Selects the signed energy-preserving feedback permutation." },
    { "Decay", ControlKind::horizontal, true, "Tail",
      "Sets the resonant field RT60." },
    { "Damping", ControlKind::horizontal, true, "Tail",
      "Sets the feedback low-pass cutoff." },
    { "Detune", ControlKind::horizontal, false, "Motion",
      "Applies deterministic offsets up to 30 cents." },
    { "Motion Rate", ControlKind::horizontal, false, "Motion",
      "Sets the resonator pitch-motion rate." },
    { "Motion Depth", ControlKind::horizontal, false, "Motion",
      "Sets bounded resonator motion up to 50 cents." },
    { "Width", ControlKind::horizontal, true, "Stereo",
      "Scales the mono-cancelling resonant side field." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry input and resonant field with constant power." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry/wet blend." }
}};
constexpr Metadata wavefoldGardenMetadata {{
    { "Character", ControlKind::choice, true, "Character",
      "Selects Petal, Prism, Chebyshev, or Bloom folding topology." },
    { "Drive", ControlKind::horizontal, true, "Fold",
      "Pushes level into the antialiased fold network." },
    { "Folds", ControlKind::horizontal, true, "Fold",
      "Sets one to eight active folding stages." },
    { "Symmetry", ControlKind::horizontal, true, "Fold",
      "Offsets the transfer curve toward negative or positive folds." },
    { "Shape", ControlKind::horizontal, true, "Fold",
      "Changes curvature within the selected topology." },
    { "Dynamics", ControlKind::horizontal, true, "Envelope",
      "Makes the input envelope close or open fold thresholds." },
    { "Attack", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the fold envelope reacts." },
    { "Release", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the fold envelope lets go." },
    { "Tone", ControlKind::horizontal, true, "Character",
      "Sets the post-fold low-pass cutoff." },
    { "Stereo Bloom", ControlKind::horizontal, false, "Stereo",
      "Adds complementary envelope-driven wet side movement." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry and folded signals with constant power." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry/wet blend." }
}};
constexpr Metadata gateExpanderMetadata {{
    { "Threshold", ControlKind::level, true, "Dynamics",
      "Sets the level where the gate opens." },
    { "Range", ControlKind::level, true, "Dynamics",
      "Sets the maximum attenuation while the gate is closed." },
    { "Attack", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the gate opens." },
    { "Hold", ControlKind::horizontal, false, "Envelope",
      "Keeps the gate open after the signal falls." },
    { "Release", ControlKind::horizontal, false, "Envelope",
      "Sets how quickly the gate closes." },
    { "Hysteresis", ControlKind::horizontal, false, "Dynamics",
      "Separates the opening and closing thresholds to prevent chatter." },
    { "Low Cut", ControlKind::horizontal, false, "Detector",
      "Removes low frequencies from the detector." },
    { "High Cut", ControlKind::horizontal, false, "Detector",
      "Removes high frequencies from the detector." },
    { "External Sidechain", ControlKind::toggle, false, "Detector",
      "Uses the external sidechain when one is connected." },
    { "Listen", ControlKind::toggle, false, "Detector",
      "Auditions the filtered detector signal." },
    { "Stereo Link", ControlKind::horizontal, true, "Stereo",
      "Links left and right detector action." },
    unused
}};
constexpr Metadata transientDesignerMetadata {{
    { "Attack", ControlKind::horizontal, true, "Shape",
      "Cuts or emphasizes detected attacks." },
    { "Sustain", ControlKind::horizontal, true, "Shape",
      "Cuts or emphasizes sustained sound." },
    { "Sensitivity", ControlKind::horizontal, true, "Detector",
      "Sets how readily the processor identifies transients." },
    { "Speed", ControlKind::horizontal, false, "Detector",
      "Sets the separation between attack and sustain envelopes." },
    { "Focus", ControlKind::horizontal, false, "Detector",
      "Sets the frequency that most strongly triggers shaping." },
    { "Clip Guard", ControlKind::toggle, true, "Output",
      "Softly protects boosted transients from overload." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the shaped and original signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after transient shaping." },
    unused, unused, unused, unused
}};
constexpr Metadata multibandCompressorMetadata {{
    { "Low/Mid Crossover", ControlKind::horizontal, true, "Bands",
      "Sets the split between low and mid bands." },
    { "Mid/High Crossover", ControlKind::horizontal, true, "Bands",
      "Sets the split between mid and high bands." },
    { "Low Threshold", ControlKind::level, true, "Thresholds",
      "Sets where low-band compression begins." },
    { "Mid Threshold", ControlKind::level, true, "Thresholds",
      "Sets where mid-band compression begins." },
    { "High Threshold", ControlKind::level, true, "Thresholds",
      "Sets where high-band compression begins." },
    { "Ratio", ControlKind::horizontal, true, "Dynamics",
      "Sets compression strength for all three bands." },
    { "Attack", ControlKind::horizontal, false, "Timing",
      "Sets how quickly compression reacts." },
    { "Release", ControlKind::horizontal, false, "Timing",
      "Sets how quickly compression lets go." },
    { "Auto Makeup", ControlKind::toggle, true, "Recovery",
      "Restores slow average level lost in each active band." },
    { "Stereo Link", ControlKind::horizontal, false, "Stereo",
      "Links left and right band detectors." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the compressed and original signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after multiband compression." }
}};
constexpr Metadata studioPhaserMetadata {{
    { "Stages", ControlKind::choice, true, "Character",
      "Selects the number of all-pass stages and response notches." },
    { "Rate", ControlKind::horizontal, true, "Motion",
      "Sets the free-running sweep rate." },
    { "Sync", ControlKind::toggle, true, "Motion",
      "Locks the sweep to host tempo." },
    { "Division", ControlKind::choice, true, "Motion",
      "Selects the tempo-synced sweep cycle." },
    { "Depth", ControlKind::horizontal, true, "Motion",
      "Sets the amount of phase movement." },
    { "Center", ControlKind::horizontal, true, "Response",
      "Sets the center of the swept notch field." },
    { "Sweep", ControlKind::horizontal, true, "Response",
      "Sets the swept range in octaves." },
    { "Feedback", ControlKind::horizontal, true, "Character",
      "Returns a signed, filtered signal around the all-pass chain." },
    { "Stereo Phase", ControlKind::horizontal, false, "Stereo",
      "Offsets the right-channel sweep phase." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the phased and original signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after phasing." },
    unused
}};
constexpr Metadata studioFlangerMetadata {{
    { "Model", ControlKind::choice, true, "Model",
      "Selects Tape, Through-Zero, Jet, or BBD flanging." },
    { "Rate", ControlKind::horizontal, true, "Motion",
      "Sets the free-running flange rate." },
    { "Sync", ControlKind::toggle, true, "Motion",
      "Locks flange motion to host tempo." },
    { "Division", ControlKind::choice, true, "Motion",
      "Selects the tempo-synced flange cycle." },
    { "Depth", ControlKind::horizontal, true, "Delay",
      "Sets delay-time movement in milliseconds." },
    { "Manual Delay", ControlKind::horizontal, true, "Delay",
      "Sets the center delay of the comb response." },
    { "Feedback", ControlKind::horizontal, true, "Character",
      "Returns a signed, filtered flanged signal." },
    { "Stereo Phase", ControlKind::horizontal, false, "Stereo",
      "Offsets right-channel flange motion." },
    { "Tone", ControlKind::horizontal, false, "Character",
      "Sets the wet and feedback bandwidth." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the flanged and latency-aligned dry signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after flanging." },
    unused
}};
constexpr Metadata diffusionDelayMetadata {{
    { "Time", ControlKind::horizontal, true, "Timing",
      "Sets the free-running primary repeat time." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Locks the primary repeat to host tempo." },
    { "Division", ControlKind::choice, true, "Timing",
      "Selects the tempo-synced repeat value." },
    { "Feedback", ControlKind::horizontal, true, "Repeats",
      "Sets how long the echo cloud continues." },
    { "Diffusion", ControlKind::horizontal, true, "Cloud",
      "Spreads each repeat into a denser echo cloud." },
    { "Movement", ControlKind::horizontal, false, "Cloud",
      "Adds shallow decorrelated motion to the cloud." },
    { "Low Cut", ControlKind::horizontal, false, "Passband",
      "Removes low frequencies from the wet path." },
    { "High Cut", ControlKind::horizontal, false, "Passband",
      "Removes high frequencies from the wet path." },
    { "Width", ControlKind::horizontal, true, "Stereo",
      "Sets cloud width; 100% is natural stereo." },
    { "Ducking", ControlKind::horizontal, false, "Output",
      "Reduces wet echoes while the input is active." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry input and the diffuse echo cloud." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata pitchBloomMetadata {{
    { "Interval", ControlKind::choice, true, "Pitch",
      "Selects the musical interval applied to blooming repeats." },
    { "Fine", ControlKind::horizontal, false, "Pitch",
      "Fine-tunes the shifted repeats in cents." },
    { "Delay", ControlKind::horizontal, true, "Bloom",
      "Sets time before each shifted repeat." },
    { "Feedback", ControlKind::horizontal, true, "Bloom",
      "Sets how many pitch-shifted repeats continue." },
    { "Bloom", ControlKind::horizontal, true, "Bloom",
      "Spreads shifted repeats into a denser field." },
    { "Spread", ControlKind::horizontal, true, "Stereo",
      "Spreads decorrelated pitch voices across stereo." },
    { "Low Cut", ControlKind::horizontal, false, "Passband",
      "Removes low frequencies from shifted repeats." },
    { "High Cut", ControlKind::horizontal, false, "Passband",
      "Removes high frequencies from shifted repeats." },
    { "Ducking", ControlKind::horizontal, false, "Output",
      "Reduces the bloom while the input is active." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and pitch bloom." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused
}};
constexpr Metadata frequencyLabMetadata {{
    { "Root", ControlKind::choice, true, "Harmony",
      "Sets the root note shared by the tuned resonators." },
    { "Harmony", ControlKind::choice, true, "Harmony",
      "Selects the chord voiced across the resonator bank." },
    { "Register", ControlKind::choice, true, "Harmony",
      "Moves the resonator bank between low and airy registers." },
    { "Decay", ControlKind::horizontal, true, "Resonance",
      "Sets how long the tuned resonances ring." },
    { "Tone", ControlKind::horizontal, true, "Resonance",
      "Balances energy between low and high resonator voices." },
    { "Motion Rate", ControlKind::horizontal, false, "Motion",
      "Sets the speed of gentle pitch movement." },
    { "Motion Depth", ControlKind::horizontal, false, "Motion",
      "Sets resonator pitch movement in cents." },
    { "Stereo Spread", ControlKind::horizontal, true, "Stereo",
      "Spreads alternating chord voices across the stereo field." },
    { "Low Cut", ControlKind::horizontal, false, "Passband",
      "Removes low frequencies from the resonator excitation." },
    { "High Cut", ControlKind::horizontal, false, "Passband",
      "Removes high frequencies from the resonator excitation." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry audio with the tuned resonator field." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata spatialOrbitMetadata {{
    { "Path", ControlKind::choice, true, "Orbit",
      "Selects Circle, Figure Eight, Pendulum, or Wander motion." },
    { "Rate", ControlKind::horizontal, true, "Timing",
      "Sets free-running orbit speed." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Locks orbit motion to host tempo." },
    { "Division", ControlKind::choice, true, "Timing",
      "Selects the tempo-synced orbit cycle." },
    { "Azimuth Span", ControlKind::horizontal, true, "Orbit",
      "Sets the horizontal path width in degrees." },
    { "Width", ControlKind::horizontal, true, "Stereo",
      "Sets output stereo width." },
    { "Distance", ControlKind::horizontal, true, "Orbit",
      "Sets virtual source distance from the listener." },
    { "Doppler", ControlKind::horizontal, false, "Motion",
      "Sets physical pitch movement from source motion." },
    { "Air Damping", ControlKind::horizontal, false, "Distance",
      "Darkens the source as distance increases." },
    { "Mono Below", ControlKind::horizontal, false, "Foundation",
      "Centers low frequencies below this crossover." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and orbiting signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after spatial motion." }
}};
constexpr Metadata signalDecayMetadata {{
    { "Resolution", ControlKind::horizontal, true, "Digital",
      "Sets quantizer resolution in bits." },
    { "Sample Rate", ControlKind::horizontal, true, "Digital",
      "Sets the effective sample-and-hold rate." },
    { "Jitter", ControlKind::horizontal, false, "Digital",
      "Adds bounded clock-timing variation." },
    { "Dropouts", ControlKind::horizontal, true, "Wear",
      "Sets the rate and depth of smoothly windowed signal losses." },
    { "Bandwidth", ControlKind::horizontal, true, "Wear",
      "Sets the degraded signal bandwidth." },
    { "Noise", ControlKind::level, false, "Wear",
      "Sets deterministic noise level in dBFS." },
    { "Wow", ControlKind::horizontal, false, "Motion",
      "Adds slow pitch and timing drift." },
    { "Flutter", ControlKind::horizontal, false, "Motion",
      "Adds faster pitch and timing instability." },
    { "Stereo Wear", ControlKind::horizontal, false, "Stereo",
      "Introduces bounded differences between channel wear." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and degraded signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after degradation." },
    unused
}};
constexpr Metadata analogTapeMetadata {{
    { "Machine", ControlKind::choice, true, "Machine",
      "Selects Worn Cassette, Consumer Reel, Ampex-Style Deck, or Studer-Style Deck character." },
    { "Input", ControlKind::level, true, "Level",
      "Sets the level printed to the tape before saturation and compression." },
    { "Drive", ControlKind::horizontal, true, "Character",
      "Adds level-compensated saturation, density, and transient softening." },
    { "Bias", ControlKind::horizontal, false, "Character",
      "Moves from brighter underbias to darker overbias." },
    { "Tape Speed", ControlKind::choice, true, "Machine",
      "Selects transport speed from 3.75 to 30 ips." },
    { "Head Bump", ControlKind::horizontal, false, "Character",
      "Sets the playback-head low-frequency resonance." },
    { "Wow", ControlKind::horizontal, false, "Transport",
      "Sets slow transport pitch drift." },
    { "Flutter", ControlKind::horizontal, false, "Transport",
      "Sets fast transport pitch variation." },
    { "Wear", ControlKind::horizontal, false, "Transport",
      "Adds hysteresis smear and gentle dropout modulation." },
    { "Noise", ControlKind::horizontal, false, "Character",
      "Sets energy-gated tape hiss." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and tape-processed signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata resonanceTamerMetadata {{
    { "Reduction", ControlKind::level, true, "Suppression",
      "Sets the maximum adaptive resonance reduction." },
    { "Selectivity", ControlKind::choice, true, "Detection",
      "Chooses broad, focused, or surgical resonance detection." },
    { "Reaction", ControlKind::choice, false, "Detection",
      "Sets how quickly detected resonances are followed." },
    { "Tone Bias", ControlKind::horizontal, false, "Detection",
      "Biases sensitivity across the spectrum in dB per octave." },
    { "Low Limit", ControlKind::horizontal, false, "Range",
      "Sets the lowest frequency eligible for suppression." },
    { "High Limit", ControlKind::horizontal, false, "Range",
      "Sets the highest frequency eligible for suppression." },
    { "Transient Preserve", ControlKind::horizontal, true, "Protection",
      "Relaxes suppression during measured broadband transients." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and resonance-reduced signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused, unused, unused
}};
constexpr Metadata spectralBalanceMetadata {{
    { "Contour", ControlKind::choice, true, "Target",
      "Selects the broad musical balance target." },
    { "Amount", ControlKind::horizontal, true, "Correction",
      "Sets the strength of adaptive tonal correction." },
    { "Low Weight", ControlKind::level, false, "Target",
      "Adjusts the low-frequency target in dB." },
    { "Presence", ControlKind::level, false, "Target",
      "Adjusts the presence target in dB." },
    { "Air", ControlKind::level, false, "Target",
      "Adjusts the air-band target in dB." },
    { "Adaptation", ControlKind::horizontal, true, "Timing",
      "Sets how quickly the long-term balance follows the source." },
    { "Detail", ControlKind::choice, false, "Correction",
      "Chooses smooth, balanced, or detailed correction." },
    { "Transient Preserve", ControlKind::horizontal, false, "Protection",
      "Protects measured broadband transients from correction." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts the corrected output level." },
    unused, unused, unused
}};
constexpr Metadata phaseCoherenceMetadata {{
    { "Range", ControlKind::choice, true, "Analysis",
      "Chooses the frequency range used for phase repair." },
    { "Crossover", ControlKind::horizontal, true, "Analysis",
      "Sets the upper edge of focused low-frequency repair." },
    { "Correction", ControlKind::horizontal, true, "Repair",
      "Sets the amount of confident delay and phase correction." },
    { "Max Alignment", ControlKind::horizontal, false, "Repair",
      "Limits interchannel time alignment in milliseconds." },
    { "Phase Rotation", ControlKind::horizontal, false, "Repair",
      "Limits all-pass phase rotation in degrees." },
    { "Stereo Preserve", ControlKind::horizontal, true, "Stereo",
      "Retains measured stereo difference during correction." },
    { "Mono Below", ControlKind::horizontal, false, "Stereo",
      "Sets the frequency below which output is explicitly mono." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after phase repair." },
    unused, unused, unused, unused
}};
constexpr Metadata loudnessRiderMetadata {{
    { "Target", ControlKind::level, true, "Loudness",
      "Sets the momentary loudness target in LUFS." },
    { "Range", ControlKind::horizontal, true, "Ride",
      "Limits automatic fader movement in dB." },
    { "Window", ControlKind::choice, false, "Loudness",
      "Chooses the loudness observation window." },
    { "Reaction", ControlKind::horizontal, true, "Ride",
      "Sets how quickly ride gain follows programme changes." },
    { "Lookahead", ControlKind::horizontal, false, "Ride",
      "Sets predictive delay while preserving fixed host latency." },
    { "Transient Hold", ControlKind::horizontal, false, "Protection",
      "Delays ride changes around short transients." },
    { "Crest Preserve", ControlKind::horizontal, true, "Protection",
      "Protects measured peak-to-average contrast." },
    { "Gate", ControlKind::level, false, "Loudness",
      "Stops upward riding below this loudness." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after automatic riding." },
    unused, unused, unused
}};
constexpr Metadata adaptiveClipperMetadata {{
    { "Drive", ControlKind::level, true, "Clipping",
      "Sets level into the adaptive clipping stage." },
    { "Ceiling", ControlKind::level, true, "Clipping",
      "Sets the final true-peak ceiling in dBTP." },
    { "Style", ControlKind::choice, true, "Character",
      "Chooses clean, punch, or dense adaptation." },
    { "Shape", ControlKind::horizontal, false, "Character",
      "Moves from a soft knee to hard clipping." },
    { "Transient Bias", ControlKind::horizontal, true, "Detection",
      "Prioritizes transient peaks or sustained body." },
    { "Release", ControlKind::horizontal, false, "Detection",
      "Sets recovery after clipping activity." },
    { "Stereo Link", ControlKind::horizontal, false, "Stereo",
      "Links clipping adaptation across channels." },
    { "Oversampling", ControlKind::choice, false, "Quality",
      "Selects 2x, 4x, or 8x oversampling at fixed latency." },
    { "Auto Trim", ControlKind::toggle, true, "Output",
      "Removes measured loudness added by Drive without boosting." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and clipped audio." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused
}};
constexpr Metadata spectralDelayCanvasMetadata {{
    { "Sync", ControlKind::toggle, true, "Timing",
      "Switches between milliseconds and musical divisions." },
    { "Base Time", ControlKind::horizontal, true, "Timing",
      "Sets the base spectral delay in milliseconds." },
    { "Division", ControlKind::choice, true, "Timing",
      "Sets the tempo-synced base delay." },
    { "Low Delay", ControlKind::horizontal, true, "Canvas",
      "Scales delay time for low frequencies." },
    { "Mid Delay", ControlKind::horizontal, true, "Canvas",
      "Scales delay time for mid frequencies." },
    { "High Delay", ControlKind::horizontal, true, "Canvas",
      "Scales delay time for high frequencies." },
    { "Feedback", ControlKind::horizontal, true, "Regeneration",
      "Returns delayed spectral energy to history." },
    { "Diffusion", ControlKind::horizontal, false, "Texture",
      "Spreads neighboring spectral delay times." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Offsets spectral history between channels." },
    { "Freeze", ControlKind::toggle, false, "History",
      "Stops writing new frames while history continues." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and delayed spectra." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata formantForgeMetadata {{
    { "Model", ControlKind::choice, true, "Tract",
      "Chooses Human, Tube, Creature, or Metallic resonances." },
    { "Vowel X", ControlKind::horizontal, true, "Vowel",
      "Moves across the A to E vowel axis." },
    { "Vowel Y", ControlKind::horizontal, true, "Vowel",
      "Moves through the I, O, and U vowel field." },
    { "Formant Shift", ControlKind::horizontal, true, "Tract",
      "Moves resonances in semitones without shifting source pitch." },
    { "Resonance", ControlKind::horizontal, true, "Tract",
      "Sets formant emphasis and bandwidth." },
    { "Breath", ControlKind::horizontal, false, "Excitation",
      "Adds deterministic signal-gated breath excitation." },
    { "Motion Rate", ControlKind::horizontal, false, "Motion",
      "Sets vowel-field motion in hertz." },
    { "Motion Depth", ControlKind::horizontal, false, "Motion",
      "Sets motion distance through the vowel field." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Offsets formant motion between channels." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry and formant-shaped audio." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused
}};
constexpr Metadata harmonicMirageMetadata {{
    { "Mode", ControlKind::choice, true, "Resynthesis",
      "Chooses Harmonic, Subharmonic, Hollow, or Metallic partials." },
    { "Tracking", ControlKind::choice, true, "Analysis",
      "Sets loose, musical, or tight partial tracking." },
    { "Partials", ControlKind::horizontal, true, "Resynthesis",
      "Sets the number of generated partials." },
    { "Even / Odd", ControlKind::horizontal, true, "Resynthesis",
      "Balances even and odd harmonic families." },
    { "Inharmonicity", ControlKind::horizontal, false, "Resynthesis",
      "Offsets upper partials from the harmonic series." },
    { "Fine Drift", ControlKind::horizontal, false, "Motion",
      "Sets deterministic pitch drift in cents." },
    { "Response", ControlKind::horizontal, false, "Analysis",
      "Sets partial lock and release timing." },
    { "Transient Preserve", ControlKind::horizontal, true, "Protection",
      "Keeps unpitched transients on the aligned source path." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Places generated partials across stereo." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends aligned source and generated partials." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused
}};
constexpr Metadata chaosFieldMetadata {{
    { "Attractor", ControlKind::choice, true, "Motion",
      "Chooses Lorenz, Rossler, or Double Pendulum motion." },
    { "Rate", ControlKind::horizontal, true, "Timing",
      "Sets free attractor speed in hertz." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Switches attractor speed to musical divisions." },
    { "Division", ControlKind::choice, true, "Timing",
      "Sets tempo-synced attractor speed." },
    { "Depth", ControlKind::horizontal, true, "Motion",
      "Scales filter, delay, and pan modulation together." },
    { "Filter Center", ControlKind::horizontal, true, "Filter",
      "Sets the center of attractor-driven filtering." },
    { "Delay Center", ControlKind::horizontal, false, "Delay",
      "Sets the center fractional delay in milliseconds." },
    { "Feedback", ControlKind::horizontal, true, "Delay",
      "Sets signed filtered delay feedback." },
    { "Pan Orbit", ControlKind::horizontal, false, "Stereo",
      "Sets attractor-driven pan movement." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Offsets motion between channels." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry and attractor-modulated audio." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." }
}};
constexpr Metadata timeMosaicMetadata {{
    { "History", ControlKind::horizontal, true, "History",
      "Sets the available spectral history in seconds." },
    { "Tile Width", ControlKind::horizontal, true, "Tiles",
      "Sets spectral tile bandwidth in octaves." },
    { "Tile Time", ControlKind::horizontal, true, "Tiles",
      "Sets how long each history assignment is held." },
    { "Age", ControlKind::horizontal, true, "History",
      "Biases tiles from present toward older history." },
    { "Motion", ControlKind::horizontal, false, "Tiles",
      "Sets how strongly tile ages evolve." },
    { "Coherence", ControlKind::horizontal, true, "Tiles",
      "Links neighboring tile history assignments." },
    { "Pitch Drift", ControlKind::horizontal, false, "Texture",
      "Sets upward spectral pitch drift in cents." },
    { "Freeze", ControlKind::toggle, false, "History",
      "Stops writing new spectral frames." },
    { "Stereo Spread", ControlKind::horizontal, false, "Stereo",
      "Offsets tile ages between channels." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends aligned source and reconstructed mosaic." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the blend." },
    unused
}};
constexpr Metadata guitarCabinetMetadata {{
    { "Cabinet IR", ControlKind::choice, true, "Impulse",
      "Selects one of the bundled cabinet impulse responses." },
    { "Low Cut", ControlKind::horizontal, true, "Passband",
      "Removes excess low end from the cabinet response." },
    { "High Cut", ControlKind::horizontal, true, "Passband",
      "Softens the upper edge of the cabinet response." },
    { "Body", ControlKind::horizontal, true, "Tone",
      "Adjusts low-mid cabinet weight around 180 Hz." },
    { "Presence", ControlKind::horizontal, true, "Tone",
      "Adjusts upper-mid definition above 2.2 kHz." },
    { "Drive", ControlKind::level, true, "Input",
      "Adds bounded speaker-style input drive before the cabinet IR." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the immediate source with the cabinet signal." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final cabinet level." },
    unused, unused, unused, unused
}};
constexpr Metadata chordLoomMetadata {{
    { "Sensitivity", ControlKind::horizontal, true, "Capture",
      "Higher sensitivity lowers the visible input threshold and requires less "
      "attack contrast before a new strum replaces the held chord." },
    { "Capture", ControlKind::horizontal, true, "Capture",
      "Sets how much of each new chord is gathered for the sustain texture." },
    { "Transition", ControlKind::horizontal, true, "Capture",
      "Sets how smoothly the previous chord gives way to the new chord." },
    { "Texture", ControlKind::horizontal, true, "Sustain",
      "Moves from long smooth grains to a more animated woven texture." },
    { "Motion", ControlKind::horizontal, false, "Sustain",
      "Adds subtle independent pitch drift to the sustaining grains." },
    { "Octave Bloom", ControlKind::horizontal, false, "Sustain",
      "Blends a soft octave layer into the held chord." },
    { "Stereo Spread", ControlKind::horizontal, true, "Stereo",
      "Spreads overlapping grains while preserving mono fold-down." },
    { "Low Cut", ControlKind::horizontal, false, "Passband",
      "Removes low frequencies from the sustained layer." },
    { "High Cut", ControlKind::horizontal, false, "Passband",
      "Removes high frequencies from the sustained layer." },
    { "Dry", ControlKind::level, true, "Levels",
      "Sets the independent live-input level." },
    { "Sustain", ControlKind::level, true, "Levels",
      "Sets the independent held-chord level." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry and sustained layers." }
}};
constexpr Metadata volumeSwellMetadata {{
    { "Sensitivity", ControlKind::horizontal, true, "Detector",
      "Higher sensitivity lowers both thresholds for detecting a new chord." },
    { "Swell Time", ControlKind::horizontal, true, "Envelope",
      "Sets how long the volume takes to rise after each detected chord." },
    { "Curve", ControlKind::horizontal, true, "Envelope",
      "Shapes the rise from a fast start through linear to a slow start." },
    { "Floor", ControlKind::level, true, "Envelope",
      "Sets how quiet the signal becomes at the start of each swell." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the latency-aligned dry signal with the automatic swell." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the swell envelope." },
    unused, unused, unused, unused, unused, unused
}};
constexpr Metadata spectralNebulaMetadata {{
    { "Smear", ControlKind::horizontal, true, "Cloud",
      "Slows spectral absorption so notes bloom and morph into one another." },
    { "Decay", ControlKind::horizontal, true, "Cloud",
      "Sets how long absorbed spectral energy remains after the input stops." },
    { "Drift", ControlKind::horizontal, true, "Motion",
      "Adds slow bounded pitch movement to the synthesized cloud." },
    { "Shimmer", ControlKind::horizontal, true, "Harmonics",
      "Adds a soft octave-above image derived from stored spectral energy." },
    { "Focus", ControlKind::horizontal, true, "Texture",
      "Moves from airy noise through a balanced cloud to stable tonal partials." },
    { "Freeze", ControlKind::toggle, true, "Cloud",
      "Stops spectral-state updates while phase motion continues smoothly." },
    { "Width", ControlKind::horizontal, true, "Stereo",
      "Scales only the cloud's side domain, preserving mono fold-down." },
    { "Low Cut", ControlKind::horizontal, false, "Passband",
      "Removes low-frequency energy from the synthesized cloud." },
    { "High Cut", ControlKind::horizontal, false, "Passband",
      "Removes high-frequency energy from the synthesized cloud." },
    { "Dry", ControlKind::level, true, "Levels",
      "Sets the independent immediate input level." },
    { "Cloud", ControlKind::level, true, "Levels",
      "Sets the independent synthesized cloud level." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry and cloud paths." }
}};
constexpr Metadata neuralAmpModelerMetadata {{
    { "Pre Gain", ControlKind::level, true, "Gain",
      "Adjusts level entering the neural amp model." },
    { "Bass", ControlKind::rotary, true, "Tone Stack",
      "Cuts or boosts the guitar tone stack below 120 Hz." },
    { "Mid", ControlKind::rotary, true, "Tone Stack",
      "Cuts or boosts the guitar tone stack around 750 Hz." },
    { "Treble", ControlKind::rotary, true, "Tone Stack",
      "Cuts or boosts the guitar tone stack above 3.2 kHz." },
    { "Post Gain", ControlKind::level, true, "Gain",
      "Adjusts level after the neural model and tone stack." },
    unused, unused, unused, unused, unused, unused, unused
}};
constexpr Metadata stageVocalMetadata {{
    { "Low Cut", ControlKind::horizontal, true, "Cleanup",
      "Removes stage rumble and proximity buildup below the selected frequency." },
    { "Fast Compression", ControlKind::level, true, "Fast FET",
      "Drives the feedback FET stage harder to catch vocal peaks." },
    { "Fast Attack", ControlKind::horizontal, true, "Fast FET",
      "Sets the FET peak catcher's sub-millisecond attack." },
    { "Fast Release", ControlKind::horizontal, false, "Fast FET",
      "Sets how quickly the FET stage recovers after peaks." },
    { "Slow Compression", ControlKind::level, true, "Slow Optical",
      "Drives the optical stage harder for smooth phrase leveling." },
    { "Slow Attack", ControlKind::horizontal, true, "Slow Optical",
      "Sets how gradually the optical cell applies leveling." },
    { "Slow Release", ControlKind::horizontal, false, "Slow Optical",
      "Sets the base recovery time before program-dependent extension." },
    { "De-Ess Frequency", ControlKind::horizontal, true, "De-Esser",
      "Sets the lower edge of the sibilance band." },
    { "De-Ess Amount", ControlKind::level, true, "De-Esser",
      "Sets the maximum linked high-band attenuation for sibilant consonants." },
    { "Presence", ControlKind::level, true, "Tone",
      "Cuts or boosts vocal articulation between roughly 1.2 and 5 kHz." },
    { "Air", ControlKind::level, true, "Tone",
      "Cuts or boosts the open high-frequency band above roughly 8 kHz." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final level after the complete vocal strip." }
}};
constexpr Metadata vinylSimulatorMetadata {{
    { "Condition", ControlKind::horizontal, true, "Record",
      "Moves from a clean pressing to worn grooves, softer transients, and darker playback." },
    { "Crackle", ControlKind::horizontal, true, "Surface",
      "Sets the level of larger groove clicks and pops." },
    { "Dust", ControlKind::horizontal, true, "Surface",
      "Sets the density of fine dust ticks." },
    { "Scratches", ControlKind::horizontal, true, "Surface",
      "Sets the likelihood and level of short repeating scratch bursts." },
    { "Surface Noise", ControlKind::level, false, "Noise Floor",
      "Sets the continuous shaped groove hiss from -90 to -34 dB." },
    { "Rumble", ControlKind::level, false, "Mechanics",
      "Sets low-frequency platter and bearing noise from -90 to -36 dB." },
    { "Wow", ControlKind::horizontal, true, "Transport",
      "Sets slow platter-speed pitch movement up to 3.8 ms." },
    { "Flutter", ControlKind::horizontal, true, "Transport",
      "Sets faster stylus and motor pitch movement up to 0.65 ms." },
    { "Tone", ControlKind::horizontal, true, "Playback",
      "Sets the playback bandwidth from dark vintage to full range." },
    { "Stereo Spread", ControlKind::horizontal, true, "Stereo",
      "Spreads synthetic surface texture while preserving mono fold-down." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned clean and vinyl playback paths." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the complete vinyl simulation." }
}};
constexpr Metadata optoLevelerMetadata {{
    { "Peak Reduction", ControlKind::level, true, "Leveling",
      "Sets how deeply the program-dependent optical cell reduces level." },
    { "Gain", ControlKind::level, true, "Output",
      "Sets the optical leveler's makeup amplifier gain." },
    { "Mode", ControlKind::choice, true, "Leveling",
      "Selects the gentler Compress curve or firmer Limit curve." },
    { "HF Emphasis", ControlKind::horizontal, false, "Detector",
      "Makes high-frequency peaks drive the optical detector more strongly." },
    { "Sidechain Low Cut", ControlKind::horizontal, false, "Detector",
      "Reduces low-frequency influence on gain reduction." },
    { "Cell Age", ControlKind::horizontal, false, "Optical Cell",
      "Moves from a fresh cell to slower, more persistent optical memory." },
    { "Stereo Link", ControlKind::horizontal, true, "Stereo",
      "Links channel detection to preserve the stereo image." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and leveled signals." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final level after the analog output stage." },
    unused, unused, unused
}};
constexpr Metadata fetLimiterMetadata {{
    { "Input", ControlKind::level, true, "Gain",
      "Drives the signal into the feedback FET gain-reduction circuit." },
    { "Output", ControlKind::level, true, "Gain",
      "Sets output-amplifier gain before the final trim." },
    { "Attack", ControlKind::horizontal, true, "Timing",
      "Sets the FET detector attack from 20 to 800 microseconds." },
    { "Release", ControlKind::horizontal, true, "Timing",
      "Sets gain-reduction recovery from 50 to 1100 milliseconds." },
    { "Ratio", ControlKind::choice, true, "Compression",
      "Selects 4:1, 8:1, 12:1, 20:1, or the aggressive ALL mode." },
    { "Detector Low Cut", ControlKind::horizontal, false, "Detector",
      "Reduces low-frequency influence on the feedback detector." },
    { "Stereo Link", ControlKind::horizontal, true, "Stereo",
      "Links channel detection to preserve the stereo image." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends latency-aligned dry and limited signals." },
    { "Output Trim", ControlKind::level, true, "Output",
      "Adjusts final level after the modeled transformer." },
    unused, unused, unused
}};
constexpr Metadata programEqualizerMetadata {{
    { "Low Frequency", ControlKind::choice, true, "Low Band",
      "Selects the passive low boost and attenuation frequency." },
    { "Low Boost", ControlKind::level, true, "Low Band",
      "Boosts the selected low frequency through the passive network." },
    { "Low Attenuation", ControlKind::level, true, "Low Band",
      "Attenuates around the selected low frequency with a different curve." },
    { "High Boost Frequency", ControlKind::choice, true, "High Boost",
      "Selects the center frequency of the broad high boost." },
    { "High Boost", ControlKind::level, true, "High Boost",
      "Boosts the selected high-frequency region." },
    { "Bandwidth", ControlKind::horizontal, true, "High Boost",
      "Moves the high boost from broad to sharp." },
    { "High Attenuation Frequency", ControlKind::choice, true, "High Cut",
      "Selects the passive high-attenuation turnover." },
    { "High Attenuation", ControlKind::level, true, "High Cut",
      "Attenuates high frequencies above the selected turnover." },
    { "Drive", ControlKind::level, true, "Amplifier",
      "Drives the makeup amplifier and transformer coloration." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final level after the makeup amplifier." },
    unused, unused
}};
constexpr Metadata studioPlateMetadata {{
    { "Plate Size", ControlKind::choice, true, "Plate",
      "Selects Compact, Standard, or Large plate geometry." },
    { "Decay", ControlKind::horizontal, true, "Plate",
      "Sets the mid-band reverberation time." },
    { "Damping", ControlKind::horizontal, true, "Plate",
      "Shortens high-frequency decay as the plate loses energy." },
    { "Pre-Delay", ControlKind::horizontal, false, "Excitation",
      "Delays plate excitation while leaving Dry immediate." },
    { "Low Cut", ControlKind::horizontal, false, "Excitation",
      "Removes low frequencies before they excite the plate." },
    { "High Cut", ControlKind::horizontal, false, "Excitation",
      "Limits high frequencies before they excite the plate." },
    { "Pickup Spread", ControlKind::horizontal, true, "Stereo Pickups",
      "Moves the virtual left and right pickups apart across the plate." },
    { "Width", ControlKind::horizontal, true, "Stereo Pickups",
      "Scales only the mono-cancelling wet side field." },
    { "Ducking", ControlKind::level, false, "Wet Field",
      "Attenuates only the completed wet field while input is active." },
    { "Dry", ControlKind::level, true, "Output",
      "Sets immediate dry level independently." },
    { "Wet", ControlKind::level, true, "Output",
      "Sets completed plate level independently." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final level after the dry and wet paths." }
}};
constexpr Metadata velvetDriveMetadata {{
    { "Input", ControlKind::choice, true, "Front End",
      "Selects the normal tube input or a brighter, hotter FET-style input." },
    { "Voice", ControlKind::choice, true, "Architecture",
      "Selects smooth classic overdrive, post-drive modern mids, or high-headroom clean response." },
    { "Drive", ControlKind::level, true, "Preamp",
      "Sets gain through the cascaded preamp stages." },
    { "Ratio", ControlKind::horizontal, true, "Preamp",
      "Blends the clean-stage voice into the additional overdrive cascade." },
    { "Level", ControlKind::level, true, "Preamp",
      "Sets overdrive-channel level before the phase inverter." },
    { "Bass", ControlKind::rotary, true, "Tone Stack",
      "Adjusts the interactive passive low-frequency response." },
    { "Mid", ControlKind::rotary, true, "Tone Stack",
      "Adjusts the interactive passive midrange response." },
    { "Treble", ControlKind::rotary, true, "Tone Stack",
      "Adjusts the interactive passive high-frequency response." },
    { "Presence", ControlKind::rotary, true, "Power Amp",
      "Reduces high-frequency negative feedback for more open attack and upper presence." },
    { "Feel", ControlKind::horizontal, true, "Power Amp",
      "Sets linked power-supply sag, compression, and recovery bloom." },
    { "Speaker", ControlKind::choice, true, "Speaker Load",
      "Selects direct output or one of three analytic high-headroom speaker-load responses." },
    { "Master", ControlKind::level, true, "Output",
      "Drives the modeled power stage and sets the final calibrated level." }
}};
constexpr Metadata wormholeConvolutionMetadata {{
    { "A Start", ControlKind::horizontal, true, "Impulse A",
      "Sets the beginning of the crop window for impulse A." },
    { "A End", ControlKind::horizontal, true, "Impulse A",
      "Sets the end of the crop window for impulse A." },
    { "Reverse A", ControlKind::toggle, true, "Impulse A",
      "Reverses the cropped impulse A." },
    { "Level A", ControlKind::level, true, "Impulse A",
      "Sets the parallel wet level of impulse A." },
    { "B Start", ControlKind::horizontal, true, "Impulse B",
      "Sets the beginning of the crop window for impulse B." },
    { "B End", ControlKind::horizontal, true, "Impulse B",
      "Sets the end of the crop window for impulse B." },
    { "Reverse B", ControlKind::toggle, true, "Impulse B",
      "Reverses the cropped impulse B." },
    { "Level B", ControlKind::level, true, "Impulse B",
      "Sets the parallel wet level of impulse B." },
    { "Low Cut", ControlKind::horizontal, false, "Wet Passband",
      "Removes low frequencies from the combined wet field." },
    { "High Cut", ControlKind::horizontal, false, "Wet Passband",
      "Softens high frequencies in the combined wet field." },
    { "Dry", ControlKind::level, true, "Output",
      "Sets the immediate dry signal level." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final output level." }
}};
constexpr Metadata bitcrusherMetadata {{
    { "Bit Depth", ControlKind::horizontal, true, "Resolution",
      "Sets the continuously variable digital word length." },
    { "Sample Rate", ControlKind::horizontal, true, "Clock",
      "Sets a 250 Hz to 768 kHz sample-and-hold clock request, capped by the host rate." },
    { "Quantizer", ControlKind::choice, true, "Resolution",
      "Chooses balanced rounding, truncation, downward rounding, or companded detail." },
    { "Dither", ControlKind::horizontal, false, "Quantization",
      "Adds deterministic triangular dither scaled to the active bit depth." },
    { "Noise Shape", ControlKind::horizontal, false, "Quantization",
      "Moves quantization error upward using stable first-order feedback." },
    { "Anti-Alias", ControlKind::horizontal, true, "Clock",
      "Filters content above the reduced sample rate before sample-and-hold." },
    { "Jitter", ControlKind::horizontal, false, "Clock",
      "Adds bounded deterministic variation to sample-clock intervals." },
    { "Step Slew", ControlKind::horizontal, false, "Reconstruction",
      "Rounds the hard edges between held samples." },
    { "Grid Bias", ControlKind::horizontal, false, "Quantization",
      "Offsets the quantization grid by up to one least-significant bit." },
    { "Drive", ControlKind::level, true, "Front End",
      "Pushes the signal into bounded pre-quantizer saturation." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the immediate dry signal with the crushed signal." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final output level." }
}};
constexpr Metadata portalPermuterMetadata {{
    { "Slice", ControlKind::choice, true, "Capture",
      "Sets the tempo-locked input slice size." },
    { "Chance", ControlKind::horizontal, true, "Capture",
      "Sets the probability that a recent slice enters the portal." },
    { "Pattern", ControlKind::choice, true, "Permutation",
      "Chooses how recent slices are repeated, reversed, rotated, or shuffled." },
    { "History", ControlKind::horizontal, false, "Permutation",
      "Sets how many recent slices may be selected." },
    { "Length", ControlKind::horizontal, true, "Permutation",
      "Sets the number of steps in each permutation event." },
    { "Slice Length", ControlKind::horizontal, false, "Permutation",
      "Shortens each selected slice before it excites the impulse response." },
    { "Pitch", ControlKind::horizontal, true, "Permutation",
      "Transposes selected slices before convolution." },
    { "IR Position", ControlKind::horizontal, true, "Impulse",
      "Moves a focused crop window through the loaded impulse response." },
    { "Reverse IR", ControlKind::toggle, true, "Impulse",
      "Reverses the selected impulse-response region." },
    { "Feedback", ControlKind::horizontal, true, "Portal",
      "Returns a softly bounded filtered convolved signal to the portal excitation." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends immediate dry signal with the convolved permutation field." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final output level." }
}};
constexpr Metadata irMosaicMetadata {{
    { "Shuffle Style", ControlKind::choice, true, "Mosaic",
      "Chooses the deterministic sequence used to select each newly shuffled space." },
    { "Re-shuffle Speed", ControlKind::rotary, true, "Mosaic",
      "Sets how often a newly shuffled impulse-response space is selected." },
    { "Sync", ControlKind::toggle, false, "Motion",
      "Uses a tempo division instead of the free-running re-shuffle speed." },
    { "Crossfade", ControlKind::horizontal, true, "Mosaic",
      "Sets the click-free equal-power transition time between shuffled spaces." },
    { "Division", ControlKind::choice, false, "Motion",
      "Sets how often a tempo-synchronized re-shuffle occurs." },
    { "Wet Level", ControlKind::level, true, "Wet Field",
      "Sets the loudness-normalized shuffled convolution field before Mix." },
    { "Stereo Width", ControlKind::horizontal, true, "Wet Field",
      "Scales only the wet side field while preserving mono fold-down." },
    { "Feedback", ControlKind::horizontal, true, "Wet Field",
      "Returns a bounded filtered wet field to the convolution input." },
    { "Low Cut", ControlKind::horizontal, false, "Wet Passband",
      "Removes low frequencies from the completed wet field." },
    { "High Cut", ControlKind::horizontal, false, "Wet Passband",
      "Softens high frequencies in the completed wet field." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends immediate dry signal with the moving mosaic field." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts final output level." }
}};
constexpr Metadata midnightCurrentMetadata {{
    { "Filter Center", ControlKind::horizontal, true, "Submerged Filter",
      "Sets the center of the dark resonant filter and phase network." },
    { "Filter Sweep", ControlKind::horizontal, true, "Submerged Filter",
      "Sets how far the filter moves above and below its center." },
    { "Motion Rate", ControlKind::horizontal, true, "Motion",
      "Sets the free-running speed of the slow analog current." },
    { "Phase Depth", ControlKind::horizontal, true, "Phase Network",
      "Sets how deeply the eight all-pass stages sweep." },
    { "Regeneration", ControlKind::horizontal, true, "Phase Network",
      "Feeds the phase field back with positive or negative polarity." },
    { "Resonance", ControlKind::horizontal, true, "Submerged Filter",
      "Emphasizes the moving filter cutoff without unstable self-oscillation." },
    { "Warmth", ControlKind::horizontal, true, "Analog Front End",
      "Drives the preamp and output stages into soft analog saturation." },
    { "Softness", ControlKind::horizontal, true, "Analog Front End",
      "Rounds sharp attacks and darkens the electric-piano-like body." },
    { "Drift", ControlKind::horizontal, false, "Motion",
      "Adds slow imperfect movement around the main modulation cycle." },
    { "Stereo Spread", ControlKind::horizontal, true, "Stereo",
      "Expands a mono-cancelling side field while preserving fold-down." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the immediate dry input with the submerged phase field." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the complete effect." }
}};
constexpr Metadata autoWahMetadata {{
    { "Sensitivity", ControlKind::horizontal, true, "Detector",
      "Sets the visible input threshold; higher sensitivity responds to quieter picks." },
    { "Center Frequency", ControlKind::horizontal, true, "Filter",
      "Sets the musical midpoint of the wah sweep." },
    { "Sweep Range", ControlKind::horizontal, true, "Filter",
      "Sets how many octaves the filter travels after a pick." },
    { "Attack", ControlKind::horizontal, true, "Envelope",
      "Sets how quickly the wah opens or closes after a pick." },
    { "Release", ControlKind::horizontal, true, "Envelope",
      "Sets how quickly the wah returns after the note decays." },
    { "Resonance", ControlKind::horizontal, true, "Filter",
      "Sets the vocal emphasis at the moving filter frequency." },
    { "Direction", ControlKind::choice, true, "Envelope",
      "Chooses an upward or downward sweep after each pick." },
    { "Pick Focus", ControlKind::horizontal, true, "Detector",
      "Emphasizes pick attack relative to sustained signal level." },
    { "Drive", ControlKind::horizontal, false, "Character",
      "Adds level-referenced saturation and harmonic push before the resonant filter." },
    { "Filter Type", ControlKind::choice, true, "Filter",
      "Selects Low Pass, Band Pass, Peak, or Classic Wah response." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the immediate dry signal with the wah filter." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry and filtered paths." }
}};
constexpr Metadata resonantSculptureMetadata {{
    { "Material", ControlKind::choice, true, "Object",
      "Selects String, Glass, Metal, or Membrane modal behavior." },
    { "Shape", ControlKind::horizontal, true, "Object",
      "Warps the spacing and geometry of the resonant modes." },
    { "Pitch Offset", ControlKind::horizontal, true, "Pitch",
      "Transposes the tracked input fundamental from -24 to +24 semitones." },
    { "Excitation", ControlKind::horizontal, true, "Excitation",
      "Sets how strongly incoming audio energizes the resonant modes." },
    { "Decay", ControlKind::horizontal, true, "Object",
      "Sets how long modal energy continues after excitation." },
    { "Mutation", ControlKind::horizontal, true, "Motion",
      "Adds slow independent movement to the modal frequencies." },
    { "Bow", ControlKind::horizontal, true, "Excitation",
      "Turns sustained input into continuous bowed excitation." },
    { "Strike", ControlKind::horizontal, true, "Excitation",
      "Emphasizes pick and consonant attacks as sharp impacts." },
    { "Stereo Field", ControlKind::horizontal, true, "Stereo",
      "Spreads alternating modes in a mono-cancelling side field." },
    { "Chaos", ControlKind::horizontal, true, "Motion",
      "Adds deterministic irregular motion without destabilizing decay." },
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends the immediate dry signal with the resonant sculpture." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry and resonant paths." }
}};
constexpr Metadata guitarTunerMetadata {{
    { "Reference Pitch", ControlKind::horizontal, true, "Calibration",
      "Calibrates concert A from 430 to 450 Hz." },
    { "View", ControlKind::choice, true, "Display",
      "Legacy saved display selection; the tuner now always uses Precision." },
    { "Tuning", ControlKind::choice, true, "Target",
      "Legacy saved target selection; the tuner now always uses Chromatic." },
    { "Transpose", ControlKind::horizontal, false, "Target",
      "Shifts the target notes from -12 to +12 semitones." },
    { "Input Gain", ControlKind::level, false, "Detector",
      "Adjusts detector sensitivity without changing passthrough level." },
    { "Gate", ControlKind::level, true, "Detector",
      "Rejects input below the selected level from pitch tracking." },
    { "Response", ControlKind::horizontal, true, "Detector",
      "Balances fast note acquisition against steady cent readout." },
    { "Display Range", ControlKind::choice, false, "Display",
      "Sets the visible fine-tuning range from plus or minus 2 to 50 cents." },
    { "Strobe Speed", ControlKind::horizontal, false, "Display",
      "Legacy saved strobe setting retained for project compatibility." },
    { "Mute", ControlKind::toggle, true, "Output",
      "Legacy saved mute setting; tuner passthrough is now always transparent." },
    { "Output", ControlKind::level, false, "Output",
      "Legacy saved output setting; tuner passthrough is now always transparent." },
    { "Hold", ControlKind::horizontal, false, "Display",
      "Keeps the last confident note visible after the input falls away." }
}};
constexpr Names emptyNames { "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-" };
constexpr Names eqNames {
    "Low Frequency", "Low Gain", "Low Q",
    "Mid Frequency", "Mid Gain", "Mid Q",
    "High Frequency", "High Gain", "High Q",
    "Output", "Low Shape", "High Shape"
};
constexpr Names compressorNames {
    "Threshold", "Ratio", "Attack", "Release", "Knee", "Manual Trim",
    "Mix", "Detector", "Auto Makeup", "-", "-", "-"
};
constexpr Names saturatorNames {
    "Drive", "Tone", "Bias", "Output", "Mix", "Mode", "-",
    "-", "-", "-", "-", "-"
};
constexpr Names delayNames {
    "Time", "Division", "Sync", "Feedback", "Style", "Character",
    "Stereo Mode", "Spread", "Movement", "Tone", "Ducking", "Mix"
};
constexpr Names limiterNames {
    "Threshold", "Ceiling", "Release", "Lookahead", "Auto Gain", "-", "-",
    "-", "-", "-", "-", "-"
};
constexpr Names reverbNames {
    "Decay", "Room Scale", "Dry", "Wet", "Mode", "Pre-delay",
    "Diffusion", "Ducking", "Width", "Damping", "Low Cut", "High Cut"
};
constexpr Names stereoWidthNames {
    "Width", "Dimension", "Mono Below", "Focus", "Balance", "Mix",
    "Output", "Mono Safe", "-", "-", "-", "-"
};
constexpr Names midSideDecoderNames {
    "Width", "Swap Channels", "Mute Sides", "-", "-", "-", "-", "-", "-",
    "-", "-", "-"
};
constexpr Names tremoloNames {
    "Mode", "Rate", "Sync", "Division", "Tremolo Depth", "Pitch Depth",
    "Shape", "Stereo Phase", "Crossover", "Mix", "Output", "-"
};
constexpr Names envelopeModulatorNames {
    "Mode", "Quiet Speed", "Loud Speed", "Quiet Level", "Loud Level",
    "Attack", "Release", "Tremolo Depth", "Pitch Depth", "Waveform",
    "Mix", "Output"
};
constexpr Names rotarySpeakerNames {
    "Speed", "Drive", "Rotor Balance", "Crossover", "Motion",
    "Mic Distance", "Mic Spread", "Spin-up", "Cabinet Color", "Ambience", "Mix",
    "Output"
};
constexpr Names convolutionReverbNames {
    "Low Cut", "High Cut", "Wet", "Output Trim", "Dry", "Ducking", "-", "-",
    "-", "-", "-", "-"
};
constexpr Names dynamicEqualizerNames {
    "Frequency", "Q", "Range", "Threshold", "Ratio", "Attack", "Release",
    "Shape", "Detector", "External Sidechain", "Listen", "Stereo Link"
};
constexpr Names randomGranulizerNames {
    "Voices", "Size Minimum", "Grain Rate", "Capture Range", "Size Maximum",
    "Reverse Chance", "Stereo Spread", "Rhythmic Delay Chance", "Brightness",
    "Regeneration", "Dry", "Wet"
};
constexpr Names vintageChorusNames {
    "Model", "Rate", "Depth", "Delay", "Density", "Width", "Regeneration",
    "Tone", "Age", "Stereo Phase", "Mix", "Output"
};
constexpr Names beatPermuterNames {
    "Slice", "Chance", "Pattern", "History", "Length", "Slice Length", "Pitch",
    "Variation", "Stereo", "Feedback", "Mix", "Output"
};
constexpr Names spectralPrismNames {
    "Warp", "Pivot", "Shift", "Smear", "Freeze", "Motion Rate",
    "Motion Depth", "Phase Diffusion", "Stereo Spread", "Transient Preserve",
    "Mix", "Output"
};
constexpr Names resonantMatrixNames {
    "Tune", "Scale", "Span", "Topology", "Decay", "Damping", "Detune",
    "Motion Rate", "Motion Depth", "Width", "Mix", "Output"
};
constexpr Names wavefoldGardenNames {
    "Character", "Drive", "Folds", "Symmetry", "Shape", "Dynamics", "Attack",
    "Release", "Tone", "Stereo Bloom", "Mix", "Output"
};
constexpr Names gateExpanderNames {
    "Threshold", "Range", "Attack", "Hold", "Release", "Hysteresis",
    "Low Cut", "High Cut", "External Sidechain", "Listen", "Stereo Link", "-"
};
constexpr Names transientDesignerNames {
    "Attack", "Sustain", "Sensitivity", "Speed", "Focus", "Clip Guard",
    "Mix", "Output", "-", "-", "-", "-"
};
constexpr Names multibandCompressorNames {
    "Low/Mid Crossover", "Mid/High Crossover", "Low Threshold",
    "Mid Threshold", "High Threshold", "Ratio", "Attack", "Release",
    "Auto Makeup", "Stereo Link", "Mix", "Output"
};
constexpr Names studioPhaserNames {
    "Stages", "Rate", "Sync", "Division", "Depth", "Center", "Sweep",
    "Feedback", "Stereo Phase", "Mix", "Output", "-"
};
constexpr Names studioFlangerNames {
    "Model", "Rate", "Sync", "Division", "Depth", "Manual Delay",
    "Feedback", "Stereo Phase", "Tone", "Mix", "Output", "-"
};
constexpr Names diffusionDelayNames {
    "Time", "Sync", "Division", "Feedback", "Diffusion", "Movement",
    "Low Cut", "High Cut", "Width", "Ducking", "Mix", "Output"
};
constexpr Names pitchBloomNames {
    "Interval", "Fine", "Delay", "Feedback", "Bloom", "Spread",
    "Low Cut", "High Cut", "Ducking", "Mix", "Output", "-"
};
constexpr Names frequencyLabNames {
    "Root", "Harmony", "Register", "Decay", "Tone", "Motion Rate",
    "Motion Depth", "Stereo Spread", "Low Cut", "High Cut", "Mix", "Output"
};
constexpr Names spatialOrbitNames {
    "Path", "Rate", "Sync", "Division", "Azimuth Span", "Width", "Distance",
    "Doppler", "Air Damping", "Mono Below", "Mix", "Output"
};
constexpr Names signalDecayNames {
    "Resolution", "Sample Rate", "Jitter", "Dropouts", "Bandwidth", "Noise",
    "Wow", "Flutter", "Stereo Wear", "Mix", "Output", "-"
};
constexpr Names analogTapeNames {
    "Machine", "Input", "Drive", "Bias", "Tape Speed", "Head Bump", "Wow",
    "Flutter", "Wear", "Noise", "Mix", "Output"
};
constexpr Names resonanceTamerNames {
    "Reduction", "Selectivity", "Reaction", "Tone Bias", "Low Limit",
    "High Limit", "Transient Preserve", "Mix", "Output", "-", "-", "-"
};
constexpr Names spectralBalanceNames {
    "Contour", "Amount", "Low Weight", "Presence", "Air", "Adaptation",
    "Detail", "Transient Preserve", "Output", "-", "-", "-"
};
constexpr Names phaseCoherenceNames {
    "Range", "Crossover", "Correction", "Max Alignment", "Phase Rotation",
    "Stereo Preserve", "Mono Below", "Output", "-", "-", "-", "-"
};
constexpr Names loudnessRiderNames {
    "Target", "Range", "Window", "Reaction", "Lookahead", "Transient Hold",
    "Crest Preserve", "Gate", "Output", "-", "-", "-"
};
constexpr Names adaptiveClipperNames {
    "Drive", "Ceiling", "Style", "Shape", "Transient Bias", "Release",
    "Stereo Link", "Oversampling", "Auto Trim", "Mix", "Output", "-"
};
constexpr Names spectralDelayCanvasNames {
    "Sync", "Base Time", "Division", "Low Delay", "Mid Delay", "High Delay",
    "Feedback", "Diffusion", "Stereo Spread", "Freeze", "Mix", "Output"
};
constexpr Names formantForgeNames {
    "Model", "Vowel X", "Vowel Y", "Formant Shift", "Resonance", "Breath",
    "Motion Rate", "Motion Depth", "Stereo Spread", "Mix", "Output", "-"
};
constexpr Names harmonicMirageNames {
    "Mode", "Tracking", "Partials", "Even / Odd", "Inharmonicity",
    "Fine Drift", "Response", "Transient Preserve", "Stereo Spread", "Mix",
    "Output", "-"
};
constexpr Names chaosFieldNames {
    "Attractor", "Rate", "Sync", "Division", "Depth", "Filter Center",
    "Delay Center", "Feedback", "Pan Orbit", "Stereo Spread", "Mix", "Output"
};
constexpr Names timeMosaicNames {
    "History", "Tile Width", "Tile Time", "Age", "Motion", "Coherence",
    "Pitch Drift", "Freeze", "Stereo Spread", "Mix", "Output", "-"
};
constexpr Names guitarCabinetNames {
    "Cabinet IR", "Low Cut", "High Cut", "Body", "Presence", "Drive",
    "Mix", "Output", "-", "-", "-", "-"
};
constexpr Names chordLoomNames {
    "Sensitivity", "Capture", "Transition", "Texture", "Motion",
    "Octave Bloom", "Stereo Spread", "Low Cut", "High Cut", "Dry",
    "Sustain", "Output"
};
constexpr Names volumeSwellNames {
    "Sensitivity", "Swell Time", "Curve", "Floor", "Mix", "Output",
    "-", "-", "-", "-", "-", "-"
};
constexpr Names spectralNebulaNames {
    "Smear", "Decay", "Drift", "Shimmer", "Focus", "Freeze", "Width",
    "Low Cut", "High Cut", "Dry", "Cloud", "Output"
};
constexpr Names neuralAmpModelerNames {
    "Pre Gain", "Bass", "Mid", "Treble", "Post Gain",
    "-", "-", "-", "-", "-", "-", "-"
};
constexpr Names stageVocalNames {
    "Low Cut", "Fast Compression", "Fast Attack", "Fast Release",
    "Slow Compression", "Slow Attack", "Slow Release",
    "De-Ess Frequency", "De-Ess Amount", "Presence", "Air", "Output"
};
constexpr Names vinylSimulatorNames {
    "Condition", "Crackle", "Dust", "Scratches", "Surface Noise", "Rumble",
    "Wow", "Flutter", "Tone", "Stereo Spread", "Mix", "Output"
};
constexpr Names optoLevelerNames {
    "Peak Reduction", "Gain", "Mode", "HF Emphasis", "Sidechain Low Cut",
    "Cell Age", "Stereo Link", "Mix", "Output", "-", "-", "-"
};
constexpr Names fetLimiterNames {
    "Input", "Output", "Attack", "Release", "Ratio", "Detector Low Cut",
    "Stereo Link", "Mix", "Output Trim", "-", "-", "-"
};
constexpr Names programEqualizerNames {
    "Low Frequency", "Low Boost", "Low Attenuation",
    "High Boost Frequency", "High Boost", "Bandwidth",
    "High Attenuation Frequency", "High Attenuation", "Drive", "Output",
    "-", "-"
};
constexpr Names studioPlateNames {
    "Plate Size", "Decay", "Damping", "Pre-Delay", "Low Cut", "High Cut",
    "Pickup Spread", "Width", "Ducking", "Dry", "Wet", "Output"
};
constexpr Names velvetDriveNames {
    "Input", "Voice", "Drive", "Ratio", "Level", "Bass", "Mid", "Treble",
    "Presence", "Feel", "Speaker", "Master"
};
constexpr Names wormholeConvolutionNames {
    "A Start", "A End", "Reverse A", "Level A",
    "B Start", "B End", "Reverse B", "Level B",
    "Low Cut", "High Cut", "Dry", "Output"
};
constexpr Names bitcrusherNames {
    "Bit Depth", "Sample Rate", "Quantizer", "Dither", "Noise Shape",
    "Anti-Alias", "Jitter", "Step Slew", "Grid Bias", "Drive", "Mix",
    "Output"
};
constexpr Names portalPermuterNames {
    "Slice", "Chance", "Pattern", "History", "Length", "Slice Length",
    "Pitch", "IR Position", "Reverse IR", "Feedback", "Mix", "Output"
};
constexpr Names irMosaicNames {
    "Shuffle Style", "Re-shuffle Speed", "Sync", "Crossfade", "Division",
    "Wet Level",
    "Stereo Width", "Feedback", "Low Cut", "High Cut", "Mix", "Output"
};
constexpr Names midnightCurrentNames {
    "Filter Center", "Filter Sweep", "Motion Rate", "Phase Depth",
    "Regeneration", "Resonance", "Warmth", "Softness", "Drift",
    "Stereo Spread", "Mix", "Output"
};
constexpr Names autoWahNames {
    "Sensitivity", "Center Frequency", "Sweep Range", "Attack", "Release",
    "Resonance", "Direction", "Pick Focus", "Drive", "Filter Type", "Mix",
    "Output"
};
constexpr Names resonantSculptureNames {
    "Material", "Shape", "Pitch Offset", "Excitation", "Decay", "Mutation",
    "Bow", "Strike", "Stereo Field", "Chaos", "Mix", "Output"
};
constexpr Names guitarTunerNames {
    "Reference Pitch", "View", "Tuning", "Transpose", "Input Gain", "Gate",
    "Response", "Display Range", "Strobe Speed", "Mute", "Output", "Hold"
};
const std::array<std::array<float, 16>, 3> premiumEarlyMilliseconds {{
    { 11.3f, 18.7f, 27.1f, 37.9f, 51.7f, 69.1f, 79.3f, 88.7f,
      99.1f, 111.7f, 125.3f, 140.9f, 158.3f, 177.7f, 199.1f, 223.9f },
    { 4.7f, 8.3f, 13.1f, 19.3f, 27.1f, 36.7f, 42.1f, 48.7f,
      56.3f, 65.1f, 75.7f, 88.1f, 102.7f, 119.3f, 138.1f, 159.7f },
    { 0.9f, 2.1f, 3.7f, 5.9f, 8.3f, 11.3f, 14.9f, 19.1f,
      23.9f, 29.3f, 35.3f, 42.1f, 49.7f, 58.1f, 67.3f, 77.3f }
}};

float linear(float low, float high, float normalized)
{
    return low + (high - low) * juce::jlimit(0.0f, 1.0f, normalized);
}

float linearNormalized(float low, float high, float plain)
{
    return juce::jlimit(0.0f, 1.0f, (plain - low) / (high - low));
}

float exponential(float low, float high, float normalized)
{
    return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, normalized));
}

juce::String formatFrequency(float frequency)
{
    if (frequency >= 1000.0f)
        return juce::String(frequency / 1000.0f,
                            frequency < 9999.5f ? 2 : 1)
               + " kHz";
    return juce::String(frequency, 0) + " Hz";
}

juce::String formatTunedFrequency(float frequency)
{
    static constexpr std::array<const char*, 12> noteNames {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    const auto midi = juce::roundToInt(
        69.0 + 12.0 * std::log2(static_cast<double>(frequency) / 440.0));
    const auto note = (midi % 12 + 12) % 12;
    const auto octave = midi / 12 - 1;
    return juce::String(frequency, 1) + " Hz  "
           + juce::String(noteNames[static_cast<size_t>(note)])
           + juce::String(octave);
}

float parsedFrequency(const juce::String& text, float value)
{
    return text.containsIgnoreCase("khz") ? value * 1000.0f : value;
}

juce::String formatGrainSize(float milliseconds)
{
    if (milliseconds >= 1000.0f)
        return juce::String(milliseconds * 0.001f, 2) + " s";
    return juce::String(milliseconds, 0) + " ms";
}

float parsedMilliseconds(const juce::String& text, float value)
{
    return text.containsIgnoreCase("s")
               && !text.containsIgnoreCase("ms")
               ? value * 1000.0f : value;
}

juce::String formatSeconds(float seconds)
{
    return seconds < 1.0f
               ? juce::String(seconds * 1000.0f, 0) + " ms"
               : juce::String(seconds, seconds < 10.0f ? 2 : 1) + " s";
}

float parsedSeconds(const juce::String& text, float value)
{
    return text.containsIgnoreCase("ms") ? value * 0.001f : value;
}

const char* roomScaleName(float percent)
{
    if (percent < 62.5f)
        return "Compact";
    if (percent < 125.0f)
        return "Natural";
    if (percent < 175.0f)
        return "Large";
    return "Vast";
}

std::optional<float> namedRoomScale(const juce::String& text)
{
    const auto name = text.trim();
    if (name.equalsIgnoreCase("compact")
        || name.equalsIgnoreCase("small"))
        return linearNormalized(25.0f, 200.0f, 50.0f);
    if (name.equalsIgnoreCase("natural")
        || name.equalsIgnoreCase("medium"))
        return linearNormalized(25.0f, 200.0f, 100.0f);
    if (name.equalsIgnoreCase("large"))
        return linearNormalized(25.0f, 200.0f, 150.0f);
    if (name.equalsIgnoreCase("vast")
        || name.equalsIgnoreCase("huge"))
        return 1.0f;
    return std::nullopt;
}

float exponentialNormalized(float low, float high, float plain)
{
    return juce::jlimit(0.0f, 1.0f,
        std::log(juce::jlimit(low, high, plain) / low) / std::log(high / low));
}

std::optional<float> numericValue(const juce::String& text)
{
    const auto trimmed = text.trim();
    int start = 0;
    while (start < trimmed.length()
           && !juce::CharacterFunctions::isDigit(trimmed[start])
           && trimmed[start] != '-' && trimmed[start] != '+'
           && trimmed[start] != '.')
        ++start;
    if (start >= trimmed.length())
        return std::nullopt;
    const auto token = trimmed.substring(start);
    bool hasDigit = false;
    for (const auto character : token)
        hasDigit = hasDigit || juce::CharacterFunctions::isDigit(character);
    if (!hasDigit)
        return std::nullopt;
    return static_cast<float>(token.getDoubleValue());
}
} // namespace

int discreteIndex(float normalized, int optionCount)
{
    return juce::jlimit(0, juce::jmax(0, optionCount - 1),
        static_cast<int>(juce::jlimit(0.0f, 1.0f, normalized)
                         * static_cast<float>(optionCount)));
}

float discreteValue(int index, int optionCount)
{
    if (optionCount <= 0)
        return 0.0f;
    return (static_cast<float>(juce::jlimit(0, optionCount - 1, index)) + 0.5f)
           / static_cast<float>(optionCount);
}

EqualizerBandMode equalizerBandMode(float normalizedMode)
{
    return static_cast<EqualizerBandMode>(discreteIndex(normalizedMode, 3));
}

bool equalizerLowIsHighPass(float normalizedMode)
{
    return equalizerBandMode(normalizedMode) == EqualizerBandMode::cut;
}

bool equalizerHighIsLowPass(float normalizedMode)
{
    return equalizerBandMode(normalizedMode) == EqualizerBandMode::cut;
}

const std::array<std::array<float, 16>, 3>& reverbEarlyMilliseconds()
{
    return premiumEarlyMilliseconds;
}

std::array<float, 2> reverbDecayRatios(int mode, float damping)
{
    const auto safeMode = juce::jlimit(0, 2, mode);
    const auto lowRatio = safeMode == 0 ? 1.22f
                                        : safeMode == 1 ? 1.02f : 0.88f;
    const auto modeHighRatio = safeMode == 0 ? 0.72f
                                             : safeMode == 1 ? 0.88f : 1.08f;
    return {
        lowRatio,
        modeHighRatio * linear(1.12f, 0.18f, damping)
    };
}

namespace
{
ControlOptions optionsFor(ModuleType type, int control)
{
    if (type == ModuleType::equalizer && control == 10)
        return { { "Bell", "Low Shelf", "High Pass" }, 3 };
    if (type == ModuleType::equalizer && control == 11)
        return { { "Bell", "High Shelf", "Low Pass" }, 3 };
    if (type == ModuleType::saturator && control == 5)
        return { { "Soft", "Smooth", "Hard" }, 3 };
    if (type == ModuleType::delay && control == 1)
        return { { "1/32T", "1/32", "1/16T", "1/16",
                   "1/8T", "1/16.", "1/8", "1/4T",
                   "1/8.", "1/4", "1/4.", "1/2" }, 12 };
    if (type == ModuleType::delay && control == 4)
        return { { "Studio", "Tape", "BBD", "Vintage Digital" }, 4 };
    if (type == ModuleType::delay && control == 6)
        return { { "Mono", "Dual", "Ping Pong", "Cross" }, 4 };
    if (type == ModuleType::algorithmicReverb && control == 4)
        return { { "Hall", "Chamber", "Plate" }, 3 };
    if (type == ModuleType::tremolo && control == 0)
        return { { "Amplitude", "Harmonic", "Vibrato" }, 3 };
    if (type == ModuleType::envelopeModulator && control == 0)
        return { { "Tremolo", "Harmonic", "Vibrato" }, 3 };
    if (type == ModuleType::envelopeModulator && control == 9)
        return { { "Sine", "Triangle", "Rounded Pulse" }, 3 };
    if (type == ModuleType::tremolo && control == 3)
        return { { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8",
                   "1/8.", "1/16" }, 8 };
    if (type == ModuleType::rotarySpeaker && control == 0)
        return { { "Brake", "Chorale", "Tremolo" }, 3 };
    if (type == ModuleType::dynamicEqualizer && control == 7)
        return { { "Bell", "Low Shelf", "High Shelf" }, 3 };
    if (type == ModuleType::dynamicEqualizer && control == 8)
        return { { "Peak", "RMS" }, 2 };
    if (type == ModuleType::vintageChorus && control == 0)
        return { { "Vintage BBD", "Dimension", "Tri-Chorus", "String Ensemble" }, 4 };
    if (type == ModuleType::beatPermuter && control == 0)
        return { { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 6 };
    if (type == ModuleType::beatPermuter && control == 2)
        return { { "Repeat", "Reverse", "Rotate", "Shuffle" }, 4 };
    if (type == ModuleType::resonantMatrix && control == 1)
        return { { "Major", "Minor", "Dorian", "Pentatonic", "Whole Tone", "Octaves" }, 6 };
    if (type == ModuleType::resonantMatrix && control == 3)
        return { { "Orbit", "Butterfly", "Spiral", "Scatter" }, 4 };
    if (type == ModuleType::wavefoldGarden && control == 0)
        return { { "Petal", "Prism", "Chebyshev", "Bloom" }, 4 };
    if (type == ModuleType::bitcrusher && control == 2)
        return { { "Balanced", "Truncate", "Floor", "Companded" }, 4 };
    if (type == ModuleType::portalPermuter && control == 0)
        return { { "1/4", "1/8.", "1/4T", "1/8",
                   "1/8T", "1/16", "1/16T", "1/32" }, 8 };
    if (type == ModuleType::portalPermuter && control == 2)
        return { { "Repeat", "Reverse", "Rotate", "Shuffle" }, 4 };
    if (type == ModuleType::irMosaic && control == 0)
        return { { "Step", "Random", "Ping Pong", "Leap" }, 4 };
    if (type == ModuleType::irMosaic && control == 4)
        return { { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8",
                   "1/8.", "1/16" }, 8 };
    if (type == ModuleType::studioPhaser && control == 0)
        return { { "2", "4", "6", "8", "12" }, 5 };
    if ((type == ModuleType::studioPhaser
         || type == ModuleType::studioFlanger
         || type == ModuleType::spatialOrbit)
        && control == 3)
        return { { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8",
                   "1/8.", "1/16" }, 8 };
    if (type == ModuleType::studioFlanger && control == 0)
        return { { "Tape", "Through-Zero", "Jet", "BBD" }, 4 };
    if (type == ModuleType::diffusionDelay && control == 2)
        return { { "1/32", "1/16", "1/16.", "1/8",
                   "1/8.", "1/4", "1/4.", "1/2" }, 8 };
    if (type == ModuleType::pitchBloom && control == 0)
        return { { "Unison", "Fifth", "Octave", "Octave + Fifth",
                   "Two Octaves" }, 5 };
    if (type == ModuleType::frequencyLab && control == 0)
        return { { "C", "C#", "D", "D#", "E", "F",
                   "F#", "G", "G#", "A", "A#", "B" }, 12 };
    if (type == ModuleType::frequencyLab && control == 1)
        return { { "Major", "Minor", "Sus 2", "Sus 4", "Fifths",
                   "Major 7", "Minor 7", "Octave Drone" }, 8 };
    if (type == ModuleType::frequencyLab && control == 2)
        return { { "Low", "Warm", "Bright", "Air" }, 4 };
    if (type == ModuleType::spatialOrbit && control == 0)
        return { { "Circle", "Figure Eight", "Pendulum", "Wander" }, 4 };
    if (type == ModuleType::analogTape && control == 0)
        return { { "Worn Cassette", "Consumer Reel", "Ampex-Style Deck",
                   "Studer-Style Deck" }, 4 };
    if (type == ModuleType::analogTape && control == 4)
        return { { "3.75 ips", "7.5 ips", "15 ips", "30 ips" }, 4 };
    if (type == ModuleType::resonanceTamer && control == 1)
        return { { "Broad", "Focused", "Surgical" }, 3 };
    if (type == ModuleType::resonanceTamer && control == 2)
        return { { "Slow", "Natural", "Fast" }, 3 };
    if (type == ModuleType::spectralBalance && control == 0)
        return { { "Natural", "Warm", "Clear", "Vocal", "Flat" }, 5 };
    if (type == ModuleType::spectralBalance && control == 6)
        return { { "Smooth", "Balanced", "Detailed" }, 3 };
    if (type == ModuleType::phaseCoherence && control == 0)
        return { { "Low End", "Low + Mid", "Full Range" }, 3 };
    if (type == ModuleType::loudnessRider && control == 2)
        return { { "Short", "Medium", "Long" }, 3 };
    if (type == ModuleType::adaptiveClipper && control == 2)
        return { { "Clean", "Punch", "Dense" }, 3 };
    if (type == ModuleType::adaptiveClipper && control == 7)
        return { { "2x", "4x", "8x" }, 3 };
    if (type == ModuleType::spectralDelayCanvas && control == 2)
        return { { "1/32", "1/16", "1/8", "1/4",
                   "1/2", "1 bar", "2 bars" }, 7 };
    if (type == ModuleType::formantForge && control == 0)
        return { { "Human", "Tube", "Creature", "Metallic" }, 4 };
    if (type == ModuleType::harmonicMirage && control == 0)
        return { { "Harmonic", "Subharmonic", "Hollow", "Metallic" }, 4 };
    if (type == ModuleType::harmonicMirage && control == 1)
        return { { "Loose", "Musical", "Tight" }, 3 };
    if (type == ModuleType::chaosField && control == 0)
        return { { "Lorenz", "Rossler", "Double Pendulum" }, 3 };
    if (type == ModuleType::chaosField && control == 3)
        return { { "1 bar", "1/2", "1/4.", "1/4",
                   "1/8.", "1/8", "1/16.", "1/16" }, 8 };
    if (type == ModuleType::guitarCabinet && control == 0)
        return { {
            "1960 GREEN 57+57+121", "1984B V30 GB", "6402 57+121",
            "6402 57", "65W 57+121", "BLACK 57+160", "CREAM65 57+121",
            "EV12L 57+121", "GREEN", "GREEN+V30 MIX", "MESRECT V30",
            "Mikko 1", "66 Alnico Silver e906", "66 Alnico Silver FAT-906",
            "66 Alnico Silver R121-SM545", "69 G12M Green R121-545",
            "70 G12H Green FAT-57", "C12N e906", "C12N R121-SM545",
            "M75 e906", "M75 FAT-SM57", "M75 R121-SM545", "V30"
        }, 23 };
    if (type == ModuleType::autoWah && control == 6)
        return { { "Up", "Down" }, 2 };
    if (type == ModuleType::autoWah && control == 9)
        return { { "Low Pass", "Band Pass", "Peak", "Classic Wah" }, 4 };
    if (type == ModuleType::resonantSculpture && control == 0)
        return { { "String", "Glass", "Metal", "Membrane" }, 4 };
    if (type == ModuleType::guitarTuner && control == 1)
        return { { "Strobe", "Precision", "Needle" }, 3 };
    if (type == ModuleType::guitarTuner && control == 2)
        return { { "Chromatic", "Guitar", "Bass", "Drop D",
                   "Open D", "Open G", "DADGAD" }, 7 };
    if (type == ModuleType::guitarTuner && control == 7)
        return { { "+/- 2 cents", "+/- 5 cents", "+/- 10 cents",
                   "+/- 25 cents", "+/- 50 cents" }, 5 };
    if (type == ModuleType::optoLeveler && control == 2)
        return { { "Compress", "Limit" }, 2 };
    if (type == ModuleType::fetLimiter && control == 4)
        return { { "4:1", "8:1", "12:1", "20:1", "ALL" }, 5 };
    if (type == ModuleType::programEqualizer && control == 0)
        return { { "20 Hz", "30 Hz", "60 Hz", "100 Hz" }, 4 };
    if (type == ModuleType::programEqualizer && control == 3)
        return { { "3 kHz", "4 kHz", "5 kHz", "8 kHz",
                   "10 kHz", "12 kHz", "16 kHz" }, 7 };
    if (type == ModuleType::programEqualizer && control == 6)
        return { { "5 kHz", "10 kHz", "20 kHz" }, 3 };
    if (type == ModuleType::studioPlate && control == 0)
        return { { "Compact", "Standard", "Large" }, 3 };
    if (type == ModuleType::velvetDrive && control == 0)
        return { { "Normal", "FET" }, 2 };
    if (type == ModuleType::velvetDrive && control == 1)
        return { { "Classic", "Modern", "High Headroom" }, 3 };
    if (type == ModuleType::velvetDrive && control == 10)
        return { { "Direct", "Flat 12", "Scooped 12", "Chime 12" }, 4 };
    return {};
}

std::array<float, controlsPerSlot> defaultsFor(ModuleType type)
{
    std::array<float, controlsPerSlot> values {};
    values.fill(0.5f);
    switch (type)
    {
        case ModuleType::equalizer:
            values = { exponentialNormalized(30.0f, 1200.0f, 120.0f), 0.5f,
                       exponentialNormalized(0.2f, 10.0f, 0.8f),
                       exponentialNormalized(150.0f, 7000.0f, 1000.0f), 0.5f,
                       exponentialNormalized(0.2f, 10.0f, 0.8f),
                       exponentialNormalized(1500.0f, 20000.0f, 8000.0f), 0.5f,
                       exponentialNormalized(0.2f, 10.0f, 0.8f),
                       0.5f, 0.0f, 0.0f };
            break;
        case ModuleType::compressor:
            values = { linearNormalized(-60.0f, 0.0f, -18.0f),
                       exponentialNormalized(1.0f, 20.0f, 3.0f),
                       exponentialNormalized(0.1f, 100.0f, 10.0f),
                       exponentialNormalized(10.0f, 1000.0f, 150.0f),
                       linearNormalized(0.0f, 18.0f, 6.0f), 0.0f, 1.0f, 0.0f,
                       1.0f, 0.5f, 0.5f, 0.5f };
            break;
        case ModuleType::saturator:
            values = { linearNormalized(0.0f, 36.0f, 6.0f),
                       exponentialNormalized(500.0f, 20000.0f, 12000.0f),
                       0.5f, linearNormalized(-24.0f, 6.0f, -3.0f), 1.0f,
                       discreteValue(0, 3), 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
            break;
        case ModuleType::delay:
            values = { exponentialNormalized(1.0f, 2000.0f, 250.0f),
                       discreteValue(9, 12), 1.0f, 0.30f / 0.94f,
                       discreteValue(0, 4), 0.25f,
                       discreteValue(0, 4), 0.50f, 0.0f, 0.65f,
                       0.0f, 0.20f };
            break;
        case ModuleType::limiter:
            values = { linearNormalized(-24.0f, 0.0f, -1.0f),
                       linearNormalized(-12.0f, 0.0f, -1.0f),
                       exponentialNormalized(10.0f, 1000.0f, 100.0f),
                       linearNormalized(1.0f, 10.0f, 5.0f),
                       1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
            break;
        case ModuleType::algorithmicReverb:
            values = { exponentialNormalized(0.2f, 12.0f, 2.0f),
                       linearNormalized(25.0f, 200.0f, 100.0f), 1.0f, 0.10f,
                       discreteValue(0, 3), 20.0f / 250.0f,
                       0.55f, 0.0f,
                       100.0f / 150.0f, 0.50f,
                       exponentialNormalized(20.0f, 1000.0f, 120.0f),
                       exponentialNormalized(2000.0f, 20000.0f, 12000.0f) };
            break;
        case ModuleType::stereoWidth:
            values = { 0.60f, 0.22f,
                       exponentialNormalized(20.0f, 500.0f, 120.0f),
                       exponentialNormalized(500.0f, 8000.0f, 1800.0f),
                       0.5f, 1.0f, 0.5f, 1.0f,
                       0.5f, 0.5f, 0.5f, 0.5f };
            break;
        case ModuleType::midSideDecoder:
            values = { 0.35f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f,
                       0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
            break;
        case ModuleType::tremolo:
            values = { discreteValue(0, 3),
                       exponentialNormalized(0.05f, 20.0f, 4.0f),
                       0.0f, discreteValue(4, 8), 0.70f,
                       linearNormalized(0.0f, 100.0f, 24.0f), 0.0f,
                       linearNormalized(0.0f, 180.0f, 0.0f),
                       exponentialNormalized(100.0f, 4000.0f, 700.0f),
                       1.0f, linearNormalized(-12.0f, 12.0f, 0.0f), 0.5f };
            break;
        case ModuleType::envelopeModulator:
            values = {
                discreteValue(0, 3),
                exponentialNormalized(0.1f, 20.0f, 1.5f),
                exponentialNormalized(0.1f, 20.0f, 8.0f),
                linearNormalized(-72.0f, 0.0f, -48.0f),
                linearNormalized(-72.0f, 0.0f, -12.0f),
                exponentialNormalized(1.0f, 200.0f, 10.0f),
                exponentialNormalized(30.0f, 3000.0f, 250.0f),
                0.65f, 0.25f, discreteValue(0, 3), 1.0f,
                linearNormalized(-12.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::rotarySpeaker:
            values = { discreteValue(1, 3),
                       linearNormalized(0.0f, 24.0f, 6.0f), 0.5f,
                       exponentialNormalized(500.0f, 1400.0f, 800.0f),
                       0.75f, linearNormalized(20.0f, 200.0f, 70.0f),
                       linearNormalized(0.0f, 180.0f, 110.0f), 0.5f,
                       0.65f, 0.18f, 1.0f,
                       linearNormalized(-12.0f, 12.0f, 0.0f) };
            break;
        case ModuleType::convolutionReverb:
            values = {
                exponentialNormalized(20.0f, 1000.0f, 20.0f),
                exponentialNormalized(2000.0f, 20000.0f, 20000.0f),
                0.10f, linearNormalized(-18.0f, 18.0f, 0.0f),
                1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::dynamicEqualizer:
            values = {
                exponentialNormalized(20.0f, 20000.0f, 6000.0f),
                exponentialNormalized(0.2f, 12.0f, 3.0f),
                linearNormalized(-18.0f, 12.0f, -6.0f),
                linearNormalized(-60.0f, 0.0f, -24.0f),
                exponentialNormalized(1.0f, 10.0f, 3.0f),
                exponentialNormalized(0.1f, 100.0f, 5.0f),
                exponentialNormalized(10.0f, 1000.0f, 100.0f),
                discreteValue(0, 3), discreteValue(1, 2),
                0.0f, 0.0f, 1.0f
            };
            break;
        case ModuleType::randomGranulizer:
            values = {
                linearNormalized(1.0f, 16.0f, 6.0f),
                exponentialNormalized(50.0f, 2000.0f, 80.0f),
                exponentialNormalized(0.5f, 30.0f, 6.0f),
                0.35f, exponentialNormalized(50.0f, 2000.0f, 280.0f),
                0.20f, 0.65f, 0.30f,
                exponentialNormalized(500.0f, 20000.0f, 12000.0f),
                0.10f / 0.80f,
                std::cos(0.35f * juce::MathConstants<float>::halfPi),
                std::sin(0.35f * juce::MathConstants<float>::halfPi)
            };
            break;
        case ModuleType::vintageChorus:
            values = {
                discreteValue(0, 4),
                exponentialNormalized(0.05f, 8.0f, 0.8f),
                0.45f,
                exponentialNormalized(2.0f, 30.0f, 9.0f),
                linearNormalized(1.0f, 6.0f, 2.0f),
                0.5f,
                linearNormalized(-75.0f, 75.0f, 8.0f),
                exponentialNormalized(800.0f, 18000.0f, 10000.0f),
                0.25f,
                0.5f,
                0.40f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::beatPermuter:
            values = {
                discreteValue(3, 6), 0.35f, discreteValue(0, 4),
                linearNormalized(1.0f, 8.0f, 4.0f),
                linearNormalized(1.0f, 8.0f, 2.0f),
                linearNormalized(20.0f, 100.0f, 90.0f), 0.5f, 0.20f, 0.25f,
                0.10f / 0.75f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::spectralPrism:
            values = {
                linearNormalized(-100.0f, 100.0f, 20.0f),
                exponentialNormalized(80.0f, 8000.0f, 1000.0f),
                0.5f, 0.20f, 0.0f,
                exponentialNormalized(0.02f, 4.0f, 0.15f),
                0.15f, 0.15f, 0.35f, 0.65f, 0.40f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::resonantMatrix:
            values = {
                exponentialNormalized(27.5f, 440.0f, 110.0f),
                discreteValue(3, 6),
                linearNormalized(1.0f, 4.0f, 2.0f),
                discreteValue(0, 4),
                exponentialNormalized(0.10f, 12.0f, 2.5f),
                exponentialNormalized(500.0f, 20000.0f, 8000.0f),
                linearNormalized(0.0f, 30.0f, 4.0f),
                exponentialNormalized(0.02f, 2.0f, 0.10f),
                linearNormalized(0.0f, 50.0f, 6.0f),
                linearNormalized(0.0f, 150.0f, 80.0f), 0.25f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::wavefoldGarden:
            values = {
                discreteValue(0, 4),
                linearNormalized(0.0f, 36.0f, 6.0f),
                linearNormalized(1.0f, 8.0f, 2.0f),
                0.5f, 0.35f,
                linearNormalized(-100.0f, 100.0f, 25.0f),
                exponentialNormalized(0.1f, 100.0f, 8.0f),
                exponentialNormalized(10.0f, 1000.0f, 120.0f),
                exponentialNormalized(500.0f, 20000.0f, 12000.0f),
                0.20f, 0.45f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::gateExpander:
            values = {
                linearNormalized(-80.0f, 0.0f, -36.0f),
                linearNormalized(0.0f, 80.0f, 24.0f),
                exponentialNormalized(0.05f, 100.0f, 2.0f),
                linearNormalized(0.0f, 500.0f, 50.0f),
                exponentialNormalized(5.0f, 2000.0f, 180.0f),
                linearNormalized(0.0f, 18.0f, 6.0f),
                exponentialNormalized(20.0f, 2000.0f, 80.0f),
                exponentialNormalized(1000.0f, 20000.0f, 12000.0f),
                0.0f, 0.0f, 1.0f, 0.5f
            };
            break;
        case ModuleType::transientDesigner:
            values = {
                0.5f, 0.5f, 0.5f,
                exponentialNormalized(5.0f, 200.0f, 40.0f),
                exponentialNormalized(80.0f, 8000.0f, 1500.0f),
                1.0f, 1.0f, linearNormalized(-18.0f, 18.0f, 0.0f),
                0.5f, 0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::multibandCompressor:
            values = {
                exponentialNormalized(40.0f, 800.0f, 180.0f),
                exponentialNormalized(1000.0f, 12000.0f, 3500.0f),
                linearNormalized(-60.0f, 0.0f, -18.0f),
                linearNormalized(-60.0f, 0.0f, -18.0f),
                linearNormalized(-60.0f, 0.0f, -18.0f),
                exponentialNormalized(1.0f, 20.0f, 3.0f),
                exponentialNormalized(0.1f, 100.0f, 15.0f),
                exponentialNormalized(20.0f, 2000.0f, 180.0f),
                1.0f, 1.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::studioPhaser:
            values = {
                discreteValue(2, 5),
                exponentialNormalized(0.02f, 12.0f, 0.35f),
                0.0f, discreteValue(4, 8), 0.6f,
                exponentialNormalized(80.0f, 8000.0f, 900.0f),
                exponentialNormalized(0.25f, 6.0f, 3.0f),
                linearNormalized(-95.0f, 95.0f, 20.0f),
                linearNormalized(0.0f, 180.0f, 90.0f),
                0.5f, linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::studioFlanger:
            values = {
                discreteValue(0, 4),
                exponentialNormalized(0.02f, 12.0f, 0.2f),
                0.0f, discreteValue(4, 8),
                linearNormalized(0.0f, 10.0f, 2.5f),
                exponentialNormalized(0.1f, 15.0f, 3.0f),
                linearNormalized(-90.0f, 90.0f, 15.0f),
                linearNormalized(0.0f, 180.0f, 90.0f),
                exponentialNormalized(1000.0f, 20000.0f, 12000.0f),
                0.5f, linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::diffusionDelay:
            values = {
                exponentialNormalized(10.0f, 2000.0f, 500.0f),
                1.0f, discreteValue(5, 8),
                0.35f / 0.90f, 0.30f, 0.15f,
                exponentialNormalized(20.0f, 2000.0f, 120.0f),
                exponentialNormalized(1000.0f, 20000.0f, 10000.0f),
                100.0f / 150.0f, 0.20f, 0.20f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::pitchBloom:
            values = {
                discreteValue(2, 5), 0.5f,
                exponentialNormalized(20.0f, 500.0f, 120.0f),
                0.30f / 0.85f, 0.35f, 0.75f,
                exponentialNormalized(20.0f, 2000.0f, 180.0f),
                exponentialNormalized(1000.0f, 20000.0f, 10000.0f),
                0.20f, 0.15f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::frequencyLab:
            values = {
                discreteValue(0, 12), discreteValue(0, 8),
                discreteValue(1, 4),
                exponentialNormalized(0.08f, 4.0f, 1.2f), 0.5f,
                exponentialNormalized(0.02f, 4.0f, 0.15f),
                3.0f / 25.0f, 0.60f,
                exponentialNormalized(20.0f, 1000.0f, 80.0f),
                exponentialNormalized(2000.0f, 20000.0f, 10000.0f),
                0.35f, linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::spatialOrbit:
            values = {
                discreteValue(0, 4),
                exponentialNormalized(0.02f, 5.0f, 0.1f),
                0.0f, discreteValue(2, 8),
                linearNormalized(0.0f, 360.0f, 180.0f),
                linearNormalized(0.0f, 200.0f, 100.0f),
                exponentialNormalized(0.5f, 10.0f, 2.0f),
                0.15f, 0.35f,
                exponentialNormalized(20.0f, 500.0f, 120.0f),
                1.0f, linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::signalDecay:
            values = {
                linearNormalized(4.0f, 24.0f, 16.0f),
                exponentialNormalized(1.0f, 48.0f, 32.0f),
                0.05f, 0.03f,
                exponentialNormalized(1000.0f, 20000.0f, 14000.0f),
                linearNormalized(-90.0f, -24.0f, -72.0f),
                0.08f, 0.05f, 0.10f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::analogTape:
            values = {
                discreteValue(2, 4),
                linearNormalized(-18.0f, 18.0f, 0.0f),
                0.25f, 0.5f, discreteValue(2, 4),
                0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::resonanceTamer:
            values = {
                6.0f / 18.0f, discreteValue(1, 3), discreteValue(1, 3),
                0.5f, exponentialNormalized(20.0f, 2000.0f, 80.0f),
                exponentialNormalized(1000.0f, 20000.0f, 12000.0f),
                0.65f, 1.0f, linearNormalized(-18.0f, 12.0f, 0.0f),
                0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::spectralBalance:
            values = {
                discreteValue(0, 5), 0.5f, 0.5f, 0.5f, 0.5f,
                exponentialNormalized(0.5f, 30.0f, 5.0f),
                discreteValue(1, 3), 0.65f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::phaseCoherence:
            values = {
                discreteValue(0, 3),
                exponentialNormalized(40.0f, 800.0f, 180.0f),
                0.75f, 0.5f, 0.5f, 0.75f,
                exponentialNormalized(20.0f, 500.0f, 40.0f),
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.5f, 0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::loudnessRider:
            values = {
                linearNormalized(-36.0f, -8.0f, -20.0f), 0.5f,
                discreteValue(1, 3),
                exponentialNormalized(0.25f, 10.0f, 1.5f),
                100.0f / 250.0f, 0.5f, 0.65f,
                linearNormalized(-70.0f, -30.0f, -56.0f),
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::adaptiveClipper:
            values = {
                6.0f / 24.0f,
                linearNormalized(-12.0f, 0.0f, -1.0f),
                discreteValue(0, 3), 0.65f, 0.55f,
                exponentialNormalized(20.0f, 1000.0f, 120.0f),
                1.0f, discreteValue(1, 3), 1.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::spectralDelayCanvas:
            values = {
                1.0f, exponentialNormalized(10.0f, 4000.0f, 500.0f),
                discreteValue(3, 7), 0.5f, 0.75f, 1.0f,
                0.35f, 0.25f, 0.50f, 0.0f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::formantForge:
            values = {
                discreteValue(0, 4), 0.35f, 0.45f, 0.5f, 0.45f,
                0.12f, exponentialNormalized(0.02f, 6.0f, 0.15f),
                0.15f, 0.35f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::harmonicMirage:
            values = {
                discreteValue(0, 4), discreteValue(1, 3),
                6.0f / 22.0f, 0.5f, 0.10f,
                0.10f, 0.5f,
                0.65f, 0.35f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::chaosField:
            values = {
                discreteValue(0, 3),
                exponentialNormalized(0.015f, 7.0f, 0.15f),
                0.0f, discreteValue(3, 8), 0.50f,
                exponentialNormalized(80.0f, 10000.0f, 1200.0f),
                exponentialNormalized(2.0f, 600.0f, 40.0f),
                0.50f, 0.70f, 0.60f, 0.45f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::timeMosaic:
            values = {
                0.5f, 0.35f, 0.45f,
                0.5f, 0.4f, 0.75f, 0.0f, 0.0f, 0.50f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f), 0.5f
            };
            break;
        case ModuleType::guitarCabinet:
            values = {
                discreteValue(7, 23),
                exponentialNormalized(30.0f, 300.0f, 70.0f),
                exponentialNormalized(2500.0f, 18000.0f, 9000.0f),
                0.50f, 0.50f, 0.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.5f, 0.5f, 0.5f, 0.5f
            };
            break;
        case ModuleType::chordLoom:
            values = {
                0.62f,
                exponentialNormalized(0.25f, 2.0f, 0.9f),
                exponentialNormalized(0.10f, 2.0f, 0.5f),
                0.42f, 0.25f, 0.12f, 0.65f,
                exponentialNormalized(20.0f, 1000.0f, 80.0f),
                exponentialNormalized(2000.0f, 20000.0f, 10000.0f),
                1.0f, 0.55f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::volumeSwell:
            values = {
                0.75f,
                exponentialNormalized(0.05f, 5.0f, 1.0f),
                0.62f,
                linearNormalized(-60.0f, -6.0f, -48.0f),
                1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
            };
            break;
        case ModuleType::spectralNebula:
            values = {
                0.62f,
                exponentialNormalized(0.2f, 30.0f, 3.5f),
                0.18f, 0.12f, 0.62f, 0.0f, 0.55f,
                exponentialNormalized(20.0f, 1000.0f, 80.0f),
                exponentialNormalized(1000.0f, 20000.0f, 10000.0f),
                1.0f, 0.48f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::neuralAmpModeler:
            values = {
                0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
            };
            break;
        case ModuleType::stageVocal:
            values = {
                exponentialNormalized(40.0f, 220.0f, 80.0f),
                linearNormalized(0.0f, 30.0f, 10.0f),
                exponentialNormalized(0.02f, 0.8f, 0.15f),
                exponentialNormalized(40.0f, 800.0f, 120.0f),
                linearNormalized(0.0f, 30.0f, 6.0f),
                exponentialNormalized(2.0f, 40.0f, 10.0f),
                exponentialNormalized(200.0f, 3000.0f, 800.0f),
                exponentialNormalized(3000.0f, 10000.0f, 6500.0f),
                linearNormalized(0.0f, 12.0f, 4.0f),
                linearNormalized(-6.0f, 6.0f, 1.0f),
                linearNormalized(-6.0f, 6.0f, 1.5f),
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::vinylSimulator:
            values = {
                0.35f, 0.32f, 0.28f, 0.08f,
                linearNormalized(-90.0f, -34.0f, -62.0f),
                linearNormalized(-90.0f, -36.0f, -72.0f),
                0.12f, 0.10f, 0.78f, 0.55f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::optoLeveler:
            values = {
                0.40f, linearNormalized(-12.0f, 24.0f, 0.0f),
                discreteValue(0, 2), 0.35f,
                exponentialNormalized(20.0f, 300.0f, 60.0f),
                0.35f, 1.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.0f, 0.0f, 0.0f
            };
            break;
        case ModuleType::fetLimiter:
            values = {
                linearNormalized(-24.0f, 24.0f, 0.0f),
                linearNormalized(-24.0f, 24.0f, 0.0f),
                exponentialNormalized(20.0f, 800.0f, 200.0f),
                exponentialNormalized(50.0f, 1100.0f, 300.0f),
                discreteValue(0, 5),
                exponentialNormalized(20.0f, 300.0f, 60.0f),
                1.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.0f, 0.0f, 0.0f
            };
            break;
        case ModuleType::programEqualizer:
            values = {
                discreteValue(1, 4),
                linearNormalized(0.0f, 14.0f, 0.0f),
                linearNormalized(0.0f, 18.0f, 0.0f),
                discreteValue(3, 7),
                linearNormalized(0.0f, 18.0f, 0.0f),
                0.45f,
                discreteValue(1, 3),
                linearNormalized(0.0f, 18.0f, 0.0f),
                linearNormalized(-12.0f, 18.0f, 0.0f),
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.0f, 0.0f
            };
            break;
        case ModuleType::studioPlate:
            values = {
                discreteValue(1, 3),
                exponentialNormalized(0.5f, 8.0f, 2.4f),
                0.45f,
                exponentialNormalized(1.0f, 200.0f, 20.0f),
                exponentialNormalized(20.0f, 800.0f, 80.0f),
                exponentialNormalized(2000.0f, 20000.0f, 12000.0f),
                0.65f, 0.75f,
                linearNormalized(0.0f, 18.0f, 0.0f),
                1.0f, 0.18f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::velvetDrive:
            values = {
                discreteValue(0, 2), discreteValue(0, 3),
                0.38f, 0.46f,
                linearNormalized(-18.0f, 12.0f, 0.0f),
                0.46f, 0.58f, 0.54f, 0.45f, 0.42f,
                discreteValue(1, 4),
                linearNormalized(-24.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::wormholeConvolution:
            values = {
                0.0f, 1.0f, 0.0f, 0.35f,
                0.0f, 1.0f, 0.0f, 0.35f,
                exponentialNormalized(20.0f, 1000.0f, 20.0f),
                exponentialNormalized(2000.0f, 20000.0f, 20000.0f),
                1.0f, linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::bitcrusher:
            values = {
                linearNormalized(2.0f, 24.0f, 12.0f),
                exponentialNormalized(250.0f, 768000.0f, 12000.0f),
                discreteValue(0, 4),
                0.20f, 0.0f, 0.65f, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::portalPermuter:
            values = {
                discreteValue(3, 8), 0.35f, discreteValue(0, 4),
                3.0f / 7.0f, 3.0f / 7.0f, 1.0f, 0.5f, 0.5f,
                0.0f, 0.15f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::irMosaic:
            values = {
                discreteValue(1, 4),
                exponentialNormalized(0.03f, 4.0f, 0.2f), 0.0f,
                exponentialNormalized(0.05f, 0.5f, 0.2f),
                discreteValue(2, 8), 1.0f, 2.0f / 3.0f, 0.0f,
                exponentialNormalized(20.0f, 1000.0f, 20.0f),
                exponentialNormalized(2000.0f, 20000.0f, 20000.0f),
                0.35f, linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::midnightCurrent:
            values = {
                exponentialNormalized(120.0f, 3000.0f, 520.0f),
                0.70f,
                exponentialNormalized(0.02f, 1.2f, 0.11f),
                0.78f,
                linearNormalized(-80.0f, 80.0f, 38.0f),
                0.55f, 0.45f, 0.55f, 0.15f, 0.55f, 0.70f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::autoWah:
            values = {
                linearNormalized(-18.0f, -66.0f, -42.0f),
                exponentialNormalized(120.0f, 1600.0f, 720.0f),
                linearNormalized(0.5f, 5.0f, 2.5f),
                exponentialNormalized(0.5f, 80.0f, 7.0f),
                exponentialNormalized(30.0f, 1400.0f, 320.0f),
                0.52f, discreteValue(0, 2), 0.62f, 0.12f,
                discreteValue(3, 4), 0.78f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::resonantSculpture:
            values = {
                discreteValue(1, 4), 0.45f, 0.5f,
                0.62f,
                exponentialNormalized(0.15f, 12.0f, 3.8f),
                0.18f, 0.12f, 0.72f, 0.58f, 0.08f, 0.72f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
            };
            break;
        case ModuleType::guitarTuner:
            values = {
                0.5f, discreteValue(0, 3), discreteValue(1, 7),
                0.5f, 0.5f, linearNormalized(-72.0f, -24.0f, -60.0f),
                exponentialNormalized(0.04f, 0.60f, 0.12f),
                discreteValue(4, 5), 0.45f, 0.0f,
                linearNormalized(-24.0f, 6.0f, 0.0f), 0.30f
            };
            break;
        case ModuleType::empty: break;
    }
    return values;
}
} // namespace

juce::String formatControlValue(ModuleType type, int control, float value)
{
    const auto options = controlOptions(type, control);
    if (!options.isEmpty())
        return options[discreteIndex(value, options.size())];
    if (controlMetadata(type, control).kind == ControlKind::toggle)
    {
        if (type == ModuleType::compressor && control == 7)
            return value >= 0.5f ? "External" : "Internal";
        return value >= 0.5f ? "On" : "Off";
    }

    switch (type)
    {
        case ModuleType::equalizer:
            if (control == 0) return formatFrequency(exponential(30.0f, 1200.0f, value));
            if (control == 3) return formatFrequency(exponential(150.0f, 7000.0f, value));
            if (control == 6) return formatFrequency(exponential(1500.0f, 20000.0f, value));
            if (control == 1 || control == 4 || control == 7 || control == 9)
                return juce::String(linear(-18.0f, 18.0f, value), 1) + " dB";
            if (control == 2 || control == 5 || control == 8)
                return "Q " + juce::String(exponential(0.2f, 10.0f, value), 2);
            break;
        case ModuleType::compressor:
            if (control == 0) return juce::String(linear(-60.0f, 0.0f, value), 1) + " dB";
            if (control == 1) return juce::String(exponential(1.0f, 20.0f, value), 1) + ":1";
            if (control == 2) return juce::String(exponential(0.1f, 100.0f, value), 1) + " ms";
            if (control == 3)
                return juce::String(juce::roundToInt(
                           exponential(10.0f, 1000.0f, value)))
                    + " ms";
            if (control == 4) return juce::String(linear(0.0f, 18.0f, value), 1) + " dB";
            if (control == 5) return "+" + juce::String(linear(0.0f, 24.0f, value), 1) + " dB";
            break;
        case ModuleType::saturator:
            if (control == 0) return "+" + juce::String(linear(0.0f, 36.0f, value), 1) + " dB";
            if (control == 1) return formatFrequency(exponential(500.0f, 20000.0f, value));
            if (control == 2) return juce::String(linear(-100.0f, 100.0f, value), 0) + "%";
            if (control == 3) return juce::String(linear(-24.0f, 6.0f, value), 1) + " dB";
            break;
        case ModuleType::delay:
            if (control == 0) return juce::String(exponential(1.0f, 2000.0f, value), 0) + " ms";
            if (control == 3) return juce::String(value * 94.0f, 0) + "%";
            if (control == 5) return juce::String(value * 100.0f, 0) + "%";
            if (control == 7) return juce::String(value * 100.0f, 0) + "%";
            if (control == 8) return juce::String(value * 100.0f, 0) + "%";
            if (control == 9) return juce::String(value * 100.0f, 0) + "%";
            if (control == 10) return juce::String(value * 18.0f, 1) + " dB";
            if (control == 11) return juce::String(value * 100.0f, 0) + "%";
            break;
        case ModuleType::limiter:
            if (control == 0) return juce::String(linear(-24.0f, 0.0f, value), 1) + " dB";
            if (control == 1) return juce::String(linear(-12.0f, 0.0f, value), 1) + " dB";
            if (control == 2) return juce::String(exponential(10.0f, 1000.0f, value), 0) + " ms";
            if (control == 3) return juce::String(linear(1.0f, 10.0f, value), 1) + " ms";
            break;
        case ModuleType::algorithmicReverb:
            if (control == 0) return juce::String(exponential(0.2f, 12.0f, value), 2) + " s";
            if (control == 1)
            {
                const auto percent = linear(25.0f, 200.0f, value);
                return juce::String(roomScaleName(percent)) + "  |  "
                       + juce::String(percent, 0) + "%";
            }
            if (control == 2 || control == 3)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return juce::String(value * 250.0f, 0) + " ms";
            if (control == 6) return juce::String(value * 100.0f, 0) + "%";
            if (control == 7) return juce::String(value * 18.0f, 1) + " dB";
            if (control == 8) return juce::String(value * 150.0f, 0) + "%";
            if (control == 9) return juce::String(value * 100.0f, 0) + "%";
            if (control == 10) return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 11) return formatFrequency(exponential(2000.0f, 20000.0f, value));
            break;
        case ModuleType::stereoWidth:
            if (control == 0) return juce::String(value * 200.0f, 0) + "%";
            if (control == 1) return juce::String(value * 100.0f, 0) + "%";
            if (control == 2) return formatFrequency(exponential(20.0f, 500.0f, value));
            if (control == 3) return formatFrequency(exponential(500.0f, 8000.0f, value));
            if (control == 4)
            {
                const auto balance = linear(-100.0f, 100.0f, value);
                if (std::abs(balance) < 1.0f)
                    return "C";
                return juce::String(balance < 0.0f ? "L " : "R ")
                       + juce::String(std::abs(balance), 0);
            }
            if (control == 6) return juce::String(linear(-12.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::midSideDecoder:
            if (control == 0) return juce::String(value * 100.0f, 0) + "%";
            break;
        case ModuleType::tremolo:
            if (control == 1) return juce::String(exponential(0.05f, 20.0f, value), 2) + " Hz";
            if (control == 4) return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return juce::String(linear(0.0f, 100.0f, value), 1) + " cents";
            if (control == 6) return juce::String(value * 100.0f, 0) + "%";
            if (control == 7) return juce::String(linear(0.0f, 180.0f, value), 0) + juce::String::charToString(0x00b0);
            if (control == 8) return formatFrequency(exponential(100.0f, 4000.0f, value));
            if (control == 10) return juce::String(linear(-12.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::envelopeModulator:
            if (control == 1 || control == 2)
                return juce::String(exponential(0.1f, 20.0f, value), 2) + " Hz";
            if (control == 3 || control == 4)
                return juce::String(linear(-72.0f, 0.0f, value), 1) + " dBFS";
            if (control == 5)
                return juce::String(exponential(1.0f, 200.0f, value), 1) + " ms";
            if (control == 6)
                return juce::String(exponential(30.0f, 3000.0f, value), 0) + " ms";
            if (control == 7 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8)
                return juce::String(value * 100.0f, 1) + " cents";
            if (control == 11)
                return juce::String(linear(-12.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::rotarySpeaker:
            if (control == 1) return "+" + juce::String(linear(0.0f, 24.0f, value), 1) + " dB";
            if (control == 2)
            {
                const auto balance = linear(-100.0f, 100.0f, value);
                if (std::abs(balance) < 1.0f) return "Equal";
                return juce::String(std::abs(balance), 0) + "% "
                       + (balance < 0.0f ? "Drum" : "Horn");
            }
            if (control == 3) return formatFrequency(exponential(500.0f, 1400.0f, value));
            if (control == 4 || control == 8
                || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 7)
                return juce::String(linear(0.4f, 2.5f, value), 2)
                       + juce::String::charToString(0x00d7);
            if (control == 5) return juce::String(linear(20.0f, 200.0f, value), 0) + " cm";
            if (control == 6) return juce::String(linear(0.0f, 180.0f, value), 0) + juce::String::charToString(0x00b0);
            if (control == 11) return juce::String(linear(-12.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::convolutionReverb:
            if (control == 0) return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 1) return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 2 || control == 4)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3) return juce::String(linear(-18.0f, 18.0f, value), 1) + " dB";
            if (control == 5) return juce::String(value * 18.0f, 1) + " dB";
            break;
        case ModuleType::dynamicEqualizer:
            if (control == 0) return formatFrequency(exponential(20.0f, 20000.0f, value));
            if (control == 1) return "Q " + juce::String(exponential(0.2f, 12.0f, value), 2);
            if (control == 2) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            if (control == 3) return juce::String(linear(-60.0f, 0.0f, value), 1) + " dB";
            if (control == 4) return juce::String(exponential(1.0f, 10.0f, value), 1) + ":1";
            if (control == 5) return juce::String(exponential(0.1f, 100.0f, value), 1) + " ms";
            if (control == 6) return juce::String(exponential(10.0f, 1000.0f, value), 0) + " ms";
            if (control == 11) return juce::String(value * 100.0f, 0) + "%";
            break;
        case ModuleType::randomGranulizer:
            if (control == 0)
            {
                const auto voices =
                    juce::roundToInt(linear(1.0f, 16.0f, value));
                return juce::String(voices)
                       + (voices == 1 ? " voice" : " voices");
            }
            if (control == 1 || control == 4)
                return formatGrainSize(exponential(50.0f, 2000.0f, value));
            if (control == 2)
                return juce::String(exponential(0.5f, 30.0f, value), 1)
                       + " grains/s";
            if (control == 3)
                return formatGrainSize(value * 6000.0f);
            if (control == 10 || control == 11)
                return juce::String(juce::roundToInt(value * 100.0f)) + "%";
            if (control == 5 || control == 6 || control == 7)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8)
                return formatFrequency(exponential(500.0f, 20000.0f, value));
            if (control == 9)
                return juce::String(value * 80.0f, 0) + "%";
            break;
        case ModuleType::vintageChorus:
            if (control == 1) return juce::String(exponential(0.05f, 8.0f, value), 2) + " Hz";
            if (control == 2 || control == 8 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3) return juce::String(exponential(2.0f, 30.0f, value), 1) + " ms";
            if (control == 4)
            {
                const auto voices =
                    juce::roundToInt(linear(1.0f, 6.0f, value));
                return juce::String(voices)
                       + (voices == 1 ? " voice" : " voices");
            }
            if (control == 5) return juce::String(value * 200.0f, 0) + "%";
            if (control == 6)
            {
                const auto regeneration = linear(-75.0f, 75.0f, value);
                return (regeneration > 0.5f ? "+" : "")
                       + juce::String(regeneration, 0) + "%";
            }
            if (control == 7) return formatFrequency(exponential(800.0f, 18000.0f, value));
            if (control == 9) return juce::String(value * 180.0f, 0) + juce::String::charToString(0x00b0);
            if (control == 11) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::beatPermuter:
            if (control == 1 || control == 7 || control == 8 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3 || control == 4)
                return juce::String(juce::roundToInt(linear(1.0f, 8.0f, value)));
            if (control == 5)
                return juce::String(linear(20.0f, 100.0f, value), 0) + "%";
            if (control == 6)
            {
                const auto pitch = linear(-12.0f, 12.0f, value);
                return (pitch > 0.05f ? "+" : "") + juce::String(pitch, 1) + " st";
            }
            if (control == 9)
                return juce::String(value * 75.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::portalPermuter:
            if (control == 1 || control == 3 || control == 4
                || control == 5 || control == 7 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 6)
            {
                const auto semitones = linear(-12.0f, 12.0f, value);
                return (semitones > 0.05f ? "+" : "")
                    + juce::String(semitones, 1) + " st";
            }
            if (control == 9)
                return juce::String(value * 55.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::irMosaic:
            if (control == 5 || control == 6 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 1)
                return juce::String(exponential(0.03f, 4.0f, value), 2)
                    + " Hz";
            if (control == 3)
                return juce::String(
                    exponential(0.05f, 0.5f, value) * 1000.0f, 0)
                    + " ms";
            if (control == 7)
                return juce::String(value * 55.0f, 0) + "%";
            if (control == 8)
                return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 9)
                return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::spectralPrism:
            if (control == 0)
                return juce::String(linear(-100.0f, 100.0f, value), 0) + "%";
            if (control == 1)
                return formatFrequency(exponential(80.0f, 8000.0f, value));
            if (control == 2)
            {
                const auto shift = linear(-24.0f, 24.0f, value);
                return (shift > 0.05f ? "+" : "") + juce::String(shift, 1) + " st";
            }
            if (control == 5)
                return juce::String(exponential(0.02f, 4.0f, value), 2) + " Hz";
            if (control == 3 || (control >= 6 && control <= 10))
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::resonantMatrix:
            if (control == 0)
                return formatTunedFrequency(exponential(27.5f, 440.0f, value));
            if (control == 2)
                return juce::String(linear(1.0f, 4.0f, value), 1) + " oct";
            if (control == 4)
                return juce::String(exponential(0.10f, 12.0f, value), 2) + " s";
            if (control == 5)
                return formatFrequency(exponential(500.0f, 20000.0f, value));
            if (control == 6)
                return juce::String(linear(0.0f, 30.0f, value), 1) + " cents";
            if (control == 7)
                return juce::String(exponential(0.02f, 2.0f, value), 2) + " Hz";
            if (control == 8)
                return juce::String(linear(0.0f, 50.0f, value), 1) + " cents";
            if (control == 9)
                return juce::String(linear(0.0f, 150.0f, value), 0) + "%";
            if (control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::wavefoldGarden:
            if (control == 1)
                return "+" + juce::String(linear(0.0f, 36.0f, value), 1) + " dB";
            if (control == 2)
                return juce::String(juce::roundToInt(linear(1.0f, 8.0f, value)));
            if (control == 3 || control == 5)
            {
                const auto bipolar = linear(-100.0f, 100.0f, value);
                return (bipolar > 0.5f ? "+" : "") + juce::String(bipolar, 0) + "%";
            }
            if (control == 4 || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 6)
                return juce::String(exponential(0.1f, 100.0f, value), 1) + " ms";
            if (control == 7)
                return juce::String(exponential(10.0f, 1000.0f, value), 0) + " ms";
            if (control == 8)
                return formatFrequency(exponential(500.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::gateExpander:
            if (control == 0) return juce::String(linear(-80.0f, 0.0f, value), 1) + " dB";
            if (control == 1) return juce::String(linear(0.0f, 80.0f, value), 1) + " dB";
            if (control == 2) return juce::String(exponential(0.05f, 100.0f, value), 2) + " ms";
            if (control == 3) return juce::String(linear(0.0f, 500.0f, value), 0) + " ms";
            if (control == 4) return formatGrainSize(exponential(5.0f, 2000.0f, value));
            if (control == 5) return juce::String(linear(0.0f, 18.0f, value), 1) + " dB";
            if (control == 6) return formatFrequency(exponential(20.0f, 2000.0f, value));
            if (control == 7) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 10) return juce::String(value * 100.0f, 0) + "%";
            break;
        case ModuleType::transientDesigner:
            if (control == 0 || control == 1)
            {
                const auto amount = linear(-100.0f, 100.0f, value);
                return (amount > 0.5f ? "+" : "")
                       + juce::String(amount, 0) + "%";
            }
            if (control == 2 || control == 6)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3)
                return juce::String(exponential(5.0f, 200.0f, value), 1) + " ms";
            if (control == 4)
                return formatFrequency(exponential(80.0f, 8000.0f, value));
            if (control == 7)
                return juce::String(linear(-18.0f, 18.0f, value), 1) + " dB";
            break;
        case ModuleType::multibandCompressor:
            if (control == 0) return formatFrequency(exponential(40.0f, 800.0f, value));
            if (control == 1) return formatFrequency(exponential(1000.0f, 12000.0f, value));
            if (control >= 2 && control <= 4)
                return juce::String(linear(-60.0f, 0.0f, value), 1) + " dB";
            if (control == 5) return juce::String(exponential(1.0f, 20.0f, value), 1) + ":1";
            if (control == 6) return juce::String(exponential(0.1f, 100.0f, value), 1) + " ms";
            if (control == 7) return formatGrainSize(exponential(20.0f, 2000.0f, value));
            if (control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::studioPhaser:
            if (control == 1) return juce::String(exponential(0.02f, 12.0f, value), 2) + " Hz";
            if (control == 4 || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return formatFrequency(exponential(80.0f, 8000.0f, value));
            if (control == 6) return juce::String(exponential(0.25f, 6.0f, value), 2) + " oct";
            if (control == 7)
            {
                const auto feedback = linear(-95.0f, 95.0f, value);
                return (feedback > 0.5f ? "+" : "")
                       + juce::String(feedback, 0) + "%";
            }
            if (control == 8)
                return juce::String(linear(0.0f, 180.0f, value), 0)
                       + juce::String::charToString(0x00b0);
            if (control == 10)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::studioFlanger:
            if (control == 1) return juce::String(exponential(0.02f, 12.0f, value), 2) + " Hz";
            if (control == 4) return juce::String(linear(0.0f, 10.0f, value), 2) + " ms";
            if (control == 5) return juce::String(exponential(0.1f, 15.0f, value), 2) + " ms";
            if (control == 6)
            {
                const auto feedback = linear(-90.0f, 90.0f, value);
                return (feedback > 0.5f ? "+" : "")
                       + juce::String(feedback, 0) + "%";
            }
            if (control == 7)
                return juce::String(linear(0.0f, 180.0f, value), 0)
                       + juce::String::charToString(0x00b0);
            if (control == 8) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 9) return juce::String(value * 100.0f, 0) + "%";
            if (control == 10)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::diffusionDelay:
            if (control == 0) return formatGrainSize(exponential(10.0f, 2000.0f, value));
            if (control == 3) return juce::String(value * 90.0f, 0) + "%";
            if (control == 4 || control == 5 || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 6) return formatFrequency(exponential(20.0f, 2000.0f, value));
            if (control == 7) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 8) return juce::String(value * 150.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::pitchBloom:
            if (control == 1)
            {
                const auto cents = linear(-50.0f, 50.0f, value);
                return (cents > 0.05f ? "+" : "")
                       + juce::String(cents, 1) + " cents";
            }
            if (control == 2) return juce::String(exponential(20.0f, 500.0f, value), 0) + " ms";
            if (control == 3) return juce::String(value * 85.0f, 0) + "%";
            if (control == 4 || control == 5 || control == 8 || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 6) return formatFrequency(exponential(20.0f, 2000.0f, value));
            if (control == 7) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 10)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::frequencyLab:
            if (control == 3)
                return formatSeconds(exponential(0.08f, 4.0f, value));
            if (control == 4 || control == 7 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5)
                return juce::String(
                    exponential(0.02f, 4.0f, value), 2) + " Hz";
            if (control == 6)
                return juce::String(value * 25.0f, 1) + " cents";
            if (control == 8)
                return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 9)
                return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::spatialOrbit:
            if (control == 1) return juce::String(exponential(0.02f, 5.0f, value), 2) + " Hz";
            if (control == 4) return juce::String(linear(0.0f, 360.0f, value), 0) + juce::String::charToString(0x00b0);
            if (control == 5) return juce::String(value * 200.0f, 0) + "%";
            if (control == 6) return juce::String(exponential(0.5f, 10.0f, value), 2) + " m";
            if (control == 7 || control == 8 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 9) return formatFrequency(exponential(20.0f, 500.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::signalDecay:
            if (control == 0)
            {
                const auto bits = juce::roundToInt(linear(4.0f, 24.0f, value));
                return juce::String(bits) + (bits == 1 ? " bit" : " bits");
            }
            if (control == 1) return juce::String(exponential(1.0f, 48.0f, value), 1) + " kHz";
            if (control == 2 || control == 3 || (control >= 6 && control <= 9))
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 4) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 5) return juce::String(linear(-90.0f, -24.0f, value), 1) + " dBFS";
            if (control == 10)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::analogTape:
            if (control == 1)
                return juce::String(linear(-18.0f, 18.0f, value), 1) + " dB";
            if (control == 2 || control == 5 || control == 6 || control == 7
                || control == 8 || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3)
            {
                const auto bias = linear(-100.0f, 100.0f, value);
                if (std::abs(bias) < 1.0f)
                    return "Neutral";
                return (bias < 0.0f ? "Under " : "Over ")
                       + juce::String(std::abs(bias), 0) + "%";
            }
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::resonanceTamer:
            if (control == 0) return juce::String(value * 18.0f, 1) + " dB";
            if (control == 3) return juce::String(linear(-3.0f, 3.0f, value), 1) + " dB/oct";
            if (control == 4) return formatFrequency(exponential(20.0f, 2000.0f, value));
            if (control == 5) return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 6 || control == 7)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::spectralBalance:
            if (control == 1 || control == 7)
                return juce::String(value * 100.0f, 0) + "%";
            if (control >= 2 && control <= 4)
                return juce::String(linear(-6.0f, 6.0f, value), 1) + " dB";
            if (control == 5) return formatSeconds(exponential(0.5f, 30.0f, value));
            if (control == 8) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::phaseCoherence:
            if (control == 1) return formatFrequency(exponential(40.0f, 800.0f, value));
            if (control == 2 || control == 5)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3) return juce::String(value * 2.0f, 2) + " ms";
            if (control == 4)
                return juce::String(value * 180.0f, 0)
                       + juce::String::charToString(0x00b0);
            if (control == 6) return formatFrequency(exponential(20.0f, 500.0f, value));
            if (control == 7) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::loudnessRider:
            if (control == 0) return juce::String(linear(-36.0f, -8.0f, value), 1) + " LUFS";
            if (control == 1) return juce::String(value * 18.0f, 1) + " dB";
            if (control == 3) return formatSeconds(exponential(0.25f, 10.0f, value));
            if (control == 4) return juce::String(value * 250.0f, 0) + " ms";
            if (control == 5 || control == 6)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 7) return juce::String(linear(-70.0f, -30.0f, value), 1) + " LUFS";
            if (control == 8) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::adaptiveClipper:
            if (control == 0) return juce::String(value * 24.0f, 1) + " dB";
            if (control == 1) return juce::String(linear(-12.0f, 0.0f, value), 1) + " dBTP";
            if (control == 3 || control == 4 || control == 6 || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return juce::String(exponential(20.0f, 1000.0f, value), 0) + " ms";
            if (control == 10) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::spectralDelayCanvas:
            if (control == 1) return formatGrainSize(exponential(10.0f, 4000.0f, value));
            if (control >= 3 && control <= 5)
                return juce::String(value * 200.0f, 0) + "%";
            if (control == 6) return juce::String(value * 90.0f, 0) + "%";
            if (control == 7) return juce::String(value * 70.0f, 0) + "%";
            if (control == 8 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::formantForge:
            if (control == 1 || control == 2 || control == 4 || control == 5
                || control == 7 || control == 8 || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3)
                return juce::String(linear(-24.0f, 24.0f, value), 1) + " st";
            if (control == 6) return juce::String(exponential(0.02f, 6.0f, value), 2) + " Hz";
            if (control == 10) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::harmonicMirage:
            if (control == 2) return juce::String(
                juce::roundToInt(linear(2.0f, 24.0f, value)));
            if (control == 3)
                return juce::String(linear(-100.0f, 100.0f, value), 0) + "%";
            if (control == 4 || control == 7 || control == 8 || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return juce::String(value * 30.0f, 1) + " cents";
            if (control == 6) return formatSeconds(exponential(0.02f, 2.0f, value));
            if (control == 10) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::chaosField:
            if (control == 1) return juce::String(exponential(0.015f, 7.0f, value), 2) + " Hz";
            if (control == 4 || control == 8 || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return formatFrequency(exponential(80.0f, 10000.0f, value));
            if (control == 6) return juce::String(exponential(2.0f, 600.0f, value), 1) + " ms";
            if (control == 7) return juce::String(linear(-88.0f, 88.0f, value), 0) + "%";
            if (control == 11) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::timeMosaic:
            if (control == 0) return formatSeconds(exponential(0.25f, 8.0f, value));
            if (control == 1)
                return juce::String(
                    exponential(1.0f / 24.0f, 3.0f, value), 2) + " oct";
            if (control == 2)
                return juce::String(
                    exponential(0.01f, 0.5f, value) * 1000.0f, 0) + " ms";
            if (control == 3 || control == 4 || control == 5 || control == 8
                || control == 9)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 6) return juce::String(value * 50.0f, 1) + " cents";
            if (control == 10) return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::guitarCabinet:
            if (control == 1)
                return formatFrequency(exponential(30.0f, 300.0f, value));
            if (control == 2)
                return formatFrequency(exponential(2500.0f, 18000.0f, value));
            if (control == 3 || control == 4)
                return juce::String(linear(-6.0f, 6.0f, value), 1) + " dB";
            if (control == 5)
                return juce::String(linear(0.0f, 18.0f, value), 1) + " dB";
            if (control == 6)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 7)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::chordLoom:
            if (control == 0)
                return juce::String(linear(-24.0f, -60.0f, value), 1) + " dB";
            if (control == 3 || control == 6 || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 1)
                return formatSeconds(exponential(0.25f, 2.0f, value));
            if (control == 2)
                return formatSeconds(exponential(0.10f, 2.0f, value));
            if (control == 4)
                return juce::String(value * 20.0f, 1) + " cents";
            if (control == 5)
                return juce::String(value * 50.0f, 0) + "%";
            if (control == 7)
                return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 8)
                return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::volumeSwell:
            if (control == 0)
                return juce::String(linear(-24.0f, -60.0f, value), 1) + " dB";
            if (control == 1)
                return formatSeconds(exponential(0.05f, 5.0f, value));
            if (control == 2)
            {
                if (value < 0.495f)
                    return "Fast "
                        + juce::String((0.5f - value) * 200.0f, 0) + "%";
                if (value > 0.505f)
                    return "Slow "
                        + juce::String((value - 0.5f) * 200.0f, 0) + "%";
                return "Linear";
            }
            if (control == 3)
                return juce::String(linear(-60.0f, -6.0f, value), 1) + " dB";
            if (control == 4)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::spectralNebula:
            if (control == 0 || control == 3 || control == 6
                || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 1)
                return formatSeconds(exponential(0.2f, 30.0f, value));
            if (control == 2)
                return juce::String(value * 40.0f, 1) + " cents";
            if (control == 4)
                return value < 0.495f
                    ? "Noise "
                        + juce::String((0.5f - value) * 200.0f, 0) + "%"
                    : value > 0.505f
                        ? "Tonal "
                            + juce::String((value - 0.5f) * 200.0f, 0) + "%"
                        : "Balanced";
            if (control == 5)
                return value >= 0.5f ? "Frozen" : "Absorb";
            if (control == 7)
                return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 8)
                return formatFrequency(exponential(1000.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::neuralAmpModeler:
            if (control == 0 || control == 4)
                return juce::String(linear(-24.0f, 24.0f, value), 1) + " dB";
            if (control >= 1 && control <= 3)
            {
                const auto gain = linear(-12.0f, 12.0f, value);
                return (gain > 0.05f ? "+" : "")
                    + juce::String(gain, 1) + " dB";
            }
            break;
        case ModuleType::stageVocal:
            if (control == 0)
                return formatFrequency(exponential(40.0f, 220.0f, value));
            if (control == 1 || control == 4)
                return juce::String(linear(0.0f, 30.0f, value), 1)
                    + " dB";
            if (control == 2)
                return juce::String(exponential(0.02f, 0.8f, value), 2)
                    + " ms";
            if (control == 3)
                return juce::String(exponential(40.0f, 800.0f, value), 0)
                    + " ms";
            if (control == 5)
                return juce::String(exponential(2.0f, 40.0f, value), 1)
                    + " ms";
            if (control == 6)
                return juce::String(exponential(200.0f, 3000.0f, value), 0)
                    + " ms";
            if (control == 7)
                return formatFrequency(
                    exponential(3000.0f, 10000.0f, value));
            if (control == 8)
                return juce::String(linear(0.0f, 12.0f, value), 1)
                    + " dB";
            if (control == 9 || control == 10)
            {
                const auto gain = linear(-6.0f, 6.0f, value);
                return (gain > 0.05f ? "+" : "")
                    + juce::String(gain, 1) + " dB";
            }
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1)
                    + " dB";
            break;
        case ModuleType::vinylSimulator:
            if (control >= 0 && control <= 3)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 4)
                return juce::String(linear(-90.0f, -34.0f, value), 1)
                    + " dB";
            if (control == 5)
                return juce::String(linear(-90.0f, -36.0f, value), 1)
                    + " dB";
            if (control == 6)
                return juce::String(value * 3.8f, 2) + " ms";
            if (control == 7)
                return juce::String(value * 0.65f, 2) + " ms";
            if (control >= 8 && control <= 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1)
                    + " dB";
            break;
        case ModuleType::optoLeveler:
            if (control == 0 || control == 3 || control == 5
                || control == 6 || control == 7)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 1)
                return juce::String(linear(-12.0f, 24.0f, value), 1) + " dB";
            if (control == 4)
                return formatFrequency(exponential(20.0f, 300.0f, value));
            if (control == 8)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::fetLimiter:
            if (control == 0 || control == 1)
                return juce::String(linear(-24.0f, 24.0f, value), 1) + " dB";
            if (control == 2)
                return juce::String(exponential(20.0f, 800.0f, value), 0)
                    + " us";
            if (control == 3)
                return juce::String(exponential(50.0f, 1100.0f, value), 0)
                    + " ms";
            if (control == 5)
                return formatFrequency(exponential(20.0f, 300.0f, value));
            if (control == 6 || control == 7)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::programEqualizer:
            if (control == 1)
                return juce::String(linear(0.0f, 14.0f, value), 1) + " dB";
            if (control == 2 || control == 4 || control == 7)
                return juce::String(linear(0.0f, 18.0f, value), 1) + " dB";
            if (control == 5)
                return juce::String(value * 100.0f, 0) + "% sharp";
            if (control == 8)
                return juce::String(linear(-12.0f, 18.0f, value), 1) + " dB";
            if (control == 9)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::studioPlate:
            if (control == 1)
                return juce::String(exponential(0.5f, 8.0f, value), 2) + " s";
            if (control == 2 || control == 6 || control == 7
                || control == 9 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 3)
                return juce::String(exponential(1.0f, 200.0f, value), 1)
                    + " ms";
            if (control == 4)
                return formatFrequency(exponential(20.0f, 800.0f, value));
            if (control == 5)
                return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 8)
                return juce::String(linear(0.0f, 18.0f, value), 1) + " dB";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::velvetDrive:
            if (control == 2 || control == 3
                || (control >= 5 && control <= 9))
                return juce::String(value * 10.0f, 1);
            if (control == 4)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            if (control == 11)
                return juce::String(linear(-24.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::wormholeConvolution:
            if (control == 0 || control == 1
                || control == 4 || control == 5
                || control == 3 || control == 7 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8)
                return formatFrequency(exponential(20.0f, 1000.0f, value));
            if (control == 9)
                return formatFrequency(exponential(2000.0f, 20000.0f, value));
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::bitcrusher:
            if (control == 0)
                return juce::String(linear(2.0f, 24.0f, value), 1)
                    + " bits";
            if (control == 1)
                return formatFrequency(
                    exponential(250.0f, 768000.0f, value));
            if (control == 8)
            {
                const auto bias = linear(-1.0f, 1.0f, value);
                return (bias > 0.005f ? "+" : "")
                    + juce::String(bias, 2) + " LSB";
            }
            if (control == 9)
                return juce::String(value * 24.0f, 1) + " dB";
            if ((control >= 3 && control <= 7) || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::midnightCurrent:
            if (control == 0)
                return formatFrequency(exponential(120.0f, 3000.0f, value));
            if (control == 2)
                return juce::String(exponential(0.02f, 1.2f, value), 2)
                    + " Hz";
            if (control == 4)
            {
                const auto regeneration = linear(-80.0f, 80.0f, value);
                return (regeneration > 0.5f ? "+" : "")
                    + juce::String(regeneration, 0) + "%";
            }
            if (control == 1 || (control >= 3 && control <= 10))
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::autoWah:
            if (control == 0)
                return juce::String(linear(-18.0f, -66.0f, value), 1) + " dB";
            if (control == 1)
                return formatFrequency(exponential(120.0f, 1600.0f, value));
            if (control == 2)
                return juce::String(linear(0.5f, 5.0f, value), 2) + " oct";
            if (control == 3)
                return juce::String(exponential(0.5f, 80.0f, value), 1) + " ms";
            if (control == 4)
                return formatGrainSize(exponential(30.0f, 1400.0f, value));
            if (control == 5 || (control >= 7 && control <= 10))
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::resonantSculpture:
            if (control == 2)
            {
                const auto offset = linear(-24.0f, 24.0f, value);
                return (offset > 0.05f ? "+" : "")
                    + juce::String(offset, 1) + " st";
            }
            if (control == 4)
                return formatSeconds(exponential(0.15f, 12.0f, value));
            if (control >= 1 && control <= 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
            break;
        case ModuleType::guitarTuner:
            if (control == 0)
                return juce::String(linear(430.0f, 450.0f, value), 1) + " Hz";
            if (control == 3)
            {
                const auto transpose = juce::roundToInt(
                    linear(-12.0f, 12.0f, value));
                return (transpose > 0 ? "+" : "")
                    + juce::String(transpose) + " st";
            }
            if (control == 4)
                return juce::String(linear(-24.0f, 24.0f, value), 1) + " dB";
            if (control == 5)
                return juce::String(linear(-72.0f, -24.0f, value), 1) + " dB";
            if (control == 6)
                return juce::String(
                    exponential(0.04f, 0.60f, value) * 1000.0f, 0) + " ms";
            if (control == 8)
                return juce::String(linear(0.5f, 5.0f, value), 2) + "x";
            if (control == 10)
                return juce::String(linear(-24.0f, 6.0f, value), 1) + " dB";
            if (control == 11)
                return juce::String(linear(0.0f, 2.0f, value), 2) + " s";
            break;
        case ModuleType::empty: break;
    }
    return juce::String(value * 100.0f, 0) + "%";
}

std::optional<float> parseControlValue(ModuleType type, int control,
                                       const juce::String& text)
{
    const auto options = controlOptions(type, control);
    if (!options.isEmpty())
    {
        for (int index = 0; index < options.size(); ++index)
            if (text.trim().equalsIgnoreCase(options[index]))
                return discreteValue(index, options.size());
        return std::nullopt;
    }
    if (type == ModuleType::stereoWidth && control == 4
        && (text.trim().equalsIgnoreCase("center")
            || text.trim().equalsIgnoreCase("centre")
            || text.trim().equalsIgnoreCase("c")))
        return 0.5f;
    if (type == ModuleType::rotarySpeaker && control == 2
        && text.trim().equalsIgnoreCase("equal"))
        return 0.5f;
    if (type == ModuleType::analogTape && control == 3
        && text.trim().equalsIgnoreCase("neutral"))
        return 0.5f;
    if (type == ModuleType::algorithmicReverb && control == 1)
        if (const auto named = namedRoomScale(text))
            return named;
    if (type == ModuleType::spectralNebula && control == 4
        && (text.trim().equalsIgnoreCase("full")
            || text.trim().equalsIgnoreCase("balanced")))
        return 0.5f;

    const auto parsedValue = numericValue(text);
    if (!parsedValue.has_value())
        return std::nullopt;
    const auto value = *parsedValue;
    switch (type)
    {
        case ModuleType::equalizer:
            if (control == 0) return exponentialNormalized(30.0f, 1200.0f, parsedFrequency(text, value));
            if (control == 3) return exponentialNormalized(150.0f, 7000.0f, parsedFrequency(text, value));
            if (control == 6) return exponentialNormalized(1500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 1 || control == 4 || control == 7 || control == 9)
                return linearNormalized(-18.0f, 18.0f, value);
            if (control == 2 || control == 5 || control == 8)
                return exponentialNormalized(0.2f, 10.0f, value);
            break;
        case ModuleType::compressor:
            if (control == 0) return linearNormalized(-60.0f, 0.0f, value);
            if (control == 1) return exponentialNormalized(1.0f, 20.0f, value);
            if (control == 2) return exponentialNormalized(0.1f, 100.0f, value);
            if (control == 3) return exponentialNormalized(10.0f, 1000.0f, value);
            if (control == 4) return linearNormalized(0.0f, 18.0f, value);
            if (control == 5) return linearNormalized(0.0f, 24.0f, value);
            break;
        case ModuleType::saturator:
            if (control == 0) return linearNormalized(0.0f, 36.0f, value);
            if (control == 1) return exponentialNormalized(
                500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 2) return linearNormalized(-100.0f, 100.0f, value);
            if (control == 3) return linearNormalized(-24.0f, 6.0f, value);
            break;
        case ModuleType::delay:
            if (control == 0) return exponentialNormalized(1.0f, 2000.0f, value);
            if (control == 3) return juce::jlimit(0.0f, 1.0f, value / 94.0f);
            if (control == 5 || control == 7 || control == 8
                || control == 9 || control == 11)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 18.0f);
            break;
        case ModuleType::limiter:
            if (control == 0) return linearNormalized(-24.0f, 0.0f, value);
            if (control == 1) return linearNormalized(-12.0f, 0.0f, value);
            if (control == 2) return exponentialNormalized(10.0f, 1000.0f, value);
            if (control == 3) return linearNormalized(1.0f, 10.0f, value);
            break;
        case ModuleType::algorithmicReverb:
            if (control == 0) return exponentialNormalized(0.2f, 12.0f, value);
            if (control == 1) return linearNormalized(25.0f, 200.0f, value);
            if (control == 2 || control == 3)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5) return juce::jlimit(0.0f, 1.0f, value / 250.0f);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 7)
                return linearNormalized(0.0f, 18.0f, value);
            if (control == 8) return juce::jlimit(0.0f, 1.0f, value / 150.0f);
            if (control == 9) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 10) return exponentialNormalized(
                20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 11) return exponentialNormalized(
                2000.0f, 20000.0f, parsedFrequency(text, value));
            break;
        case ModuleType::stereoWidth:
            if (control == 0) return juce::jlimit(0.0f, 1.0f, value / 200.0f);
            if (control == 1) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 2) return exponentialNormalized(
                20.0f, 500.0f, parsedFrequency(text, value));
            if (control == 3) return exponentialNormalized(
                500.0f, 8000.0f, parsedFrequency(text, value));
            if (control == 4)
            {
                const auto amount = juce::jlimit(0.0f, 100.0f, value);
                const auto trimmed = text.trim();
                if (trimmed.startsWithIgnoreCase("l")
                    || text.containsIgnoreCase("left"))
                    return linearNormalized(-100.0f, 100.0f, -amount);
                if (trimmed.startsWithIgnoreCase("r")
                    || text.containsIgnoreCase("right"))
                    return linearNormalized(-100.0f, 100.0f, amount);
                return linearNormalized(-100.0f, 100.0f, value);
            }
            if (control == 6) return linearNormalized(-12.0f, 12.0f, value);
            break;
        case ModuleType::midSideDecoder:
            if (control == 0) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            break;
        case ModuleType::tremolo:
            if (control == 1) return exponentialNormalized(0.05f, 20.0f, value);
            if (control == 4 || control == 6 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5) return linearNormalized(0.0f, 100.0f, value);
            if (control == 7) return linearNormalized(0.0f, 180.0f, value);
            if (control == 8) return exponentialNormalized(
                100.0f, 4000.0f, parsedFrequency(text, value));
            if (control == 10) return linearNormalized(-12.0f, 12.0f, value);
            break;
        case ModuleType::envelopeModulator:
            if (control == 1 || control == 2)
                return exponentialNormalized(0.1f, 20.0f, value);
            if (control == 3 || control == 4)
                return linearNormalized(-72.0f, 0.0f, value);
            if (control == 5)
                return exponentialNormalized(1.0f, 200.0f, value);
            if (control == 6)
                return exponentialNormalized(30.0f, 3000.0f, value);
            if (control == 7 || control == 8 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-12.0f, 12.0f, value);
            break;
        case ModuleType::rotarySpeaker:
            if (control == 1) return linearNormalized(0.0f, 24.0f, value);
            if (control == 2)
            {
                const auto amount = juce::jlimit(0.0f, 100.0f, value);
                if (text.containsIgnoreCase("drum"))
                    return linearNormalized(-100.0f, 100.0f, -amount);
                if (text.containsIgnoreCase("horn"))
                    return linearNormalized(-100.0f, 100.0f, amount);
                return 0.5f;
            }
            if (control == 3) return exponentialNormalized(
                500.0f, 1400.0f, parsedFrequency(text, value));
            if (control == 4 || control == 8
                || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 7) return linearNormalized(0.4f, 2.5f, value);
            if (control == 5) return linearNormalized(20.0f, 200.0f, value);
            if (control == 6) return linearNormalized(0.0f, 180.0f, value);
            if (control == 11) return linearNormalized(-12.0f, 12.0f, value);
            break;
        case ModuleType::convolutionReverb:
            if (control == 0) return exponentialNormalized(
                20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 1) return exponentialNormalized(
                2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 2 || control == 4)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3) return linearNormalized(-18.0f, 18.0f, value);
            if (control == 5) return linearNormalized(0.0f, 18.0f, value);
            break;
        case ModuleType::dynamicEqualizer:
            if (control == 0) return exponentialNormalized(
                20.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 1) return exponentialNormalized(0.2f, 12.0f, value);
            if (control == 2) return linearNormalized(-18.0f, 12.0f, value);
            if (control == 3) return linearNormalized(-60.0f, 0.0f, value);
            if (control == 4) return exponentialNormalized(1.0f, 10.0f, value);
            if (control == 5) return exponentialNormalized(0.1f, 100.0f, value);
            if (control == 6) return exponentialNormalized(10.0f, 1000.0f, value);
            if (control == 11) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            break;
        case ModuleType::randomGranulizer:
            if (control == 0) return linearNormalized(1.0f, 16.0f, value);
            if (control == 1 || control == 4)
                return exponentialNormalized(
                    50.0f, 2000.0f, parsedMilliseconds(text, value));
            if (control == 2) return exponentialNormalized(0.5f, 30.0f, value);
            if (control == 3)
                return juce::jlimit(
                    0.0f, 1.0f, parsedMilliseconds(text, value) / 6000.0f);
            if (control == 5 || control == 6 || control == 7
                || control == 10 || control == 11)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8) return exponentialNormalized(
                500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 80.0f);
            break;
        case ModuleType::vintageChorus:
            if (control == 1) return exponentialNormalized(0.05f, 8.0f, value);
            if (control == 2 || control == 8 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3) return exponentialNormalized(
                2.0f, 30.0f, parsedMilliseconds(text, value));
            if (control == 4) return linearNormalized(1.0f, 6.0f, value);
            if (control == 5) return juce::jlimit(0.0f, 1.0f, value / 200.0f);
            if (control == 6) return linearNormalized(-75.0f, 75.0f, value);
            if (control == 7) return exponentialNormalized(
                800.0f, 18000.0f, parsedFrequency(text, value));
            if (control == 9) return linearNormalized(0.0f, 180.0f, value);
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::beatPermuter:
            if (control == 1 || control == 7 || control == 8 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3 || control == 4)
                return linearNormalized(1.0f, 8.0f, value);
            if (control == 5)
                return linearNormalized(20.0f, 100.0f, value);
            if (control == 6)
                return linearNormalized(-12.0f, 12.0f, value);
            if (control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 75.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::portalPermuter:
            if (control == 1 || control == 3 || control == 4
                || control == 5 || control == 7 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 6)
                return linearNormalized(-12.0f, 12.0f, value);
            if (control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 55.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::irMosaic:
            if (control == 5 || control == 6 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 1)
                return exponentialNormalized(0.03f, 4.0f, value);
            if (control == 3)
                return exponentialNormalized(
                    0.05f, 0.5f,
                    parsedMilliseconds(text, value) * 0.001f);
            if (control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 55.0f);
            if (control == 8)
                return exponentialNormalized(
                    20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 9)
                return exponentialNormalized(
                    2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::spectralPrism:
            if (control == 0)
                return linearNormalized(-100.0f, 100.0f, value);
            if (control == 1)
                return exponentialNormalized(
                    80.0f, 8000.0f, parsedFrequency(text, value));
            if (control == 2)
                return linearNormalized(-24.0f, 24.0f, value);
            if (control == 3 || (control >= 6 && control <= 10))
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5)
                return exponentialNormalized(0.02f, 4.0f, value);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::resonantMatrix:
            if (control == 0)
                return exponentialNormalized(27.5f, 440.0f, value);
            if (control == 2)
                return linearNormalized(1.0f, 4.0f, value);
            if (control == 4)
                return exponentialNormalized(0.10f, 12.0f, value);
            if (control == 5)
                return exponentialNormalized(
                    500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 6)
                return linearNormalized(0.0f, 30.0f, value);
            if (control == 7)
                return exponentialNormalized(0.02f, 2.0f, value);
            if (control == 8)
                return linearNormalized(0.0f, 50.0f, value);
            if (control == 9)
                return linearNormalized(0.0f, 150.0f, value);
            if (control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::wavefoldGarden:
            if (control == 1)
                return linearNormalized(0.0f, 36.0f, value);
            if (control == 2)
                return linearNormalized(1.0f, 8.0f, value);
            if (control == 3 || control == 5)
                return linearNormalized(-100.0f, 100.0f, value);
            if (control == 4 || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 6)
                return exponentialNormalized(0.1f, 100.0f, value);
            if (control == 7)
                return exponentialNormalized(10.0f, 1000.0f, value);
            if (control == 8)
                return exponentialNormalized(
                    500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::gateExpander:
            if (control == 0) return linearNormalized(-80.0f, 0.0f, value);
            if (control == 1) return linearNormalized(0.0f, 80.0f, value);
            if (control == 2) return exponentialNormalized(0.05f, 100.0f, value);
            if (control == 3) return linearNormalized(0.0f, 500.0f, parsedMilliseconds(text, value));
            if (control == 4) return exponentialNormalized(5.0f, 2000.0f, parsedMilliseconds(text, value));
            if (control == 5) return linearNormalized(0.0f, 18.0f, value);
            if (control == 6) return exponentialNormalized(
                20.0f, 2000.0f, parsedFrequency(text, value));
            if (control == 7) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 10) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            break;
        case ModuleType::transientDesigner:
            if (control == 0 || control == 1)
                return linearNormalized(-100.0f, 100.0f, value);
            if (control == 2 || control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3) return exponentialNormalized(5.0f, 200.0f, value);
            if (control == 4) return exponentialNormalized(
                80.0f, 8000.0f, parsedFrequency(text, value));
            if (control == 7) return linearNormalized(-18.0f, 18.0f, value);
            break;
        case ModuleType::multibandCompressor:
            if (control == 0) return exponentialNormalized(
                40.0f, 800.0f, parsedFrequency(text, value));
            if (control == 1) return exponentialNormalized(
                1000.0f, 12000.0f, parsedFrequency(text, value));
            if (control >= 2 && control <= 4)
                return linearNormalized(-60.0f, 0.0f, value);
            if (control == 5) return exponentialNormalized(1.0f, 20.0f, value);
            if (control == 6) return exponentialNormalized(0.1f, 100.0f, value);
            if (control == 7) return exponentialNormalized(
                20.0f, 2000.0f, parsedMilliseconds(text, value));
            if (control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::studioPhaser:
            if (control == 1) return exponentialNormalized(0.02f, 12.0f, value);
            if (control == 4 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5) return exponentialNormalized(
                80.0f, 8000.0f, parsedFrequency(text, value));
            if (control == 6) return exponentialNormalized(0.25f, 6.0f, value);
            if (control == 7) return linearNormalized(-95.0f, 95.0f, value);
            if (control == 8) return linearNormalized(0.0f, 180.0f, value);
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::studioFlanger:
            if (control == 1) return exponentialNormalized(0.02f, 12.0f, value);
            if (control == 4) return linearNormalized(0.0f, 10.0f, parsedMilliseconds(text, value));
            if (control == 5) return exponentialNormalized(0.1f, 15.0f, parsedMilliseconds(text, value));
            if (control == 6) return linearNormalized(-90.0f, 90.0f, value);
            if (control == 7) return linearNormalized(0.0f, 180.0f, value);
            if (control == 8) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 9) return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::diffusionDelay:
            if (control == 0) return exponentialNormalized(
                10.0f, 2000.0f, parsedMilliseconds(text, value));
            if (control == 3) return juce::jlimit(0.0f, 1.0f, value / 90.0f);
            if (control == 4 || control == 5 || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 6) return exponentialNormalized(
                20.0f, 2000.0f, parsedFrequency(text, value));
            if (control == 7) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 8) return juce::jlimit(0.0f, 1.0f, value / 150.0f);
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::pitchBloom:
            if (control == 1) return linearNormalized(-50.0f, 50.0f, value);
            if (control == 2) return exponentialNormalized(
                20.0f, 500.0f, parsedMilliseconds(text, value));
            if (control == 3) return juce::jlimit(0.0f, 1.0f, value / 85.0f);
            if (control == 4 || control == 5 || control == 8 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 6) return exponentialNormalized(
                20.0f, 2000.0f, parsedFrequency(text, value));
            if (control == 7) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::frequencyLab:
            if (control == 3) return exponentialNormalized(
                0.08f, 4.0f, parsedSeconds(text, value));
            if (control == 4 || control == 7 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5)
                return exponentialNormalized(0.02f, 4.0f, value);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 25.0f);
            if (control == 8) return exponentialNormalized(
                20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 9) return exponentialNormalized(
                2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::spatialOrbit:
            if (control == 1) return exponentialNormalized(0.02f, 5.0f, value);
            if (control == 4) return linearNormalized(0.0f, 360.0f, value);
            if (control == 5) return juce::jlimit(0.0f, 1.0f, value / 200.0f);
            if (control == 6) return exponentialNormalized(0.5f, 10.0f, value);
            if (control == 7 || control == 8 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 9) return exponentialNormalized(
                20.0f, 500.0f, parsedFrequency(text, value));
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::signalDecay:
            if (control == 0) return linearNormalized(4.0f, 24.0f, value);
            if (control == 1) return exponentialNormalized(1.0f, 48.0f, value);
            if (control == 2 || control == 3 || (control >= 6 && control <= 9))
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 4) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 5) return linearNormalized(-90.0f, -24.0f, value);
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::analogTape:
            if (control == 1) return linearNormalized(-18.0f, 18.0f, value);
            if (control == 2 || control == 5 || control == 6 || control == 7
                || control == 8 || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3)
            {
                const auto amount = juce::jlimit(0.0f, 100.0f, value);
                if (text.containsIgnoreCase("under"))
                    return linearNormalized(-100.0f, 100.0f, -amount);
                if (text.containsIgnoreCase("over"))
                    return linearNormalized(-100.0f, 100.0f, amount);
                return linearNormalized(-100.0f, 100.0f, value);
            }
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::resonanceTamer:
            if (control == 0) return juce::jlimit(0.0f, 1.0f, value / 18.0f);
            if (control == 3) return linearNormalized(-3.0f, 3.0f, value);
            if (control == 4) return exponentialNormalized(
                20.0f, 2000.0f, parsedFrequency(text, value));
            if (control == 5) return exponentialNormalized(
                1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 6 || control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::spectralBalance:
            if (control == 1 || control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control >= 2 && control <= 4)
                return linearNormalized(-6.0f, 6.0f, value);
            if (control == 5) return exponentialNormalized(
                0.5f, 30.0f, parsedSeconds(text, value));
            if (control == 8) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::phaseCoherence:
            if (control == 1) return exponentialNormalized(
                40.0f, 800.0f, parsedFrequency(text, value));
            if (control == 2 || control == 5)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3)
                return juce::jlimit(0.0f, 1.0f, parsedMilliseconds(text, value) / 2.0f);
            if (control == 4) return linearNormalized(0.0f, 180.0f, value);
            if (control == 6) return exponentialNormalized(
                20.0f, 500.0f, parsedFrequency(text, value));
            if (control == 7) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::loudnessRider:
            if (control == 0) return linearNormalized(-36.0f, -8.0f, value);
            if (control == 1) return juce::jlimit(0.0f, 1.0f, value / 18.0f);
            if (control == 3) return exponentialNormalized(
                0.25f, 10.0f, parsedSeconds(text, value));
            if (control == 4)
                return juce::jlimit(0.0f, 1.0f, parsedMilliseconds(text, value) / 250.0f);
            if (control == 5 || control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 7) return linearNormalized(-70.0f, -30.0f, value);
            if (control == 8) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::adaptiveClipper:
            if (control == 0) return juce::jlimit(0.0f, 1.0f, value / 24.0f);
            if (control == 1) return linearNormalized(-12.0f, 0.0f, value);
            if (control == 3 || control == 4 || control == 6 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5) return exponentialNormalized(
                20.0f, 1000.0f, parsedMilliseconds(text, value));
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::spectralDelayCanvas:
            if (control == 1) return exponentialNormalized(
                10.0f, 4000.0f, parsedMilliseconds(text, value));
            if (control >= 3 && control <= 5)
                return juce::jlimit(0.0f, 1.0f, value / 200.0f);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 90.0f);
            if (control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 70.0f);
            if (control == 8 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::formantForge:
            if (control == 1 || control == 2 || control == 4 || control == 5
                || control == 7 || control == 8 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3) return linearNormalized(-24.0f, 24.0f, value);
            if (control == 6) return exponentialNormalized(0.02f, 6.0f, value);
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::harmonicMirage:
            if (control == 2) return linearNormalized(2.0f, 24.0f, value);
            if (control == 3) return linearNormalized(-100.0f, 100.0f, value);
            if (control == 4 || control == 7 || control == 8 || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5)
                return juce::jlimit(0.0f, 1.0f, value / 30.0f);
            if (control == 6) return exponentialNormalized(
                0.02f, 2.0f, parsedSeconds(text, value));
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::chaosField:
            if (control == 1) return exponentialNormalized(0.015f, 7.0f, value);
            if (control == 4 || control == 8 || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5) return exponentialNormalized(
                80.0f, 10000.0f, parsedFrequency(text, value));
            if (control == 6) return exponentialNormalized(
                2.0f, 600.0f, parsedMilliseconds(text, value));
            if (control == 7) return linearNormalized(-88.0f, 88.0f, value);
            if (control == 11) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::timeMosaic:
            if (control == 0) return exponentialNormalized(
                0.25f, 8.0f, parsedSeconds(text, value));
            if (control == 1) return exponentialNormalized(
                1.0f / 24.0f, 3.0f, value);
            if (control == 2) return exponentialNormalized(
                0.01f, 0.5f, parsedMilliseconds(text, value) * 0.001f);
            if (control == 3 || control == 4 || control == 5 || control == 8
                || control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 50.0f);
            if (control == 10) return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::guitarCabinet:
            if (control == 1)
                return exponentialNormalized(
                    30.0f, 300.0f, parsedFrequency(text, value));
            if (control == 2)
                return exponentialNormalized(
                    2500.0f, 18000.0f, parsedFrequency(text, value));
            if (control == 3 || control == 4)
                return linearNormalized(-6.0f, 6.0f, value);
            if (control == 5)
                return linearNormalized(0.0f, 18.0f, value);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 7)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::chordLoom:
            if (control == 0)
            {
                if (text.containsChar('%'))
                    return juce::jlimit(0.0f, 1.0f, value / 100.0f);
                return juce::jlimit(0.0f, 1.0f, (-24.0f - value) / 36.0f);
            }
            if (control == 3 || control == 6 || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 1) return exponentialNormalized(
                0.25f, 2.0f, parsedSeconds(text, value));
            if (control == 2) return exponentialNormalized(
                0.10f, 2.0f, parsedSeconds(text, value));
            if (control == 4)
                return juce::jlimit(0.0f, 1.0f, value / 20.0f);
            if (control == 5)
                return juce::jlimit(0.0f, 1.0f, value / 50.0f);
            if (control == 7) return exponentialNormalized(
                20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 8) return exponentialNormalized(
                2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::volumeSwell:
            if (control == 0)
            {
                if (text.containsChar('%'))
                    return juce::jlimit(0.0f, 1.0f, value / 100.0f);
                return juce::jlimit(0.0f, 1.0f, (-24.0f - value) / 36.0f);
            }
            if (control == 1)
                return exponentialNormalized(
                    0.05f, 5.0f, parsedSeconds(text, value));
            if (control == 2)
            {
                if (text.containsIgnoreCase("linear"))
                    return 0.5f;
                const auto amount =
                    juce::jlimit(0.0f, 100.0f, value) / 200.0f;
                if (text.containsIgnoreCase("fast"))
                    return 0.5f - amount;
                if (text.containsIgnoreCase("slow"))
                    return 0.5f + amount;
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            }
            if (control == 3)
                return linearNormalized(-60.0f, -6.0f, value);
            if (control == 4)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 5)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::spectralNebula:
            if (control == 0 || control == 3 || control == 6
                || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 1)
                return exponentialNormalized(
                    0.2f, 30.0f, parsedSeconds(text, value));
            if (control == 2)
                return juce::jlimit(0.0f, 1.0f, value / 40.0f);
            if (control == 4)
            {
                if (text.containsIgnoreCase("full"))
                    return 0.5f;
                const auto amount =
                    juce::jlimit(0.0f, 100.0f, value) / 200.0f;
                if (text.containsIgnoreCase("noise"))
                    return 0.5f - amount;
                if (text.containsIgnoreCase("tonal"))
                    return 0.5f + amount;
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            }
            if (control == 5)
                return text.containsIgnoreCase("frozen")
                    || text.containsIgnoreCase("on") || value >= 0.5f
                    ? 1.0f : 0.0f;
            if (control == 7)
                return exponentialNormalized(
                    20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 8)
                return exponentialNormalized(
                    1000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::neuralAmpModeler:
            if (control == 0 || control == 4)
                return linearNormalized(-24.0f, 24.0f, value);
            if (control >= 1 && control <= 3)
                return linearNormalized(-12.0f, 12.0f, value);
            break;
        case ModuleType::stageVocal:
            if (control == 0)
                return exponentialNormalized(
                    40.0f, 220.0f, parsedFrequency(text, value));
            if (control == 1 || control == 4)
                return linearNormalized(0.0f, 30.0f, value);
            if (control == 2)
                return exponentialNormalized(
                    0.02f, 0.8f, parsedMilliseconds(text, value));
            if (control == 3)
                return exponentialNormalized(
                    40.0f, 800.0f, parsedMilliseconds(text, value));
            if (control == 5)
                return exponentialNormalized(
                    2.0f, 40.0f, parsedMilliseconds(text, value));
            if (control == 6)
                return exponentialNormalized(
                    200.0f, 3000.0f, parsedMilliseconds(text, value));
            if (control == 7)
                return exponentialNormalized(
                    3000.0f, 10000.0f, parsedFrequency(text, value));
            if (control == 8)
                return linearNormalized(0.0f, 12.0f, value);
            if (control == 9 || control == 10)
                return linearNormalized(-6.0f, 6.0f, value);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::vinylSimulator:
            if ((control >= 0 && control <= 3)
                || (control >= 8 && control <= 10))
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 4)
                return linearNormalized(-90.0f, -34.0f, value);
            if (control == 5)
                return linearNormalized(-90.0f, -36.0f, value);
            if (control == 6)
                return juce::jlimit(0.0f, 1.0f, value / 3.8f);
            if (control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 0.65f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::optoLeveler:
            if (control == 0 || control == 3 || control == 5
                || control == 6 || control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 1)
                return linearNormalized(-12.0f, 24.0f, value);
            if (control == 4)
                return exponentialNormalized(
                    20.0f, 300.0f, parsedFrequency(text, value));
            if (control == 8)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::fetLimiter:
            if (control == 0 || control == 1)
                return linearNormalized(-24.0f, 24.0f, value);
            if (control == 2)
                return exponentialNormalized(20.0f, 800.0f, value);
            if (control == 3)
                return exponentialNormalized(
                    50.0f, 1100.0f, parsedMilliseconds(text, value));
            if (control == 5)
                return exponentialNormalized(
                    20.0f, 300.0f, parsedFrequency(text, value));
            if (control == 6 || control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::programEqualizer:
            if (control == 1)
                return linearNormalized(0.0f, 14.0f, value);
            if (control == 2 || control == 4 || control == 7)
                return linearNormalized(0.0f, 18.0f, value);
            if (control == 5)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8)
                return linearNormalized(-12.0f, 18.0f, value);
            if (control == 9)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::studioPlate:
            if (control == 1)
                return exponentialNormalized(0.5f, 8.0f, value);
            if (control == 2 || control == 6 || control == 7
                || control == 9 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 3)
                return exponentialNormalized(
                    1.0f, 200.0f, parsedMilliseconds(text, value));
            if (control == 4)
                return exponentialNormalized(
                    20.0f, 800.0f, parsedFrequency(text, value));
            if (control == 5)
                return exponentialNormalized(
                    2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 8)
                return linearNormalized(0.0f, 18.0f, value);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::velvetDrive:
            if (control == 2 || control == 3
                || (control >= 5 && control <= 9))
                return juce::jlimit(0.0f, 1.0f, value / 10.0f);
            if (control == 4)
                return linearNormalized(-18.0f, 12.0f, value);
            if (control == 11)
                return linearNormalized(-24.0f, 12.0f, value);
            break;
        case ModuleType::wormholeConvolution:
            if (control == 0 || control == 1
                || control == 4 || control == 5
                || control == 3 || control == 7 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8)
                return exponentialNormalized(
                    20.0f, 1000.0f, parsedFrequency(text, value));
            if (control == 9)
                return exponentialNormalized(
                    2000.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::bitcrusher:
            if (control == 0)
                return linearNormalized(2.0f, 24.0f, value);
            if (control == 1)
                return exponentialNormalized(
                    250.0f, 768000.0f, parsedFrequency(text, value));
            if (control == 8)
                return linearNormalized(-1.0f, 1.0f, value);
            if (control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 24.0f);
            if ((control >= 3 && control <= 7) || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::midnightCurrent:
            if (control == 0)
                return exponentialNormalized(
                    120.0f, 3000.0f, parsedFrequency(text, value));
            if (control == 2)
                return exponentialNormalized(0.02f, 1.2f, value);
            if (control == 4)
                return linearNormalized(-80.0f, 80.0f, value);
            if (control == 1 || (control >= 3 && control <= 10))
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::autoWah:
            if (control == 0)
                return linearNormalized(-18.0f, -66.0f, value);
            if (control == 1)
                return exponentialNormalized(
                    120.0f, 1600.0f, parsedFrequency(text, value));
            if (control == 2)
                return linearNormalized(0.5f, 5.0f, value);
            if (control == 3)
                return exponentialNormalized(0.5f, 80.0f, value);
            if (control == 4)
                return exponentialNormalized(
                    30.0f, 1400.0f, parsedMilliseconds(text, value));
            if (control == 5 || (control >= 7 && control <= 10))
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::resonantSculpture:
            if (control == 2)
                return linearNormalized(-24.0f, 24.0f, value);
            if (control == 4)
                return exponentialNormalized(
                    0.15f, 12.0f, parsedSeconds(text, value));
            if (control >= 1 && control <= 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
            break;
        case ModuleType::guitarTuner:
            if (control == 0)
                return linearNormalized(430.0f, 450.0f, value);
            if (control == 3)
                return linearNormalized(-12.0f, 12.0f, value);
            if (control == 4)
                return linearNormalized(-24.0f, 24.0f, value);
            if (control == 5)
                return linearNormalized(-72.0f, -24.0f, value);
            if (control == 6)
                return exponentialNormalized(
                    0.04f, 0.60f,
                    parsedMilliseconds(text, value) * 0.001f);
            if (control == 8)
                return linearNormalized(0.5f, 5.0f, value);
            if (control == 10)
                return linearNormalized(-24.0f, 6.0f, value);
            if (control == 11)
                return linearNormalized(0.0f, 2.0f, value);
            break;
        case ModuleType::empty: break;
    }
    return juce::jlimit(0.0f, 1.0f, value / 100.0f);
}

bool isControlContextuallyVisible(
    ModuleType type, int control,
    const std::array<float, controlsPerSlot>& values)
{
    if (!juce::isPositiveAndBelow(control, controlsPerSlot)
        || !moduleDefinition(type)
                .controls[static_cast<size_t>(control)].isActive())
        return false;

    switch (type)
    {
        case ModuleType::envelopeModulator:
            if (control == EnvelopeModulatorModule::tremoloDepthControl)
                return discreteIndex(values[0], 3) != 2;
            if (control == EnvelopeModulatorModule::pitchDepthControl)
                return discreteIndex(values[0], 3) == 2;
            break;
        case ModuleType::compressor:
            if (control == 5)
                return values[8] < 0.5f || values[5] > 0.0f;
            break;
        case ModuleType::delay:
            if (control == 0)
                return values[2] < 0.5f;
            if (control == 1)
                return values[2] >= 0.5f;
            break;
        case ModuleType::tremolo:
        {
            const auto mode = discreteIndex(values[0], 3);
            if (control == 1)
                return values[2] < 0.5f;
            if (control == 3)
                return values[2] >= 0.5f;
            if (control == 4)
                return mode != 2;
            if (control == 5)
                return mode == 2;
            if (control == 8)
                return mode == 1;
            break;
        }
        case ModuleType::studioPhaser:
        case ModuleType::studioFlanger:
        case ModuleType::spatialOrbit:
            if (control == 1)
                return values[2] < 0.5f;
            if (control == 3)
                return values[2] >= 0.5f;
            break;
        case ModuleType::diffusionDelay:
            if (control == 0)
                return values[1] < 0.5f;
            if (control == 2)
                return values[1] >= 0.5f;
            break;
        case ModuleType::spectralDelayCanvas:
            if (control == 1)
                return values[0] < 0.5f;
            if (control == 2)
                return values[0] >= 0.5f;
            break;
        case ModuleType::chaosField:
            if (control == 1)
                return values[2] < 0.5f;
            if (control == 3)
                return values[2] >= 0.5f;
            break;
        case ModuleType::irMosaic:
            if (control == 1)
                return values[2] < 0.5f;
            if (control == 4)
                return values[2] >= 0.5f;
            break;
        case ModuleType::guitarTuner:
            return control == GuitarTunerModule::referenceControl
                || control == GuitarTunerModule::inputGainControl
                || control == GuitarTunerModule::gateControl
                || control == GuitarTunerModule::responseControl
                || control == GuitarTunerModule::rangeControl
                || control == GuitarTunerModule::holdControl;
        case ModuleType::neuralAmpModeler:
        case ModuleType::stereoWidth:
        case ModuleType::rotarySpeaker:
        case ModuleType::convolutionReverb:
        case ModuleType::algorithmicReverb:
        case ModuleType::stageVocal:
        case ModuleType::vinylSimulator:
        case ModuleType::optoLeveler:
        case ModuleType::fetLimiter:
        case ModuleType::programEqualizer:
        case ModuleType::studioPlate:
        case ModuleType::velvetDrive:
        case ModuleType::wormholeConvolution: break;
        case ModuleType::bitcrusher: break;
        case ModuleType::portalPermuter: break;
        case ModuleType::empty:
        case ModuleType::equalizer:
        case ModuleType::saturator:
        case ModuleType::limiter:
        case ModuleType::midSideDecoder:
        case ModuleType::dynamicEqualizer:
        case ModuleType::randomGranulizer:
        case ModuleType::vintageChorus:
        case ModuleType::beatPermuter:
        case ModuleType::spectralPrism:
        case ModuleType::resonantMatrix:
        case ModuleType::wavefoldGarden:
        case ModuleType::gateExpander:
        case ModuleType::transientDesigner:
        case ModuleType::multibandCompressor:
        case ModuleType::pitchBloom:
        case ModuleType::frequencyLab:
        case ModuleType::signalDecay:
        case ModuleType::analogTape:
        case ModuleType::resonanceTamer:
        case ModuleType::spectralBalance:
        case ModuleType::phaseCoherence:
        case ModuleType::loudnessRider:
        case ModuleType::adaptiveClipper:
        case ModuleType::formantForge:
        case ModuleType::harmonicMirage:
        case ModuleType::timeMosaic:
        case ModuleType::guitarCabinet:
        case ModuleType::chordLoom:
        case ModuleType::volumeSwell:
        case ModuleType::spectralNebula:
        case ModuleType::midnightCurrent:
        case ModuleType::autoWah:
        case ModuleType::resonantSculpture:
            break;
    }
    return true;
}

bool isControlContextuallyEnabled(
    ModuleType type, int control,
    const std::array<float, controlsPerSlot>& values,
    bool hasStereoOutput, bool hasExternalSidechain)
{
    if (!isControlContextuallyVisible(type, control, values))
        return false;
    if (type == ModuleType::compressor && control == 7)
        return hasExternalSidechain;
    if (type == ModuleType::delay && (control == 6 || control == 7))
        return hasStereoOutput;
    if (type == ModuleType::dynamicEqualizer && control == 9)
        return hasExternalSidechain;
    if (type == ModuleType::gateExpander && control == 8)
        return hasExternalSidechain;
    return true;
}

namespace
{
template <typename Module>
std::unique_ptr<DspModule> makeModule()
{
    return std::make_unique<Module>();
}

ModuleDefinition makeDefinition(
    ModuleType type, const char* displayName,
    ModuleCategory category, const char* description, const char* searchTags,
    const Names& names, const Metadata& controlMetadataValues,
    ModuleFactory factory,
    ModuleCapability capabilities = ModuleCapability::none)
{
    ModuleDefinition definition {
        type, displayName, category, description, searchTags, {},
        factory, capabilities
    };
    const auto defaults = defaultsFor(type);
    for (int control = 0; control < controlsPerSlot; ++control)
    {
        const auto index = static_cast<size_t>(control);
        definition.controls[index] = {
            names[index], controlMetadataValues[index], defaults[index],
            optionsFor(type, control)
        };
    }
    return definition;
}

const std::array<ModuleDefinition, moduleTypeCount>& registryStorage()
{
    static const std::array<ModuleDefinition, moduleTypeCount> definitions {{
        makeDefinition(
            ModuleType::empty, "Empty",
            ModuleCategory::none,
            "No processing in this slot.", "", emptyNames, emptyMetadata,
            nullptr),
        makeDefinition(
            ModuleType::equalizer, "Parametric EQ",
            ModuleCategory::eqAndFilters,
            "Shape lows, mids, and highs with a flexible three-band EQ.",
            "eq filter tone high pass low pass", eqNames, eqMetadata,
            &makeModule<EqualizerModule>),
        makeDefinition(
            ModuleType::compressor, "Compressor", ModuleCategory::dynamics,
            "Control dynamics with smooth compression and parallel blend.",
            "compression dynamics sidechain punch", compressorNames,
            compressorMetadata, &makeModule<CompressorModule>),
        makeDefinition(
            ModuleType::saturator, "Saturator",
            ModuleCategory::saturationAndColor,
            "Add harmonic warmth, edge, and character.",
            "saturation distortion drive warmth color", saturatorNames,
            saturatorMetadata, &makeModule<SaturatorModule>),
        makeDefinition(
            ModuleType::delay, "Delay",
            ModuleCategory::delayAndEcho,
            "Shape pristine or colored dual-line echoes with stereo movement.",
            "delay echo repeats tape bbd digital ping pong modulation ducking",
            delayNames, delayMetadata, &makeModule<DelayModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::limiter, "Limiter",
            ModuleCategory::dynamics,
            "Catch peaks and raise level with lookahead limiting.",
            "limiter peak loudness ceiling dynamics", limiterNames,
            limiterMetadata, &makeModule<LimiterModule>),
        makeDefinition(
            ModuleType::algorithmicReverb,
            "Algorithmic Reverb",
            ModuleCategory::reverbAndSpace,
            "Build animated halls, chambers, and plates.",
            "reverb hall chamber plate ambience space ducking high pass",
            reverbNames,
            reverbMetadata, &makeModule<AlgorithmicReverbModule>),
        makeDefinition(
            ModuleType::stereoWidth, "Stereo Width",
            ModuleCategory::stereoAndUtility,
            "Widen, focus, and balance the stereo image.",
            "stereo width imaging mono utility", stereoWidthNames,
            stereoWidthMetadata, &makeModule<StereoWidthModule>),
        makeDefinition(
            ModuleType::midSideDecoder, "M/S Decoder",
            ModuleCategory::stereoAndUtility,
            "Decode mid/side recordings and adjust their width.",
            "mid side ms decode stereo utility", midSideDecoderNames,
            midSideDecoderMetadata, &makeModule<MidSideDecoderModule>),
        makeDefinition(
            ModuleType::tremolo, "Tremolo",
            ModuleCategory::modulation,
            "Add rhythmic volume movement or true pitch vibrato.",
            "tremolo vibrato harmonic modulation pulse", tremoloNames,
            tremoloMetadata, &makeModule<TremoloModule>),
        makeDefinition(
            ModuleType::rotarySpeaker, "Rotary Speaker",
            ModuleCategory::modulation,
            "Recreate the moving horn, drum, and cabinet of a rotary speaker.",
            "rotary leslie speaker doppler organ modulation",
            rotarySpeakerNames, rotarySpeakerMetadata,
            &makeModule<RotarySpeakerModule>),
        makeDefinition(
            ModuleType::convolutionReverb,
            "Convolution Reverb",
            ModuleCategory::reverbAndSpace,
            "Place sounds in captured spaces with an impulse response.",
            "convolution reverb impulse response ir room space ducking high pass",
            convolutionReverbNames, convolutionReverbMetadata,
            &makeModule<ConvolutionReverbModule>,
            ModuleCapability::impulseResponse
                | ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::dynamicEqualizer,
            "Dynamic EQ / De-Esser",
            ModuleCategory::eqAndFilters,
            "Tame or lift a focused band only when needed.",
            "dynamic eq de esser resonance filter sidechain",
            dynamicEqualizerNames, dynamicEqualizerMetadata,
            &makeModule<DynamicEqualizerModule>),
        makeDefinition(
            ModuleType::randomGranulizer,
            "Random Granulizer",
            ModuleCategory::glitchAndCreative,
            "Scatter incoming sound into evolving randomized grains.",
            "granular glitch grains random texture creative",
            randomGranulizerNames, randomGranulizerMetadata,
            &makeModule<RandomGranulizerModule>,
            ModuleCapability::grainVisualization),
        makeDefinition(
            ModuleType::vintageChorus, "Vintage Chorus",
            ModuleCategory::modulation,
            "Blend classic chorus, dimension, and ensemble textures.",
            "chorus dimension ensemble bbd modulation vintage",
            vintageChorusNames, vintageChorusMetadata,
            &makeModule<VintageChorusModule>),
        makeDefinition(
            ModuleType::beatPermuter, "Beat Permuter", ModuleCategory::glitchAndCreative,
            "Rearrange recent tempo-locked slices into precise musical glitches.",
            "beat repeat stutter reverse scatter glitch buffer rhythm",
            beatPermuterNames, beatPermuterMetadata,
            &makeModule<BeatPermuterModule>,
            ModuleCapability::beatPermutationVisualization),
        makeDefinition(
            ModuleType::spectralPrism, "Spectral Prism", ModuleCategory::glitchAndCreative,
            "Bend, shift, smear, and freeze sound around a spectral pivot.",
            "spectral fft warp shift freeze smear phase creative",
            spectralPrismNames, spectralPrismMetadata,
            &makeModule<SpectralPrismModule>),
        makeDefinition(
            ModuleType::resonantMatrix, "Resonant Matrix", ModuleCategory::reverbAndSpace,
            "Route tuned resonators through evolving signed feedback patterns.",
            "resonator tuned comb matrix feedback pitched metallic space",
            resonantMatrixNames, resonantMatrixMetadata,
            &makeModule<ResonantMatrixModule>),
        makeDefinition(
            ModuleType::wavefoldGarden, "Wavefold Garden", ModuleCategory::glitchAndCreative,
            "Grow animated antialiased harmonics with dynamic wavefolding.",
            "wavefold distortion nonlinear harmonics envelope creative",
            wavefoldGardenNames, wavefoldGardenMetadata,
            &makeModule<WavefoldGardenModule>),
        makeDefinition(
            ModuleType::gateExpander, "Gate / Expander", ModuleCategory::dynamics,
            "Control noise and ambience with smooth downward expansion.",
            "gate expander noise dynamics sidechain envelope",
            gateExpanderNames, gateExpanderMetadata,
            &makeModule<GateExpanderModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::transientDesigner,
            "Transient Designer",
            ModuleCategory::dynamics,
            "Shape attack and sustain without depending on input level.",
            "transient shaper attack sustain punch dynamics",
            transientDesignerNames, transientDesignerMetadata,
            &makeModule<TransientDesignerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::multibandCompressor,
            "Multiband Compressor",
            ModuleCategory::dynamics,
            "Control low, mid, and high dynamics independently.",
            "multiband compressor crossover low mid high dynamics",
            multibandCompressorNames, multibandCompressorMetadata,
            &makeModule<MultibandCompressorModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::studioPhaser, "Studio Phaser", ModuleCategory::modulation,
            "Sweep classic all-pass notches from subtle motion to deep resonance.",
            "phaser allpass phase sweep stages modulation",
            studioPhaserNames, studioPhaserMetadata,
            &makeModule<StudioPhaserModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::studioFlanger, "Studio Flanger", ModuleCategory::modulation,
            "Create tape, through-zero, jet, and BBD comb movement.",
            "flanger tape through zero jet bbd comb modulation",
            studioFlangerNames, studioFlangerMetadata,
            &makeModule<StudioFlangerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::diffusionDelay, "Diffusion Delay", ModuleCategory::delayAndEcho,
            "Let distinct echoes bloom into a moving diffuse cloud.",
            "diffusion delay echo cloud smear ambient feedback",
            diffusionDelayNames, diffusionDelayMetadata,
            &makeModule<DiffusionDelayModule>,
            ModuleCapability::eventTelemetry),
        makeDefinition(
            ModuleType::pitchBloom, "Pitch Bloom", ModuleCategory::reverbAndSpace,
            "Grow pitch-shifted repeats into rising harmonic spaces.",
            "pitch bloom shimmer octave fifth feedback space",
            pitchBloomNames, pitchBloomMetadata,
            &makeModule<PitchBloomModule>,
            ModuleCapability::eventTelemetry),
        makeDefinition(
            ModuleType::frequencyLab, "Harmonic Resonator", ModuleCategory::reverbAndSpace,
            "Turn incoming sound into tuned, moving chord resonances.",
            "harmonic resonator chord tuned modal strings drone musical",
            frequencyLabNames, frequencyLabMetadata,
            &makeModule<FrequencyLabModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::spatialOrbit, "Spatial Orbit", ModuleCategory::stereoAndUtility,
            "Move sound through paths with distance, air, and Doppler cues.",
            "spatial orbit autopan doppler trajectory stereo motion",
            spatialOrbitNames, spatialOrbitMetadata,
            &makeModule<SpatialOrbitModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::signalDecay, "Signal Decay", ModuleCategory::saturationAndColor,
            "Add controlled digital, transmission, and mechanical wear.",
            "lofi bitcrush sample rate jitter dropout wow flutter noise",
            signalDecayNames, signalDecayMetadata,
            &makeModule<SignalDecayModule>,
            ModuleCapability::continuousTelemetry
                | ModuleCapability::eventTelemetry),
        makeDefinition(
            ModuleType::analogTape, "Analog Tape", ModuleCategory::saturationAndColor,
            "Print through a worn cassette or a finely tuned studio reel deck.",
            "tape saturation reel cassette wow flutter analog warmth",
            analogTapeNames, analogTapeMetadata,
            &makeModule<AnalogTapeModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::resonanceTamer, "Resonance Tamer", ModuleCategory::eqAndFilters,
            "Detect and suppress narrow resonances while preserving broad tone and transients.",
            "adaptive resonance suppression spectral dynamic eq harshness",
            resonanceTamerNames, resonanceTamerMetadata,
            &makeModule<ResonanceTamerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::spectralBalance, "Spectral Balance", ModuleCategory::eqAndFilters,
            "Guide long-term tonal balance toward a musical target contour.",
            "adaptive spectral tonal balance contour automatic eq",
            spectralBalanceNames, spectralBalanceMetadata,
            &makeModule<SpectralBalanceModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::phaseCoherence, "Phase Coherence", ModuleCategory::stereoAndUtility,
            "Repair confident stereo timing and low-frequency phase mismatches.",
            "phase coherence correlation alignment mono stereo utility",
            phaseCoherenceNames, phaseCoherenceMetadata,
            &makeModule<PhaseCoherenceModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::loudnessRider, "Loudness Rider", ModuleCategory::dynamics,
            "Ride long-term loudness with a bounded transparent fader trajectory.",
            "loudness rider lufs gain automation dynamics level",
            loudnessRiderNames, loudnessRiderMetadata,
            &makeModule<LoudnessRiderModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::adaptiveClipper, "Adaptive Clipper", ModuleCategory::dynamics,
            "Clip transient and body energy with adaptive shape and true-peak awareness.",
            "adaptive clipper clipping peak loudness transient oversampling",
            adaptiveClipperNames, adaptiveClipperMetadata,
            &makeModule<AdaptiveClipperModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::spectralDelayCanvas,
            "Spectral Delay Canvas", ModuleCategory::delayAndEcho,
            "Send low, mid, and high frequencies through independently timed spectral echoes.",
            "spectral delay echo frequency canvas freeze diffusion",
            spectralDelayCanvasNames, spectralDelayCanvasMetadata,
            &makeModule<SpectralDelayCanvasModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::formantForge, "Formant Forge", ModuleCategory::glitchAndCreative,
            "Sculpt animated vocal-tract resonances without shifting source pitch.",
            "formant vowel vocal tract filter creature metallic creative",
            formantForgeNames, formantForgeMetadata,
            &makeModule<FormantForgeModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::harmonicMirage, "Harmonic Mirage", ModuleCategory::saturationAndColor,
            "Track and transform confident partials into phase-continuous harmonic layers.",
            "harmonic partial resynthesis subharmonic inharmonic color",
            harmonicMirageNames, harmonicMirageMetadata,
            &makeModule<HarmonicMirageModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::chaosField, "Chaos Field", ModuleCategory::modulation,
            "Drive filter, delay, and stereo motion from deterministic chaotic attractors.",
            "chaos attractor lorenz rossler modulation orbit delay filter",
            chaosFieldNames, chaosFieldMetadata,
            &makeModule<ChaosFieldModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::timeMosaic, "Time Mosaic", ModuleCategory::glitchAndCreative,
            "Reassemble spectral tiles from recent history into evolving time-frequency textures.",
            "spectral history mosaic tile freeze glitch time creative",
            timeMosaicNames, timeMosaicMetadata,
            &makeModule<TimeMosaicModule>,
            ModuleCapability::continuousTelemetry
                | ModuleCapability::eventTelemetry),
        makeDefinition(
            ModuleType::guitarCabinet, "Guitar Cabinet", ModuleCategory::guitar,
            "Shape guitar through bundled or user-loaded cabinet impulse responses.",
            "guitar cabinet speaker cab impulse response ir microphone",
            guitarCabinetNames, guitarCabinetMetadata,
            &makeModule<GuitarCabinetModule>,
            ModuleCapability::impulseResponse
                | ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::chordLoom, "Chord Loom", ModuleCategory::reverbAndSpace,
            "Hold each guitar chord as a continuously woven pad until the next strum.",
            "guitar chord sustain freeze pad infinite granular hold strum",
            chordLoomNames, chordLoomMetadata,
            &makeModule<ChordLoomModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::volumeSwell, "Volume Swell", ModuleCategory::dynamics,
            "Automatically softens each chord attack and rises like a guitarist's volume pedal.",
            "guitar auto swell volume pedal knob attack fade chord texture",
            volumeSwellNames, volumeSwellMetadata,
            &makeModule<VolumeSwellModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::spectralNebula, "Spectral Nebula", ModuleCategory::reverbAndSpace,
            "Dissolve guitar or vocals into a drifting, shimmering spectral cloud.",
            "spectral cloud smear freeze shimmer ambient vocal guitar texture",
            spectralNebulaNames, spectralNebulaMetadata,
            &makeModule<SpectralNebulaModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::midnightCurrent, "Midnight Current", ModuleCategory::modulation,
            "Submerge input in a warm resonant analog filter and deep slow phase current.",
            "submerged underwater electric piano analog vcs3 phaser filter "
            "psychedelic vintage slow swirl",
            midnightCurrentNames, midnightCurrentMetadata,
            &makeModule<MidnightCurrentModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::autoWah, "Auto Wah", ModuleCategory::eqAndFilters,
            "Drive a vocal resonant filter directly from pick attack and playing dynamics.",
            "auto wah envelope filter pick attack touch sensitive guitar funk "
            "vocal quack",
            autoWahNames, autoWahMetadata,
            &makeModule<AutoWahModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::resonantSculpture,
            "Resonant Sculpture", ModuleCategory::glitchAndCreative,
            "Excite a morphing virtual object of strings, glass, metal, or membranes.",
            "physical modeling modal resonator sculpture bowed struck material "
            "glass metal membrane impossible texture",
            resonantSculptureNames, resonantSculptureMetadata,
            &makeModule<ResonantSculptureModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::neuralAmpModeler,
            "Neural Amp Modeler",
            ModuleCategory::guitar,
            "Load .nam neural amp models including A2 Standard and A2 Nano for authentic amp tones.",
            "nam neural amp modeler guitar tone cabinet a2 machine learning",
            neuralAmpModelerNames, neuralAmpModelerMetadata,
            &makeModule<NeuralAmpModelerModule>,
            ModuleCapability::neuralAmpModel),
        makeDefinition(
            ModuleType::stageVocal,
            "Stage Vocal",
            ModuleCategory::dynamics,
            "Catch peaks with fast feedback FET compression, then level phrases through a slow optical cell.",
            "stage live vocal microphone dual serial fet 1176 optical la2a deesser compressor presence air strip",
            stageVocalNames, stageVocalMetadata,
            &makeModule<StageVocalModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::vinylSimulator,
            "Vinyl Simulator",
            ModuleCategory::saturationAndColor,
            "Recreate worn record playback with deterministic groove noise, crackle, dust, scratches, rumble, and pitch drift.",
            "vinyl record turntable phonograph crackle dust scratch surface noise "
            "rumble wow flutter lofi",
            vinylSimulatorNames, vinylSimulatorMetadata,
            &makeModule<VinylSimulatorModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::optoLeveler,
            "Opto Leveler",
            ModuleCategory::dynamics,
            "Program-dependent optical leveling with a dual-memory cell and analog makeup stage.",
            "classic studio optical leveler tube feedback compressor smooth vocal bass",
            optoLevelerNames, optoLevelerMetadata,
            &makeModule<OptoLevelerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::fetLimiter,
            "FET Limiter",
            ModuleCategory::dynamics,
            "Fast feedback FET limiting with stepped ratios and an aggressive ALL mode.",
            "classic studio fet limiter feedback compressor all ratio fast drums vocal",
            fetLimiterNames, fetLimiterMetadata,
            &makeModule<FetLimiterModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::programEqualizer,
            "Program Equalizer",
            ModuleCategory::eqAndFilters,
            "Interacting passive low boost and attenuation with broad high-frequency shaping.",
            "classic studio passive program equalizer tube broad boost attenuation",
            programEqualizerNames, programEqualizerMetadata,
            &makeModule<ProgramEqualizerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::studioPlate,
            "Studio Plate",
            ModuleCategory::reverbAndSpace,
            "Physically informed electromechanical plate reverb with stereo pickup geometry.",
            "classic studio plate reverb electromechanical modal dense metal pickup",
            studioPlateNames, studioPlateMetadata,
            &makeModule<StudioPlateModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::velvetDrive,
            "Velvet Drive",
            ModuleCategory::guitar,
            "Purely modeled touch-sensitive boutique guitar amplifier with cascading overdrive and dynamic power-stage bloom.",
            "guitar amp boutique touch sensitive cascading overdrive tube sag speaker clean sustain",
            velvetDriveNames, velvetDriveMetadata,
            &makeModule<VelvetDriveModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::wormholeConvolution,
            "Wormhole Convolution", ModuleCategory::reverbAndSpace,
            "Crop, reverse, and combine two reverb impulse responses in parallel.",
            "dual parallel convolution reverb impulse reverse crop trim wormhole",
            wormholeConvolutionNames, wormholeConvolutionMetadata,
            &makeModule<WormholeConvolutionModule>,
            ModuleCapability::impulseResponse),
        makeDefinition(
            ModuleType::bitcrusher, "Bitcrusher", ModuleCategory::saturationAndColor,
            "Shape digital resolution and sample-clock reduction from polished texture to broken-machine grit.",
            "bit crusher bitcrush lo-fi sample rate reduction quantizer dither alias jitter digital",
            bitcrusherNames, bitcrusherMetadata,
            &makeModule<BitcrusherModule>),
        makeDefinition(
            ModuleType::portalPermuter,
            "Portal Permuter", ModuleCategory::glitchAndCreative,
            "Rearrange recent tempo-locked slices before sending them through a focused impulse-response portal.",
            "portal permutation beat slice convolution impulse response ir reverse shuffle texture guitar",
            portalPermuterNames, portalPermuterMetadata,
            &makeModule<PortalPermuterModule>,
            ModuleCapability::impulseResponse
                | ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::irMosaic, "IR Mosaic", ModuleCategory::reverbAndSpace,
            "Reassemble an impulse response into moving tile orders while preserving its continuous tail.",
            "impulse response ir mosaic tiles convolution moving reverb texture morph",
            irMosaicNames, irMosaicMetadata,
            &makeModule<IRMosaicModule>,
            ModuleCapability::impulseResponse
                | ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::guitarTuner, "Guitar Tuner", ModuleCategory::guitar,
            "Tune chromatic sources with off-thread sub-cent precision analysis and transparent passthrough.",
            "guitar bass chromatic tuner precision cents pitch calibration",
            guitarTunerNames, guitarTunerMetadata,
            &makeModule<GuitarTunerModule>,
            ModuleCapability::continuousTelemetry),
        makeDefinition(
            ModuleType::envelopeModulator, "Envelope Modulator",
            ModuleCategory::modulation,
            "Let playing dynamics steer tremolo, harmonic tremolo, or vibrato between two reversible speeds.",
            "envelope dynamics guitar touch responsive modulation tremolo harmonic vibrato amplitude speed threshold reverse",
            envelopeModulatorNames, envelopeModulatorMetadata,
            &makeModule<EnvelopeModulatorModule>,
            ModuleCapability::continuousTelemetry)
    }};
    return definitions;
}
} // namespace

juce::StringArray ControlOptions::strings() const
{
    juce::StringArray result;
    for (int index = 0; index < count; ++index)
        result.add(values[static_cast<size_t>(index)]);
    return result;
}

bool ControlDefinition::isActive() const
{
    return juce::String(name) != "-";
}

const std::array<ModuleDefinition, moduleTypeCount>& moduleRegistry()
{
    return registryStorage();
}

const ModuleDefinition* findModuleDefinition(ModuleType type)
{
    for (const auto& definition : registryStorage())
        if (definition.type == type)
            return &definition;
    return nullptr;
}

const ModuleDefinition& moduleDefinition(ModuleType type)
{
    if (const auto* definition = findModuleDefinition(type))
        return *definition;
    return *findModuleDefinition(ModuleType::empty);
}

CompactVisualization compactVisualization(ModuleType type)
{
    switch (type)
    {
        case ModuleType::equalizer:
        case ModuleType::dynamicEqualizer:
        case ModuleType::autoWah:
        case ModuleType::programEqualizer:
            return CompactVisualization::response;

        case ModuleType::compressor:
        case ModuleType::limiter:
        case ModuleType::gateExpander:
        case ModuleType::multibandCompressor:
        case ModuleType::loudnessRider:
        case ModuleType::stageVocal:
        case ModuleType::optoLeveler:
        case ModuleType::fetLimiter:
            return CompactVisualization::dynamics;

        case ModuleType::saturator:
        case ModuleType::wavefoldGarden:
        case ModuleType::adaptiveClipper:
        case ModuleType::midnightCurrent:
            return CompactVisualization::transfer;

        case ModuleType::delay:
        case ModuleType::diffusionDelay:
            return CompactVisualization::delay;

        case ModuleType::algorithmicReverb:
        case ModuleType::studioPlate:
        case ModuleType::wormholeConvolution:
            return CompactVisualization::space;

        case ModuleType::irMosaic:
            return CompactVisualization::impulse;

        case ModuleType::convolutionReverb:
            return CompactVisualization::wetSpectrum;

        case ModuleType::stereoWidth:
        case ModuleType::midSideDecoder:
        case ModuleType::spatialOrbit:
        case ModuleType::phaseCoherence:
            return CompactVisualization::stereo;

        case ModuleType::tremolo:
        case ModuleType::envelopeModulator:
        case ModuleType::rotarySpeaker:
        case ModuleType::vintageChorus:
        case ModuleType::studioPhaser:
        case ModuleType::studioFlanger:
            return CompactVisualization::modulation;

        case ModuleType::randomGranulizer:
        case ModuleType::chordLoom:
            return CompactVisualization::grains;

        case ModuleType::beatPermuter:
        case ModuleType::timeMosaic:
        case ModuleType::portalPermuter:
            return CompactVisualization::slices;

        case ModuleType::spectralPrism:
        case ModuleType::frequencyLab:
        case ModuleType::resonanceTamer:
        case ModuleType::spectralBalance:
        case ModuleType::formantForge:
        case ModuleType::spectralNebula:
        case ModuleType::spectralDelayCanvas:
        case ModuleType::neuralAmpModeler:
            return CompactVisualization::spectral;

        case ModuleType::resonantMatrix:
        case ModuleType::harmonicMirage:
        case ModuleType::resonantSculpture:
            return CompactVisualization::resonator;

        case ModuleType::guitarTuner:
            return CompactVisualization::tuner;

        case ModuleType::analogTape:
        case ModuleType::vinylSimulator:
            return CompactVisualization::transport;

        case ModuleType::pitchBloom:
            return CompactVisualization::pitch;

        case ModuleType::transientDesigner:
        case ModuleType::signalDecay:
        case ModuleType::volumeSwell:
            return CompactVisualization::envelope;

        case ModuleType::guitarCabinet:
        case ModuleType::velvetDrive:
            return CompactVisualization::cabinet;

        case ModuleType::bitcrusher:
            return CompactVisualization::bitcrusher;

        case ModuleType::chaosField:
            return CompactVisualization::chaos;

        case ModuleType::empty:
            return CompactVisualization::response;
    }
    return CompactVisualization::response;
}

bool compactVisualizationShowsGainReduction(ModuleType type)
{
    return compactVisualization(type)
        == CompactVisualization::dynamics;
}

const char* moduleCategoryName(ModuleCategory category)
{
    switch (category)
    {
        case ModuleCategory::eqAndFilters: return "EQ & Filters";
        case ModuleCategory::dynamics: return "Dynamics";
        case ModuleCategory::saturationAndColor: return "Saturation & Color";
        case ModuleCategory::delayAndEcho: return "Delay & Echo";
        case ModuleCategory::reverbAndSpace: return "Reverb & Space";
        case ModuleCategory::modulation: return "Modulation";
        case ModuleCategory::stereoAndUtility: return "Stereo & Utility";
        case ModuleCategory::glitchAndCreative: return "Glitch & Creative";
        case ModuleCategory::guitar: return "Guitar";
        case ModuleCategory::none: return "";
    }
    return "";
}

std::unique_ptr<DspModule> createDspModule(ModuleType type)
{
    const auto* definition = findModuleDefinition(type);
    return definition != nullptr && definition->factory != nullptr
               ? definition->factory() : nullptr;
}

const ControlMetadata& controlMetadata(ModuleType type, int control)
{
    const auto stableType = juce::jlimit(
        0, moduleTypeCount - 1, static_cast<int>(type));
    const auto safeControl = juce::jlimit(0, controlsPerSlot - 1, control);
    return moduleDefinition(static_cast<ModuleType>(stableType))
        .controls[static_cast<size_t>(safeControl)].metadata;
}

std::array<float, controlsPerSlot> moduleDefaults(ModuleType type)
{
    std::array<float, controlsPerSlot> result {};
    const auto& definition = moduleDefinition(type);
    for (int control = 0; control < controlsPerSlot; ++control)
        result[static_cast<size_t>(control)] =
            definition.controls[static_cast<size_t>(control)].defaultValue;
    return result;
}

juce::StringArray controlOptions(ModuleType type, int control)
{
    const auto safeControl = juce::jlimit(0, controlsPerSlot - 1, control);
    return moduleDefinition(type).controls[static_cast<size_t>(safeControl)]
        .options.strings();
}

juce::StringArray validateModuleRegistry()
{
    juce::StringArray errors;
    std::array<int, moduleTypeCount> typeCounts {};
    for (const auto& definition : registryStorage())
    {
        const auto stableType = static_cast<int>(definition.type);
        if (!juce::isPositiveAndBelow(stableType, moduleTypeCount)
            && stableType != 0)
        {
            errors.add("Module type is outside the stable 0..29 range.");
            continue;
        }
        ++typeCounts[static_cast<size_t>(stableType)];

        const auto active = definition.type != ModuleType::empty;
        const auto unknownCapabilities =
            static_cast<std::uint32_t>(definition.capabilities)
            & ~static_cast<std::uint32_t>(allModuleCapabilities);
        if (unknownCapabilities != 0)
            errors.add(juce::String(definition.displayName)
                       + " advertises an unknown capability flag.");
        if (active)
        {
            if (juce::String(moduleCategoryName(definition.category)).isEmpty())
                errors.add(juce::String(definition.displayName)
                           + " has no browsable category.");
            if (juce::String(definition.description).isEmpty()
                || juce::String(definition.searchTags).isEmpty())
                errors.add(juce::String(definition.displayName)
                           + " has incomplete browser text.");
            if (definition.factory == nullptr)
                errors.add(juce::String(definition.displayName)
                           + " has no DSP factory.");
            else
            {
                const auto module = definition.factory();
                if (module == nullptr)
                    errors.add(juce::String(definition.displayName)
                               + " factory returned null.");
                else
                {
                    if (definition.capabilities
                        != module->capabilities())
                        errors.add(juce::String(definition.displayName)
                                   + " capability flags do not match its DSP.");
                }
            }
        }
        else if (definition.category != ModuleCategory::none
                 || definition.factory != nullptr)
        {
            errors.add("Empty must not have a category or DSP factory.");
        }

        for (int control = 0; control < controlsPerSlot; ++control)
        {
            const auto& value =
                definition.controls[static_cast<size_t>(control)];
            if (!value.isActive())
                continue;
            const auto prefix = juce::String(definition.displayName) + " "
                                + juce::String(control + 1);
            if (juce::String(value.metadata.label).isEmpty()
                || juce::String(value.metadata.label) == "-"
                || juce::String(value.metadata.group).isEmpty()
                || juce::String(value.metadata.tooltip).isEmpty())
                errors.add(prefix + " has incomplete metadata.");
            if (!std::isfinite(value.defaultValue)
                || value.defaultValue < 0.0f || value.defaultValue > 1.0f)
                errors.add(prefix + " has an invalid default.");
            if (value.metadata.kind == ControlKind::choice
                && value.options.count == 0)
                errors.add(prefix + " has no choice options.");
        }
    }
    for (int type = 0; type < moduleTypeCount; ++type)
    {
        if (typeCounts[static_cast<size_t>(type)] != 1)
            errors.add("Stable module type " + juce::String(type)
                       + " does not have exactly one definition.");
    }
    return errors;
}

} // namespace megadsp
