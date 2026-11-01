#pragma once

#include "Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <algorithm>
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
        pushBlock(1, [value](int) { return value; });
    }

    template <typename Read>
    void pushBlock(int count, Read read)
    {
        if (count <= 0)
            return;
        writeGeneration.fetch_add(1, std::memory_order_seq_cst);
        const auto position = writePosition.load(std::memory_order_relaxed);
        for (int index = 0; index < count; ++index)
            samples[(position + static_cast<uint64_t>(index)) % Capacity]
                .store(read(index), std::memory_order_relaxed);
        writePosition.store(
            position + static_cast<uint64_t>(count), std::memory_order_release);
        writeGeneration.fetch_add(1, std::memory_order_seq_cst);
    }

    template <size_t DestinationSize>
    size_t copyLatest(
        std::array<float, DestinationSize>& destination,
        float emptyValue = 0.0f) const
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto generation =
                writeGeneration.load(std::memory_order_seq_cst);
            if ((generation & 1u) != 0)
                continue;
            const auto end = writePosition.load(std::memory_order_acquire);
            const auto available = static_cast<size_t>(juce::jmin<uint64_t>(
                end, static_cast<uint64_t>(juce::jmin(Capacity, DestinationSize))));
            const auto start = end - available;
            const auto offset = DestinationSize - available;
            std::fill_n(destination.begin(), offset, emptyValue);
            for (size_t index = 0; index < available; ++index)
                destination[offset + index] =
                    samples[(start + index) % Capacity].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (writeGeneration.load(std::memory_order_seq_cst) == generation)
                return available;
        }
        destination.fill(emptyValue);
        return 0;
    }

    void clear()
    {
        writeGeneration.fetch_add(1, std::memory_order_seq_cst);
        writePosition.store(0, std::memory_order_release);
        writeGeneration.fetch_add(1, std::memory_order_seq_cst);
    }

    uint64_t position() const noexcept
    {
        return writePosition.load(std::memory_order_acquire);
    }

private:
    std::array<std::atomic<float>, Capacity> samples {};
    std::atomic<uint64_t> writePosition { 0 };
    // Payloads remain atomic: generation checks reject overlapping block writes.
    std::atomic<uint64_t> writeGeneration { 0 };
};

struct SlotVisualizationData
{
    SampleHistory<8192> input;
    SampleHistory<8192> output;
    SampleHistory<16384> inputLeft;
    SampleHistory<16384> inputRight;
    SampleHistory<16384> outputLeft;
    SampleHistory<16384> outputRight;
    SampleHistory<2048> gainReduction;
    SampleHistory<512> inputLevel;
    SampleHistory<512> outputLevel;
    SampleHistory<512> gainReductionLevel;

    template <size_t Size>
    bool copyOutputStereoLatest(
        std::array<float, Size>& left, std::array<float, Size>& right) const
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto end = outputLeft.position();
            if (outputRight.position() != end)
                continue;
            const auto leftCount = outputLeft.copyLatest(left);
            const auto rightCount = outputRight.copyLatest(right);
            if (leftCount == rightCount && (end == 0 || leftCount != 0)
                && outputLeft.position() == end && outputRight.position() == end)
                return true;
        }
        return false;
    }

    struct LevelWriter
    {
        void prepare(double sampleRate)
        {
            interval = juce::jmax(1, juce::roundToInt(sampleRate / 50.0));
            reset();
        }

        void reset()
        {
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
            while (numSamples > 0)
            {
                peak = juce::jmax(peak, value);
                const auto count = juce::jmin(remaining, numSamples);
                remaining -= count;
                numSamples -= count;
                if (remaining == 0)
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
        inputLeft.clear();
        inputRight.clear();
        outputLeft.clear();
        outputRight.clear();
        gainReduction.clear();
        inputLevel.clear();
        outputLevel.clear();
        gainReductionLevel.clear();
        inputLevelWriter.reset();
        outputLevelWriter.reset();
        gainReductionWriter.reset();
    }
};

class VisualizationData
{
public:
    void setCaptureRequirements(int slot, std::uint32_t requirements)
    {
        if (juce::isPositiveAndBelow(slot, slotCount))
        {
            const auto index = static_cast<size_t>(slot);
            const auto next = requirements & captureAll;
            if (captureRequirements[index].exchange(
                    next, std::memory_order_acq_rel) != next)
                resetRequested[index].store(true, std::memory_order_release);
        }
    }

    std::uint32_t getCaptureRequirements(int slot) const
    {
        return juce::isPositiveAndBelow(slot, slotCount)
            ? captureRequirements[static_cast<size_t>(slot)]
                  .load(std::memory_order_acquire) : 0;
    }

    void copyCaptureRequirementsFrom(const VisualizationData& other)
    {
        for (int slot = 0; slot < slotCount; ++slot)
            setCaptureRequirements(slot, other.getCaptureRequirements(slot));
    }

    void setSlotActive(int slot, bool active)
    {
        if (!juce::isPositiveAndBelow(slot, slotCount))
            return;
        const auto bit = std::uint32_t { 1 } << static_cast<unsigned>(slot);
        const auto previous = active
            ? activeSlotMask.fetch_or(bit, std::memory_order_acq_rel)
            : activeSlotMask.fetch_and(~bit, std::memory_order_acq_rel);
        if (((previous & bit) != 0) != active)
            resetRequested[static_cast<size_t>(slot)].store(
                true, std::memory_order_release);
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
        const auto next = mask & validMask;
        const auto previous =
            activeSlotMask.exchange(next, std::memory_order_acq_rel);
        for (int slot = 0; slot < slotCount; ++slot)
            if (((previous ^ next)
                 & (std::uint32_t { 1 } << static_cast<unsigned>(slot))) != 0)
                resetRequested[static_cast<size_t>(slot)].store(
                    true, std::memory_order_release);
    }

    void clearActiveSlots()
    {
        setActiveSlotMask(0);
    }

    void prepare(double sampleRate)
    {
        for (auto& slot : slots)
            slot.prepare(sampleRate);
        clear();
    }

    void captureInput(int slot, const juce::AudioBuffer<float>& buffer)
    {
        if (juce::isPositiveAndBelow(slot, slotCount)
            && buffer.getNumChannels() > 0)
        {
            auto& data = slots[static_cast<size_t>(slot)];
            // Visibility requests a reset; only the audio producer clears histories.
            if (resetRequested[static_cast<size_t>(slot)].exchange(
                    false, std::memory_order_acq_rel))
            {
                data.clear();
            }
            const auto requirements = getCaptureRequirements(slot);
            capture(buffer, data.input, data.inputLevel, data.inputLevelWriter,
                    (requirements & captureMonoInput) != 0,
                    (requirements & captureInputLevels) != 0);
            if ((requirements & captureStereoInput) != 0)
            {
                data.inputLeft.pushBlock(buffer.getNumSamples(),
                    [&buffer](int sample) { return buffer.getSample(0, sample); });
                data.inputRight.pushBlock(buffer.getNumSamples(),
                    [&buffer](int sample)
                    {
                        return buffer.getSample(
                            buffer.getNumChannels() > 1 ? 1 : 0, sample);
                    });
            }
        }
    }

    void captureOutput(int slot, const juce::AudioBuffer<float>& buffer)
    {
        if (juce::isPositiveAndBelow(slot, slotCount)
            && buffer.getNumChannels() > 0)
        {
            auto& data = slots[static_cast<size_t>(slot)];
            const auto requirements = getCaptureRequirements(slot);
            capture(buffer, data.output, data.outputLevel, data.outputLevelWriter,
                    (requirements & captureMonoOutput) != 0,
                    (requirements & captureOutputLevels) != 0);
            if ((requirements & captureStereoOutput) != 0)
            {
                data.outputLeft.pushBlock(buffer.getNumSamples(),
                    [&buffer](int sample) { return buffer.getSample(0, sample); });
                data.outputRight.pushBlock(buffer.getNumSamples(),
                    [&buffer](int sample)
                    {
                        return buffer.getSample(
                            buffer.getNumChannels() > 1 ? 1 : 0, sample);
                    });
            }
        }
    }

    void captureGainReduction(int slot, float value, int numSamples)
    {
        if (juce::isPositiveAndBelow(slot, slotCount)
            && (getCaptureRequirements(slot) & captureReduction) != 0)
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
                        SlotVisualizationData::LevelWriter& levelWriter,
                        bool captureWaveform, bool captureLevels)
    {
        if (!captureWaveform && !captureLevels)
            return;
        const auto read = [&](int sample)
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
            if (captureLevels)
                levelWriter.pushMagnitude(rms, levels);
            return std::copysign(rms, signSource);
        };
        if (captureWaveform)
            waveform.pushBlock(buffer.getNumSamples(), read);
        else
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                read(sample);
        }
    }

    std::array<SlotVisualizationData, slotCount> slots;
    std::atomic<std::uint32_t> activeSlotMask { 0 };
    std::array<std::atomic<std::uint32_t>, slotCount> captureRequirements {
        captureAll, captureAll, captureAll, captureAll,
        captureAll, captureAll, captureAll, captureAll
    };
    std::array<std::atomic<bool>, slotCount> resetRequested {};
};
} // namespace megadsp
