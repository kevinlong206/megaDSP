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
    unused, unused
}};
constexpr Metadata compressorMetadata {{
    { "Threshold", ControlKind::level, true, "Compression",
      "Sets where compression begins; the control track also shows live input level." },
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
      "Slowly restores average level lost to compression; Manual Trim remains an additional trim." },
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
    { "Time", ControlKind::rotary, true, "Timing",
      "Sets free-running delay time." },
    { "Feedback", ControlKind::rotary, true, "Repeats",
      "Sets how long the repeats continue." },
    { "Mix", ControlKind::rotary, true, "Output",
      "Blends the delay and dry signals." },
    { "Tone", ControlKind::horizontal, true, "Repeats",
      "Darkens or brightens each repeat." },
    { "Ping Pong", ControlKind::toggle, false, "Stereo",
      "Alternates repeats between left and right." },
    { "Sync", ControlKind::toggle, true, "Timing",
      "Locks delay time to the host tempo." },
    { "Division", ControlKind::choice, true, "Timing",
      "Selects the tempo-synced note value." },
    { "Movement Rate", ControlKind::rotary, false, "Movement",
      "Sets delay-line modulation speed from 0.05 to 8 Hz." },
    { "Movement Depth", ControlKind::rotary, false, "Movement",
      "Sets peak delay-time movement from 0 to 10 ms." },
    unused, unused, unused
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
      "Scales the independent horn and drum acceleration times from 0.40× to 2.50×." },
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
      "Changes the apparent room scale from Compact to Vast; the readout also shows the exact 25–200% scale." },
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
    { "Modulation", ControlKind::rotary, false, "Space",
      "Adds subtle modulation to the tail." },
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
    unused, unused, unused, unused, unused, unused, unused
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
    { "Mix", ControlKind::horizontal, true, "Output",
      "Blends dry and granular signals with constant power." },
    { "Output", ControlKind::level, true, "Output",
      "Adjusts level after the dry/wet blend." }
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
constexpr Names emptyNames { "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-" };
constexpr Names eqNames {
    "Low Frequency", "Low Gain", "Low Q",
    "Mid Frequency", "Mid Gain", "Mid Q",
    "High Frequency", "High Gain", "High Q",
    "Output", "-", "-"
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
    "Time", "Feedback", "Mix", "Tone", "Ping Pong", "Sync",
    "Division", "Movement Rate", "Movement Depth", "-", "-", "-"
};
constexpr Names limiterNames {
    "Threshold", "Ceiling", "Release", "Lookahead", "Auto Gain", "-", "-",
    "-", "-", "-", "-", "-"
};
constexpr Names reverbNames {
    "Decay", "Room Scale", "Dry", "Wet", "Mode", "Pre-delay",
    "Diffusion", "Modulation", "Width", "Damping", "Low Cut", "High Cut"
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
constexpr Names rotarySpeakerNames {
    "Speed", "Drive", "Rotor Balance", "Crossover", "Motion",
    "Mic Distance", "Mic Spread", "Spin-up", "Cabinet Color", "Ambience", "Mix",
    "Output"
};
constexpr Names convolutionReverbNames {
    "Low Cut", "High Cut", "Wet", "Output Trim", "Dry", "-", "-", "-", "-", "-",
    "-", "-"
};
constexpr Names dynamicEqualizerNames {
    "Frequency", "Q", "Range", "Threshold", "Ratio", "Attack", "Release",
    "Shape", "Detector", "External Sidechain", "Listen", "Stereo Link"
};
constexpr Names randomGranulizerNames {
    "Voices", "Size Minimum", "Grain Rate", "Capture Range", "Size Maximum",
    "Reverse Chance", "Stereo Spread", "Rhythmic Delay Chance", "Brightness",
    "Regeneration", "Mix", "Output"
};
constexpr Names vintageChorusNames {
    "Model", "Rate", "Depth", "Delay", "Density", "Width", "Regeneration",
    "Tone", "Age", "Stereo Phase", "Mix", "Output"
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

bool equalizerLowIsHighPass(float normalizedMode)
{
    return normalizedMode > 0.5f;
}

bool equalizerHighIsLowPass(float normalizedMode)
{
    return normalizedMode > 0.5f;
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
    if (type == ModuleType::saturator && control == 5)
        return { { "Soft", "Smooth", "Hard" }, 3 };
    if (type == ModuleType::delay && control == 6)
        return { { "1/32", "1/16", "1/16.", "1/8",
                   "1/8.", "1/4", "1/4.", "1/2" }, 8 };
    if (type == ModuleType::algorithmicReverb && control == 4)
        return { { "Hall", "Chamber", "Plate" }, 3 };
    if (type == ModuleType::tremolo && control == 0)
        return { { "Amplitude", "Harmonic", "Vibrato" }, 3 };
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
                       0.30f / 0.95f, 0.20f,
                       exponentialNormalized(800.0f, 20000.0f, 8000.0f),
                       0.0f, 1.0f, discreteValue(5, 8),
                       exponentialNormalized(0.05f, 8.0f, 0.5f), 0.0f,
                       0.5f, 0.5f, 0.5f };
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
                       0.55f, 0.20f,
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
                1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f
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
                0.10f / 0.80f, 0.35f,
                linearNormalized(-18.0f, 12.0f, 0.0f)
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
            if (control == 3) return juce::String(exponential(10.0f, 1000.0f, value), 0) + " ms";
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
            if (control == 1) return juce::String(value * 95.0f, 0) + "%";
            if (control == 3) return formatFrequency(exponential(800.0f, 20000.0f, value));
            if (control == 7) return juce::String(exponential(0.05f, 8.0f, value), 2) + " Hz";
            if (control == 8) return juce::String(value * 10.0f, 1) + " ms";
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
                return juce::String(roomScaleName(percent)) + "  ·  "
                       + juce::String(percent, 0) + "%";
            }
            if (control == 2 || control == 3)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 5) return juce::String(value * 250.0f, 0) + " ms";
            if (control == 6) return juce::String(value * 100.0f, 0) + "%";
            if (control == 7) return juce::String(value * 100.0f, 0) + "%";
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
            if (control == 5 || control == 6 || control == 7 || control == 10)
                return juce::String(value * 100.0f, 0) + "%";
            if (control == 8)
                return formatFrequency(exponential(500.0f, 20000.0f, value));
            if (control == 9)
                return juce::String(value * 80.0f, 0) + "%";
            if (control == 11)
                return juce::String(linear(-18.0f, 12.0f, value), 1) + " dB";
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
    if (type == ModuleType::algorithmicReverb && control == 1)
        if (const auto named = namedRoomScale(text))
            return named;

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
            if (control == 1) return juce::jlimit(0.0f, 1.0f, value / 95.0f);
            if (control == 3) return exponentialNormalized(
                800.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 7) return exponentialNormalized(0.05f, 8.0f, value);
            if (control == 8) return juce::jlimit(0.0f, 1.0f, value / 10.0f);
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
            if (control == 6 || control == 7)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
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
            if (control == 5 || control == 6 || control == 7 || control == 10)
                return juce::jlimit(0.0f, 1.0f, value / 100.0f);
            if (control == 8) return exponentialNormalized(
                500.0f, 20000.0f, parsedFrequency(text, value));
            if (control == 9)
                return juce::jlimit(0.0f, 1.0f, value / 80.0f);
            if (control == 11)
                return linearNormalized(-18.0f, 12.0f, value);
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
        case ModuleType::compressor:
            if (control == 5)
                return values[8] < 0.5f || values[5] > 0.0f;
            break;
        case ModuleType::delay:
            if (control == 0)
                return values[5] < 0.5f;
            if (control == 6)
                return values[5] >= 0.5f;
            if (control == 7 || control == 8)
                return false;
            break;
        case ModuleType::stereoWidth:
            if (control == 0 || control == 2)
                return false;
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
        case ModuleType::rotarySpeaker:
            if (control == 2 || control == 4 || control == 5
                || control == 6 || control == 7)
                return false;
            break;
        case ModuleType::convolutionReverb:
            if (control == 0 || control == 1)
                return false;
            break;
        case ModuleType::algorithmicReverb:
            if (control == 0 || control == 1
                || control == 10 || control == 11)
                return false;
            break;
        case ModuleType::empty:
        case ModuleType::equalizer:
        case ModuleType::saturator:
        case ModuleType::limiter:
        case ModuleType::midSideDecoder:
        case ModuleType::dynamicEqualizer:
        case ModuleType::randomGranulizer:
        case ModuleType::vintageChorus:
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
    if (type == ModuleType::delay && control == 4)
        return hasStereoOutput;
    if (type == ModuleType::dynamicEqualizer && control == 9)
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
    ModuleType type, ModulePresentation presentation, const char* displayName,
    ModuleCategory category, const char* description, const char* searchTags,
    const Names& names, const Metadata& controlMetadataValues,
    ModuleFactory factory,
    ModuleCapability capabilities = ModuleCapability::none)
{
    ModuleDefinition definition {
        type, displayName, category, description, searchTags, presentation, {},
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
            ModuleType::empty, ModulePresentation::none, "Empty",
            ModuleCategory::none,
            "No processing in this slot.", "", emptyNames, emptyMetadata,
            nullptr),
        makeDefinition(
            ModuleType::equalizer, ModulePresentation::equalizer,
            "Parametric EQ",
            ModuleCategory::eqAndFilters,
            "Shape lows, mids, and highs with a flexible three-band EQ.",
            "eq filter tone high pass low pass", eqNames, eqMetadata,
            &makeModule<EqualizerModule>),
        makeDefinition(
            ModuleType::compressor, ModulePresentation::compressor,
            "Compressor", ModuleCategory::dynamics,
            "Control dynamics with smooth compression and parallel blend.",
            "compression dynamics sidechain punch", compressorNames,
            compressorMetadata, &makeModule<CompressorModule>),
        makeDefinition(
            ModuleType::saturator, ModulePresentation::saturator, "Saturator",
            ModuleCategory::saturationAndColor,
            "Add harmonic warmth, edge, and character.",
            "saturation distortion drive warmth color", saturatorNames,
            saturatorMetadata, &makeModule<SaturatorModule>),
        makeDefinition(
            ModuleType::delay, ModulePresentation::delay, "Delay",
            ModuleCategory::delayAndEcho,
            "Create tempo-synced or free echoes with stereo movement.",
            "delay echo repeats ping pong modulation", delayNames,
            delayMetadata, &makeModule<DelayModule>),
        makeDefinition(
            ModuleType::limiter, ModulePresentation::limiter, "Limiter",
            ModuleCategory::dynamics,
            "Catch peaks and raise level with lookahead limiting.",
            "limiter peak loudness ceiling dynamics", limiterNames,
            limiterMetadata, &makeModule<LimiterModule>),
        makeDefinition(
            ModuleType::algorithmicReverb,
            ModulePresentation::algorithmicReverb, "Algorithmic Reverb",
            ModuleCategory::reverbAndSpace,
            "Build animated halls, chambers, and plates.",
            "reverb hall chamber plate ambience space", reverbNames,
            reverbMetadata, &makeModule<AlgorithmicReverbModule>),
        makeDefinition(
            ModuleType::stereoWidth, ModulePresentation::stereoWidth,
            "Stereo Width",
            ModuleCategory::stereoAndUtility,
            "Widen, focus, and balance the stereo image.",
            "stereo width imaging mono utility", stereoWidthNames,
            stereoWidthMetadata, &makeModule<StereoWidthModule>),
        makeDefinition(
            ModuleType::midSideDecoder, ModulePresentation::midSideDecoder,
            "M/S Decoder",
            ModuleCategory::stereoAndUtility,
            "Decode mid/side recordings and adjust their width.",
            "mid side ms decode stereo utility", midSideDecoderNames,
            midSideDecoderMetadata, &makeModule<MidSideDecoderModule>),
        makeDefinition(
            ModuleType::tremolo, ModulePresentation::tremolo, "Tremolo",
            ModuleCategory::modulation,
            "Add rhythmic volume movement or true pitch vibrato.",
            "tremolo vibrato harmonic modulation pulse", tremoloNames,
            tremoloMetadata, &makeModule<TremoloModule>),
        makeDefinition(
            ModuleType::rotarySpeaker, ModulePresentation::rotarySpeaker,
            "Rotary Speaker",
            ModuleCategory::modulation,
            "Recreate the moving horn, drum, and cabinet of a rotary speaker.",
            "rotary leslie speaker doppler organ modulation",
            rotarySpeakerNames, rotarySpeakerMetadata,
            &makeModule<RotarySpeakerModule>),
        makeDefinition(
            ModuleType::convolutionReverb,
            ModulePresentation::convolutionReverb, "Convolution Reverb",
            ModuleCategory::reverbAndSpace,
            "Place sounds in captured spaces with an impulse response.",
            "convolution reverb impulse response ir room space",
            convolutionReverbNames, convolutionReverbMetadata,
            &makeModule<ConvolutionReverbModule>,
            ModuleCapability::impulseResponse),
        makeDefinition(
            ModuleType::dynamicEqualizer,
            ModulePresentation::dynamicEqualizer, "Dynamic EQ / De-Esser",
            ModuleCategory::eqAndFilters,
            "Tame or lift a focused band only when needed.",
            "dynamic eq de esser resonance filter sidechain",
            dynamicEqualizerNames, dynamicEqualizerMetadata,
            &makeModule<DynamicEqualizerModule>),
        makeDefinition(
            ModuleType::randomGranulizer,
            ModulePresentation::randomGranulizer, "Random Granulizer",
            ModuleCategory::glitchAndCreative,
            "Scatter incoming sound into evolving randomized grains.",
            "granular glitch grains random texture creative",
            randomGranulizerNames, randomGranulizerMetadata,
            &makeModule<RandomGranulizerModule>,
            ModuleCapability::grainVisualization),
        makeDefinition(
            ModuleType::vintageChorus, ModulePresentation::vintageChorus,
            "Vintage Chorus",
            ModuleCategory::modulation,
            "Blend classic chorus, dimension, and ensemble textures.",
            "chorus dimension ensemble bbd modulation vintage",
            vintageChorusNames, vintageChorusMetadata,
            &makeModule<VintageChorusModule>)
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
            errors.add("Module type is outside the stable 0..14 range.");
            continue;
        }
        ++typeCounts[static_cast<size_t>(stableType)];

        const auto active = definition.type != ModuleType::empty;
        const auto presentation =
            static_cast<int>(definition.presentation);
        const auto presentationIsValid =
            presentation >= 0
            && presentation < static_cast<int>(ModulePresentation::count);
        if (!presentationIsValid
            || (active
                    ? definition.presentation == ModulePresentation::none
                    : definition.presentation != ModulePresentation::none))
            errors.add(juce::String(definition.displayName)
                       + " has no valid UI presentation.");
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
        if (typeCounts[static_cast<size_t>(type)] != 1)
            errors.add("Stable module type " + juce::String(type)
                       + " does not have exactly one definition.");
    return errors;
}

} // namespace megadsp
