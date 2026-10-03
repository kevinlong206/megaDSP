#pragma once

#include "Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>

namespace megadsp
{
template <size_t Capacity>
class SampleHistory
{
public:
    void push(float value)
    {
        const auto position = writePosition.load(std::memory_order_relaxed);
        samples[position % Capacity].store(value, std::memory_order_relaxed);
        writePosition.store(position + 1, std::memory_order_release);
    }

    template <size_t DestinationSize>
    size_t copyLatest(
        std::array<float, DestinationSize>& destination,
        float emptyValue = 0.0f) const
    {
        destination.fill(emptyValue);
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto end = writePosition.load(std::memory_order_acquire);
            const auto available = static_cast<size_t>(juce::jmin<uint64_t>(
                end, static_cast<uint64_t>(juce::jmin(Capacity, DestinationSize))));
            const auto start = end - available;
            const auto offset = DestinationSize - available;
            for (size_t index = 0; index < available; ++index)
                destination[offset + index] =
                    samples[(start + index) % Capacity].load(std::memory_order_relaxed);
            const auto finalPosition = writePosition.load(std::memory_order_acquire);
            if (finalPosition - end < Capacity)
                return available;
        }
        return 0;
    }

    void clear()
    {
        for (auto& sample : samples)
            sample.store(0.0f, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_release);
    }

    uint64_t position() const noexcept
    {
        return writePosition.load(std::memory_order_acquire);
    }

private:
    std::array<std::atomic<float>, Capacity> samples {};
    std::atomic<uint64_t> writePosition { 0 };
};

struct SlotVisualizationData
{
    SampleHistory<8192> input;
    SampleHistory<8192> output;
    SampleHistory<8192> outputLeft;
    SampleHistory<8192> outputRight;
    SampleHistory<2048> gainReduction;
    SampleHistory<512> inputLevel;
    SampleHistory<512> outputLevel;
    SampleHistory<512> gainReductionLevel;

    struct LevelWriter
    {
        void prepare(double sampleRate)
        {
            interval = juce::jmax(1, juce::roundToInt(sampleRate / 50.0));
            remaining = interval;
            peak = 0.0f;
        }

        void pushMagnitude(float magnitude, SampleHistory<512>& history)
        {
            peak = juce::jmax(peak, magnitude);
            if (--remaining <= 0)
            {
                history.push(juce::Decibels::gainToDecibels(peak, -60.0f));
                remaining = interval;
                peak = 0.0f;
            }
        }

        void pushValue(float value, int numSamples, SampleHistory<512>& history)
        {
            for (int sample = 0; sample < numSamples; ++sample)
            {
                peak = juce::jmax(peak, value);
                if (--remaining <= 0)
                {
                    history.push(peak);
                    remaining = interval;
                    peak = 0.0f;
                }
            }
        }

        int interval = 960;
        int remaining = 960;
        float peak = 0.0f;
    };

    LevelWriter inputLevelWriter;
    LevelWriter outputLevelWriter;
    LevelWriter gainReductionWriter;

    void prepare(double sampleRate)
    {
        inputLevelWriter.prepare(sampleRate);
        outputLevelWriter.prepare(sampleRate);
        gainReductionWriter.prepare(sampleRate);
    }

    void clear()
    {
        input.clear();
        output.clear();
        outputLeft.clear();
        outputRight.clear();
        gainReduction.clear();
        inputLevel.clear();
        outputLevel.clear();
        gainReductionLevel.clear();
    }
};

class VisualizationData
{
public:
    void setSlotActive(int slot, bool active)
    {
        if (!juce::isPositiveAndBelow(slot, slotCount))
            return;
        const auto bit = std::uint32_t { 1 } << static_cast<unsigned>(slot);
        if (active)
            activeSlotMask.fetch_or(bit, std::memory_order_release);
        else
            activeSlotMask.fetch_and(~bit, std::memory_order_release);
    }

    bool isSlotActive(int slot) const
    {
        if (!juce::isPositiveAndBelow(slot, slotCount))
            return false;
        const auto bit = std::uint32_t { 1 } << static_cast<unsigned>(slot);
        return (activeSlotMask.load(std::memory_order_acquire) & bit) != 0;
    }

    std::uint32_t getActiveSlotMask() const
    {
        return activeSlotMask.load(std::memory_order_acquire);
    }

    void setActiveSlotMask(std::uint32_t mask)
    {
        constexpr auto validMask =
            (std::uint32_t { 1 } << static_cast<unsigned>(slotCount)) - 1u;
        activeSlotMask.store(mask & validMask, std::memory_order_release);
    }

    void clearActiveSlots()
    {
        activeSlotMask.store(0, std::memory_order_release);
    }

    void prepare(double sampleRate)
    {
        for (auto& slot : slots)
            slot.prepare(sampleRate);
        clear();
    }

    void captureInput(int slot, const juce::AudioBuffer<float>& buffer)
    {
        if (juce::isPositiveAndBelow(slot, slotCount))
        {
            auto& data = slots[static_cast<size_t>(slot)];
            capture(buffer, data.input, data.inputLevel, data.inputLevelWriter);
        }
    }

    void captureOutput(int slot, const juce::AudioBuffer<float>& buffer)
    {
        if (juce::isPositiveAndBelow(slot, slotCount))
        {
            auto& data = slots[static_cast<size_t>(slot)];
            capture(buffer, data.output, data.outputLevel, data.outputLevelWriter);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto left = buffer.getSample(0, sample);
                const auto right = buffer.getNumChannels() > 1
                                       ? buffer.getSample(1, sample) : left;
                data.outputLeft.push(left);
                data.outputRight.push(right);
            }
        }
    }

    void captureGainReduction(int slot, float value, int numSamples)
    {
        if (juce::isPositiveAndBelow(slot, slotCount))
        {
            auto& data = slots[static_cast<size_t>(slot)];
            data.gainReduction.push(value);
            data.gainReductionWriter.pushValue(
                value, numSamples, data.gainReductionLevel);
        }
    }

    SlotVisualizationData& slotData(int slot)
    {
        return slots[static_cast<size_t>(juce::jlimit(0, slotCount - 1, slot))];
    }

    void clear()
    {
        for (auto& slot : slots)
            slot.clear();
    }

private:
    static void capture(const juce::AudioBuffer<float>& buffer,
                        SampleHistory<8192>& waveform,
                        SampleHistory<512>& levels,
                        SlotVisualizationData::LevelWriter& levelWriter)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float power = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto value = buffer.getSample(channel, sample);
                power += value * value;
            }
            const auto rms = std::sqrt(power / static_cast<float>(
                juce::jmax(1, buffer.getNumChannels())));
            const auto signSource = buffer.getNumChannels() > 0
                                        ? buffer.getSample(0, sample) : 0.0f;
            waveform.push(std::copysign(rms, signSource));
            levelWriter.pushMagnitude(rms, levels);
        }
    }

    std::array<SlotVisualizationData, slotCount> slots;
    std::atomic<std::uint32_t> activeSlotMask { 0 };
};
} // namespace megadsp
