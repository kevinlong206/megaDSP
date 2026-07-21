#include "AlgorithmicReverb.h"
#include "DspHelpers.h"

#include <cmath>

namespace megadsp
{
using detail::exponential;
using detail::lerp;

void AlgorithmicReverbModule::FractionalDelay::prepare(int capacity)
{
    buffer.assign(static_cast<size_t>(juce::jmax(2, capacity)), 0.0f);
    writePosition = 0;
}

void AlgorithmicReverbModule::FractionalDelay::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePosition = 0;
}

float AlgorithmicReverbModule::FractionalDelay::read(float delaySamples) const
{
    if (buffer.empty())
        return 0.0f;
    const auto size = static_cast<int>(buffer.size());
    auto position = static_cast<float>(writePosition)
                    - juce::jlimit(1.0f, static_cast<float>(size - 2), delaySamples);
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto centre = static_cast<int>(std::floor(position));
    const auto fraction = position - static_cast<float>(centre);
    const auto sampleAt = [this, size](int index)
    {
        while (index < 0)
            index += size;
        return buffer[static_cast<size_t>(index % size)];
    };
    const auto y0 = sampleAt(centre - 1);
    const auto y1 = sampleAt(centre);
    const auto y2 = sampleAt(centre + 1);
    const auto y3 = sampleAt(centre + 2);
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;
    return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
}

void AlgorithmicReverbModule::FractionalDelay::write(float value)
{
    if (buffer.empty())
        return;
    buffer[static_cast<size_t>(writePosition)] = value;
    if (++writePosition >= static_cast<int>(buffer.size()))
        writePosition = 0;
}

void AlgorithmicReverbModule::DecayFilter::reset()
{
    lowState = 0.0f;
    highState = 0.0f;
}

float AlgorithmicReverbModule::DecayFilter::process(
    float input, float lowCoefficient, float highCoefficient,
    float lowGain, float midGain, float highGain)
{
    lowState = lowCoefficient * lowState
               + (1.0f - lowCoefficient) * input;
    highState = highCoefficient * highState
                + (1.0f - highCoefficient) * input;
    const auto low = lowState;
    const auto mid = highState - lowState;
    const auto high = input - highState;
    return low * lowGain + mid * midGain + high * highGain;
}

namespace
{
constexpr std::array<std::array<float, 16>, 3> premiumLineMilliseconds {{
    { 34.7f, 39.1f, 43.9f, 49.3f, 55.1f, 61.7f, 68.9f, 76.9f,
      85.7f, 95.3f, 105.9f, 117.7f, 123.1f, 132.7f, 140.9f, 149.3f },
    { 14.9f, 17.3f, 20.3f, 23.9f, 27.7f, 32.3f, 37.1f, 42.7f,
      48.7f, 55.3f, 59.9f, 64.7f, 69.1f, 73.3f, 77.9f, 82.7f },
    { 7.3f, 8.9f, 10.7f, 12.7f, 15.1f, 17.9f, 21.1f, 24.7f,
      28.7f, 33.1f, 37.9f, 40.9f, 43.7f, 46.3f, 48.7f, 51.1f }
}};

constexpr std::array<float, 16> earlyGains {
    0.42f, -0.34f, 0.29f, -0.24f, 0.20f, -0.17f, 0.14f, -0.12f,
    0.10f, -0.087f, 0.074f, -0.063f, 0.053f, -0.045f, 0.038f, -0.032f
};

constexpr std::array<float, 16> earlyPan {
    -0.82f, 0.67f, -0.31f, 0.91f, 0.18f, -0.72f, 0.49f, -0.12f,
    0.78f, -0.54f, 0.05f, 0.36f, -0.93f, 0.61f, -0.25f, 0.87f
};

constexpr std::array<float, 16> injectionMilliseconds {
    0.7f, 2.3f, 3.1f, 4.9f, 6.7f, 8.9f, 11.3f, 13.9f,
    16.7f, 19.9f, 23.3f, 27.1f, 5.7f, 10.1f, 15.1f, 21.7f
};

constexpr std::array<float, 16> injectionSigns {
    1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
    1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f
};

constexpr std::array<float, 16> outputSignsLeft {
    1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
    1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f
};

constexpr std::array<float, 16> outputSignsRight {
    1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f,
    -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f
};
} // namespace

void AlgorithmicReverbModule::Tank::hadamard(
    std::array<float, lineCount>& values)
{
    for (int stride = 1; stride < lineCount; stride *= 2)
        for (int group = 0; group < lineCount; group += stride * 2)
            for (int index = 0; index < stride; ++index)
            {
                const auto a = values[static_cast<size_t>(group + index)];
                const auto b = values[static_cast<size_t>(group + index + stride)];
                values[static_cast<size_t>(group + index)] = a + b;
                values[static_cast<size_t>(group + index + stride)] = a - b;
            }
    constexpr auto normalization = 0.25f;
    for (auto& value : values)
        value *= normalization;
}

void AlgorithmicReverbModule::Tank::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    const auto delayCapacity = juce::roundToInt(sampleRate * 0.36);
    for (auto& delay : delays)
        delay.prepare(delayCapacity);
    const auto injectionCapacity = juce::roundToInt(sampleRate * 0.04);
    for (auto& history : injectionHistory)
        history.prepare(injectionCapacity);
    reset();
}

void AlgorithmicReverbModule::Tank::reset()
{
    for (auto& delay : delays)
        delay.reset();
    for (auto& history : injectionHistory)
        history.reset();
    for (auto& filter : decayFilters)
        filter.reset();
    lowGains.fill(0.0f);
    midGains.fill(0.0f);
    highGains.fill(0.0f);
    targetLowGains.fill(0.0f);
    targetMidGains.fill(0.0f);
    targetHighGains.fill(0.0f);
    rotationCos.fill(1.0f);
    rotationSin.fill(0.0f);
    rotationDeltaCos.fill(1.0f);
    rotationDeltaSin.fill(0.0f);
    for (int line = 0; line < lineCount; ++line)
    {
        const auto phase = juce::MathConstants<float>::twoPi
                           * static_cast<float>(line)
                           / static_cast<float>(lineCount);
        modulationCos[static_cast<size_t>(line)] = std::cos(phase);
        modulationSin[static_cast<size_t>(line)] = std::sin(phase);
        modulationDeltaCos[static_cast<size_t>(line)] = 1.0f;
        modulationDeltaSin[static_cast<size_t>(line)] = 0.0f;
    }
    currentSize = oldSize = targetSize = 1.0f;
    sizeFade = 1.0f;
    decayInitialized = false;
    oscillatorCounter = 0;
}

void AlgorithmicReverbModule::Tank::setMode(int newMode)
{
    mode = juce::jlimit(0, 2, newMode);
}

void AlgorithmicReverbModule::Tank::setTargets(
    const TankParameters& newParameters)
{
    parameters = newParameters;
    const auto requestedSize = juce::jlimit(0.25f, 2.0f, parameters.size);
    if (sizeFade >= 1.0f && std::abs(requestedSize - currentSize) > 0.002f)
    {
        oldSize = currentSize;
        targetSize = requestedSize;
        sizeFade = 0.0f;
    }

    const auto lowCrossover = mode == 2 ? 330.0f : 260.0f;
    const auto highCrossover = mode == 0 ? 3600.0f
                                         : mode == 1 ? 4300.0f : 5200.0f;
    targetLowCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * lowCrossover
        / static_cast<float>(sampleRate));
    targetHighCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * highCrossover
        / static_cast<float>(sampleRate));
    coefficientSmoothing = 1.0f - std::exp(
        -1.0f / static_cast<float>(sampleRate * 0.06));

    const auto decayRatios = reverbDecayRatios(mode, parameters.damping);
    const auto lowRatio = decayRatios[0];
    const auto highRatio = decayRatios[1];
    for (int line = 0; line < lineCount; ++line)
    {
        const auto delaySeconds =
            premiumLineMilliseconds[static_cast<size_t>(mode)]
                                   [static_cast<size_t>(line)]
            * 0.001f * targetSize;
        const auto midT60 = juce::jmax(0.2f, parameters.decaySeconds);
        const auto lowT60 = juce::jmax(0.15f, midT60 * lowRatio);
        const auto highT60 = juce::jmax(0.10f, midT60 * highRatio);
        targetLowGains[static_cast<size_t>(line)] =
            std::pow(0.001f, delaySeconds / lowT60);
        targetMidGains[static_cast<size_t>(line)] =
            std::pow(0.001f, delaySeconds / midT60);
        targetHighGains[static_cast<size_t>(line)] =
            std::pow(0.001f, delaySeconds / highT60);
    }
    if (!decayInitialized)
    {
        lowGains = targetLowGains;
        midGains = targetMidGains;
        highGains = targetHighGains;
        lowCoefficient = targetLowCoefficient;
        highCoefficient = targetHighCoefficient;
        decayInitialized = true;
    }

    for (int pair = 0; pair < lineCount / 2; ++pair)
    {
        const auto rate = parameters.movement
                          * (0.004f + 0.0013f * static_cast<float>(pair))
                          * (mode == 2 ? 1.8f : mode == 1 ? 1.2f : 1.0f);
        const auto delta = juce::MathConstants<float>::twoPi * rate
                           / static_cast<float>(sampleRate);
        rotationDeltaCos[static_cast<size_t>(pair)] = std::cos(delta);
        rotationDeltaSin[static_cast<size_t>(pair)] = std::sin(delta);
    }
    for (int line = 0; line < lineCount; ++line)
    {
        const auto rate = 0.047f + 0.011f * static_cast<float>(line);
        const auto delta = juce::MathConstants<float>::twoPi * rate
                           / static_cast<float>(sampleRate);
        modulationDeltaCos[static_cast<size_t>(line)] = std::cos(delta);
        modulationDeltaSin[static_cast<size_t>(line)] = std::sin(delta);
    }
}

float AlgorithmicReverbModule::Tank::lineDelaySamples(
    int line, float size, float modulation) const
{
    return premiumLineMilliseconds[static_cast<size_t>(mode)]
                                  [static_cast<size_t>(line)]
           * 0.001f * size * static_cast<float>(sampleRate)
           + modulation;
}

std::array<float, 2> AlgorithmicReverbModule::Tank::process(
    const std::array<float, 2>& input, float density, float movement)
{
    lowCoefficient +=
        (targetLowCoefficient - lowCoefficient) * coefficientSmoothing;
    highCoefficient +=
        (targetHighCoefficient - highCoefficient) * coefficientSmoothing;
    for (int line = 0; line < lineCount; ++line)
    {
        const auto index = static_cast<size_t>(line);
        lowGains[index] +=
            (targetLowGains[index] - lowGains[index]) * coefficientSmoothing;
        midGains[index] +=
            (targetMidGains[index] - midGains[index]) * coefficientSmoothing;
        highGains[index] +=
            (targetHighGains[index] - highGains[index]) * coefficientSmoothing;
    }
    for (int channel = 0; channel < 2; ++channel)
        injectionHistory[static_cast<size_t>(channel)].write(
            input[static_cast<size_t>(channel)]);

    std::array<float, lineCount> tankOutput {};
    std::array<float, lineCount> outputTap {};
    const auto modulationMaximumMs =
        (mode == 2 ? 0.42f : mode == 1 ? 0.16f : 0.12f)
        * movement;
    for (int line = 0; line < lineCount; ++line)
    {
        auto& cosine = modulationCos[static_cast<size_t>(line)];
        auto& sine = modulationSin[static_cast<size_t>(line)];
        const auto nextCosine =
            cosine * modulationDeltaCos[static_cast<size_t>(line)]
            - sine * modulationDeltaSin[static_cast<size_t>(line)];
        sine = sine * modulationDeltaCos[static_cast<size_t>(line)]
               + cosine * modulationDeltaSin[static_cast<size_t>(line)];
        cosine = nextCosine;
        const auto modulation =
            ((line % 3) == 0 ? sine : 0.35f * sine)
            * modulationMaximumMs * 0.001f * static_cast<float>(sampleRate);
        const auto newDelay = lineDelaySamples(line, targetSize, modulation);
        if (sizeFade < 1.0f)
        {
            const auto oldDelay = lineDelaySamples(line, oldSize, modulation);
            const auto oldGain = 1.0f - sizeFade;
            const auto newGain = sizeFade;
            tankOutput[static_cast<size_t>(line)] =
                delays[static_cast<size_t>(line)].read(oldDelay) * oldGain
                + delays[static_cast<size_t>(line)].read(newDelay) * newGain;
            outputTap[static_cast<size_t>(line)] =
                delays[static_cast<size_t>(line)].read(oldDelay * 0.61f) * oldGain
                + delays[static_cast<size_t>(line)].read(newDelay * 0.61f) * newGain;
        }
        else
        {
            tankOutput[static_cast<size_t>(line)] =
                delays[static_cast<size_t>(line)].read(newDelay);
            outputTap[static_cast<size_t>(line)] =
                delays[static_cast<size_t>(line)].read(newDelay * 0.61f);
        }
    }
    if (sizeFade < 1.0f)
    {
        sizeFade = juce::jmin(
            1.0f, sizeFade + 1.0f / static_cast<float>(sampleRate * 0.12));
        if (sizeFade >= 1.0f)
            currentSize = targetSize;
    }

    auto feedback = tankOutput;
    hadamard(feedback);
    for (int pair = 0; pair < lineCount / 2; ++pair)
    {
        auto& cosine = rotationCos[static_cast<size_t>(pair)];
        auto& sine = rotationSin[static_cast<size_t>(pair)];
        const auto nextCosine =
            cosine * rotationDeltaCos[static_cast<size_t>(pair)]
            - sine * rotationDeltaSin[static_cast<size_t>(pair)];
        sine = sine * rotationDeltaCos[static_cast<size_t>(pair)]
               + cosine * rotationDeltaSin[static_cast<size_t>(pair)];
        cosine = nextCosine;
        const auto first = static_cast<size_t>(pair * 2);
        const auto second = first + 1;
        const auto a = feedback[first];
        const auto b = feedback[second];
        feedback[first] = cosine * a - sine * b;
        feedback[second] = sine * a + cosine * b;
    }
    if ((++oscillatorCounter & 4095) == 0)
    {
        auto normalize = [](float& cosine, float& sine)
        {
            const auto length = std::sqrt(cosine * cosine + sine * sine);
            cosine /= juce::jmax(0.0001f, length);
            sine /= juce::jmax(0.0001f, length);
        };
        for (int line = 0; line < lineCount; ++line)
            normalize(modulationCos[static_cast<size_t>(line)],
                      modulationSin[static_cast<size_t>(line)]);
        for (int pair = 0; pair < lineCount / 2; ++pair)
            normalize(rotationCos[static_cast<size_t>(pair)],
                      rotationSin[static_cast<size_t>(pair)]);
    }

    for (int line = 0; line < lineCount; ++line)
    {
        const auto channel = line & 1;
        const auto sparseInput =
            injectionHistory[static_cast<size_t>(channel)].read(
                injectionMilliseconds[static_cast<size_t>(line)]
                * 0.001f * static_cast<float>(sampleRate));
        const auto densityWeight = line < 4
                                       ? 1.0f
                                       : lerp(0.22f, 1.0f, density);
        const auto injectionGain = std::sqrt(juce::jmax(
            0.0001f, 1.0f - midGains[static_cast<size_t>(line)]
                                  * midGains[static_cast<size_t>(line)]));
        const auto decayed =
            decayFilters[static_cast<size_t>(line)].process(
                feedback[static_cast<size_t>(line)],
                lowCoefficient, highCoefficient,
                lowGains[static_cast<size_t>(line)],
                midGains[static_cast<size_t>(line)],
                highGains[static_cast<size_t>(line)]);
        auto next = decayed
                    + sparseInput
                          * injectionSigns[static_cast<size_t>(line)]
                          * densityWeight * injectionGain * 0.36f;
        if (!std::isfinite(next))
            next = 0.0f;
        next = juce::jlimit(-32.0f, 32.0f, next);
        delays[static_cast<size_t>(line)].write(next);
    }

    std::array<float, 2> output {};
    for (int line = 0; line < lineCount; ++line)
    {
        const auto value = tankOutput[static_cast<size_t>(line)]
                           + outputTap[static_cast<size_t>(line)]
                                 * (0.18f + 0.22f * density);
        output[0] += value * outputSignsLeft[static_cast<size_t>(line)];
        output[1] += value * outputSignsRight[static_cast<size_t>(line)];
    }
    output[0] *= 0.16f;
    output[1] *= 0.16f;
    return output;
}

float AlgorithmicReverbModule::readInputHistory(int channel,
                                                 float delaySamples) const
{
    const auto& history = inputHistory[static_cast<size_t>(juce::jmin(channel, 1))];
    if (history.empty())
        return 0.0f;
    const auto size = static_cast<int>(history.size());
    auto position = static_cast<float>(inputWritePosition) - delaySamples;
    while (position < 0.0f)
        position += static_cast<float>(size);
    const auto first = static_cast<int>(position) % size;
    const auto second = (first + 1) % size;
    const auto fraction = position - std::floor(position);
    return history[static_cast<size_t>(first)]
           + (history[static_cast<size_t>(second)]
              - history[static_cast<size_t>(first)]) * fraction;
}

std::array<float, 2> AlgorithmicReverbModule::earlyField(
    int tankIndex, int mode, float preDelaySamples, float size, float density)
{
    std::array<float, 2> output {};
    for (int tap = 0; tap < 16; ++tap)
    {
        const auto delay = preDelaySamples
                           + reverbEarlyMilliseconds()[static_cast<size_t>(mode)]
                                                        [static_cast<size_t>(tap)]
                                 * 0.001f * size
                                 * static_cast<float>(sampleRate);
        const auto sourceChannel = tap & 1;
        const auto otherChannel = 1 - sourceChannel;
        const auto source =
            readInputHistory(sourceChannel, juce::jmax(1.0f, delay)) * 0.78f
            + readInputHistory(otherChannel, juce::jmax(1.0f, delay * 1.013f))
                  * 0.22f;
        const auto cutoff = (mode == 2 ? 15000.0f : 12500.0f)
                            * std::pow(0.965f, static_cast<float>(tap));
        const auto coefficient = std::exp(
            -juce::MathConstants<float>::twoPi * cutoff
            / static_cast<float>(sampleRate));
        const auto scattered = tap < 6 ? 1.0f : lerp(0.15f, 1.0f, density);
        const auto gain = earlyGains[static_cast<size_t>(tap)] * scattered;
        const auto pan = earlyPan[static_cast<size_t>(tap)];
        const auto angle = (pan + 1.0f)
                           * juce::MathConstants<float>::halfPi * 0.5f;
        const std::array<float, 2> panGain {
            std::cos(angle), std::sin(angle)
        };
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& state =
                earlyFilterState[static_cast<size_t>(tankIndex)]
                                [static_cast<size_t>(tap)]
                                [static_cast<size_t>(channel)];
            const auto target = source * gain
                                * panGain[static_cast<size_t>(channel)];
            state = coefficient * state + (1.0f - coefficient) * target;
            output[static_cast<size_t>(channel)] += state;
        }
    }
    const auto direct = readInputHistory(
        0, juce::jmax(1.0f, preDelaySamples)) * 0.5f
                        + readInputHistory(
                              1, juce::jmax(1.0f, preDelaySamples))
                              * 0.5f;
    const auto directGain = mode == 2 ? 0.16f : mode == 1 ? 0.09f : 0.055f;
    output[0] += direct * directGain;
    output[1] += direct * directGain;
    return output;
}

void AlgorithmicReverbModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto historyCapacity = juce::roundToInt(sampleRate * 0.72);
    for (auto& history : inputHistory)
        history.assign(static_cast<size_t>(historyCapacity), 0.0f);
    for (auto& tank : tanks)
        tank.prepare(sampleRate);
    for (auto* smoother : { &decaySmoothed, &sizeSmoothed, &drySmoothed,
                            &wetSmoothed,
                            &widthSmoothed, &preDelaySmoothed, &diffusionSmoothed,
                            &modulationSmoothed, &dampingSmoothed,
                            &lowCoefficientSmoothed,
                            &highCoefficientSmoothed })
        smoother->reset(sampleRate, 0.08);
    sizeSmoothed.reset(sampleRate, 0.12);
    reset();
}

void AlgorithmicReverbModule::reset()
{
    for (auto& history : inputHistory)
        std::fill(history.begin(), history.end(), 0.0f);
    inputWritePosition = 0;
    for (auto& tank : tanks)
        tank.reset();
    for (auto& tankStates : earlyFilterState)
        for (auto& tapStates : tankStates)
            tapStates.fill(0.0f);
    inputLowState.fill(0.0f);
    inputHighState.fill(0.0f);
    widthLowState = 0.0f;
    activeTank = 0;
    pendingMode = 0;
    modeTransition = false;
    modeTransitionPosition = 1.0f;
    parametersInitialized = false;
    decaySmoothed.setCurrentAndTargetValue(exponential(0.2f, 12.0f, 0.5f));
    sizeSmoothed.setCurrentAndTargetValue(1.0f);
    drySmoothed.setCurrentAndTargetValue(1.0f);
    wetSmoothed.setCurrentAndTargetValue(0.10f);
    widthSmoothed.setCurrentAndTargetValue(0.75f);
    preDelaySmoothed.setCurrentAndTargetValue(
        0.125f * static_cast<float>(sampleRate));
    diffusionSmoothed.setCurrentAndTargetValue(0.485f);
    modulationSmoothed.setCurrentAndTargetValue(
        0.00075f * static_cast<float>(sampleRate));
    dampingSmoothed.setCurrentAndTargetValue(0.5f);
    lowCoefficientSmoothed.setCurrentAndTargetValue(std::exp(
        -juce::MathConstants<float>::twoPi * 141.0f
        / static_cast<float>(sampleRate)));
    highCoefficientSmoothed.setCurrentAndTargetValue(std::exp(
        -juce::MathConstants<float>::twoPi * 6000.0f
        / static_cast<float>(sampleRate)));
}

void AlgorithmicReverbModule::process(juce::AudioBuffer<float>& buffer,
                                       const ControlValues& controls,
                                       const ProcessEnvironment&)
{
    if (inputHistory[0].empty())
        return;

    const auto requestedMode = discreteIndex(controls[4], 3);
    if (!parametersInitialized)
    {
        activeTank = 0;
        pendingMode = requestedMode;
        tanks[0].setMode(requestedMode);
        tanks[1].setMode(requestedMode);
        modeTransition = false;
        modeTransitionPosition = 1.0f;
    }
    else if (!modeTransition
             && requestedMode != tanks[static_cast<size_t>(activeTank)].getMode())
    {
        pendingMode = requestedMode;
        const auto targetTank = 1 - activeTank;
        tanks[static_cast<size_t>(targetTank)].reset();
        tanks[static_cast<size_t>(targetTank)].setMode(requestedMode);
        for (auto& states :
             earlyFilterState[static_cast<size_t>(targetTank)])
            states.fill(0.0f);
        modeTransition = true;
        modeTransitionPosition = 0.0f;
    }

    const auto damping = controls[9];
    const auto lowCut = exponential(20.0f, 1000.0f, controls[10]);
    const auto highCut = exponential(2000.0f, 20000.0f, controls[11]);
    const auto targetLowCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * lowCut
        / static_cast<float>(sampleRate));
    const auto targetHighCoefficient = std::exp(
        -juce::MathConstants<float>::twoPi * highCut
        / static_cast<float>(sampleRate));
    decaySmoothed.setTargetValue(exponential(0.2f, 12.0f, controls[0]));
    sizeSmoothed.setTargetValue(lerp(0.25f, 2.0f, controls[1]));
    drySmoothed.setTargetValue(controls[2]);
    wetSmoothed.setTargetValue(controls[3]);
    widthSmoothed.setTargetValue(controls[8] * 1.5f);
    preDelaySmoothed.setTargetValue(
        controls[5] * 0.25f * static_cast<float>(sampleRate));
    diffusionSmoothed.setTargetValue(controls[6]);
    modulationSmoothed.setTargetValue(controls[7]);
    dampingSmoothed.setTargetValue(damping);
    lowCoefficientSmoothed.setTargetValue(targetLowCoefficient);
    highCoefficientSmoothed.setTargetValue(targetHighCoefficient);
    if (!parametersInitialized)
    {
        for (auto* smoother : { &decaySmoothed, &sizeSmoothed, &drySmoothed,
                                &wetSmoothed,
                                &widthSmoothed, &preDelaySmoothed,
                                &diffusionSmoothed, &modulationSmoothed,
                                &dampingSmoothed,
                                &lowCoefficientSmoothed,
                                &highCoefficientSmoothed })
            smoother->setCurrentAndTargetValue(smoother->getTargetValue());
        parametersInitialized = true;
    }
    TankParameters tankParameters;
    tankParameters.decaySeconds = decaySmoothed.getTargetValue();
    tankParameters.size = sizeSmoothed.getTargetValue();
    tankParameters.density = diffusionSmoothed.getTargetValue();
    tankParameters.movement = modulationSmoothed.getTargetValue();
    tankParameters.damping = dampingSmoothed.getTargetValue();
    tanks[static_cast<size_t>(activeTank)].setTargets(tankParameters);
    if (modeTransition)
        tanks[static_cast<size_t>(1 - activeTank)].setTargets(tankParameters);

    const auto historySize = static_cast<int>(inputHistory[0].size());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto decaySeconds = decaySmoothed.getNextValue();
        const auto size = sizeSmoothed.getNextValue();
        const auto dryGain = drySmoothed.getNextValue();
        const auto wetGain = wetSmoothed.getNextValue();
        const auto width = widthSmoothed.getNextValue();
        const auto preDelaySamples = preDelaySmoothed.getNextValue();
        const auto density = diffusionSmoothed.getNextValue();
        const auto movement = modulationSmoothed.getNextValue();
        const auto smoothedDamping = dampingSmoothed.getNextValue();
        const auto lowCoefficient = lowCoefficientSmoothed.getNextValue();
        const auto highCoefficient = highCoefficientSmoothed.getNextValue();
        std::array<float, 2> dry {
            buffer.getSample(0, sample),
            buffer.getNumChannels() > 1 ? buffer.getSample(1, sample)
                                        : buffer.getSample(0, sample)
        };
        std::array<float, 2> conditioned {};
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& lowState = inputLowState[static_cast<size_t>(channel)];
            auto& highState = inputHighState[static_cast<size_t>(channel)];
            lowState = lowCoefficient * lowState
                       + (1.0f - lowCoefficient) * dry[static_cast<size_t>(channel)];
            const auto highPassed = dry[static_cast<size_t>(channel)] - lowState;
            highState = highCoefficient * highState
                        + (1.0f - highCoefficient) * highPassed;
            conditioned[static_cast<size_t>(channel)] = highState;
            inputHistory[static_cast<size_t>(channel)][
                static_cast<size_t>(inputWritePosition)] = highState;
        }

        const auto activeMode =
            tanks[static_cast<size_t>(activeTank)].getMode();
        auto activeEarly = earlyField(
            activeTank, activeMode, preDelaySamples, size, density);
        std::array<float, 2> targetEarly {};
        if (modeTransition)
            targetEarly = earlyField(
                1 - activeTank, pendingMode,
                preDelaySamples, size, density);

        if (++inputWritePosition >= historySize)
            inputWritePosition = 0;

        tankParameters.decaySeconds = decaySeconds;
        tankParameters.density = density;
        tankParameters.movement = movement;
        tankParameters.damping = smoothedDamping;
        std::array<float, 2> tankWet {};
        auto directEarly = activeEarly;
        if (modeTransition)
        {
            const std::array<float, 2> silence {};
            const auto oldWet =
                tanks[static_cast<size_t>(activeTank)].process(
                    silence, density, movement);
            const auto targetTank = 1 - activeTank;
            const auto newWet =
                tanks[static_cast<size_t>(targetTank)].process(
                    targetEarly, density, movement);
            const auto oldGain = std::cos(
                modeTransitionPosition
                * juce::MathConstants<float>::halfPi);
            const auto newGain = std::sin(
                modeTransitionPosition
                * juce::MathConstants<float>::halfPi);
            for (int channel = 0; channel < 2; ++channel)
            {
                tankWet[static_cast<size_t>(channel)] =
                    oldWet[static_cast<size_t>(channel)] * oldGain
                    + newWet[static_cast<size_t>(channel)] * newGain;
                directEarly[static_cast<size_t>(channel)] =
                    activeEarly[static_cast<size_t>(channel)] * oldGain
                    + targetEarly[static_cast<size_t>(channel)] * newGain;
            }
            modeTransitionPosition = juce::jmin(
                1.0f, modeTransitionPosition
                          + 1.0f / static_cast<float>(sampleRate * 0.35));
            if (modeTransitionPosition >= 1.0f)
            {
                tanks[static_cast<size_t>(activeTank)].reset();
                activeTank = targetTank;
                modeTransition = false;
            }
        }
        else
            tankWet =
                tanks[static_cast<size_t>(activeTank)].process(
                    activeEarly, density, movement);

        const auto wetLeft = directEarly[0] * 0.32f + tankWet[0];
        const auto wetRight = directEarly[1] * 0.32f + tankWet[1];
        const auto mid = (wetLeft + wetRight) * 0.5f;
        const auto rawSide = (wetLeft - wetRight) * 0.5f;
        const auto widthCoefficient = std::exp(
            -juce::MathConstants<float>::twoPi * 280.0f
            / static_cast<float>(sampleRate));
        widthLowState = widthCoefficient * widthLowState
                        + (1.0f - widthCoefficient) * rawSide;
        const auto side = (widthLowState * 0.68f
                           + (rawSide - widthLowState))
                          * width;
        const std::array<float, 2> wet {
            mid + side,
            mid - side
        };
        if (buffer.getNumChannels() == 1)
            buffer.setSample(0, sample, dry[0] * dryGain
                                      + mid * wetGain);
        else
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample(channel, sample,
                    dry[static_cast<size_t>(channel)] * dryGain
                    + wet[static_cast<size_t>(channel)] * wetGain);
    }
}

double AlgorithmicReverbModule::tailSeconds(const ControlValues& controls) const
{
    const auto mode = discreteIndex(controls[4], 3);
    const auto ratios = reverbDecayRatios(mode, controls[9]);
    const auto longestRatio = juce::jmax(1.0f, juce::jmax(ratios[0], ratios[1]));
    return static_cast<double>(
        exponential(0.2f, 12.0f, controls[0]) * longestRatio
        + lerp(0.0f, 0.25f, controls[5]) + 0.75f);
}
} // namespace megadsp
