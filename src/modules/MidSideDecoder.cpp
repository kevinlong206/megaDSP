#include "MidSideDecoder.h"

#include <cmath>

namespace megadsp
{
void MidSideDecoderModule::prepare(const juce::dsp::ProcessSpec& spec)
{
    for (auto* smoother : {
             static_cast<juce::SmoothedValue<float>*>(&widthSmoothed),
             static_cast<juce::SmoothedValue<float>*>(&sideMuteSmoothed),
             static_cast<juce::SmoothedValue<float>*>(&swapSmoothed) })
        smoother->reset(spec.sampleRate, 0.015);
    reset();
}

void MidSideDecoderModule::reset()
{
    widthSmoothed.setCurrentAndTargetValue(0.35f);
    sideMuteSmoothed.setCurrentAndTargetValue(1.0f);
    swapSmoothed.setCurrentAndTargetValue(0.0f);
}

void MidSideDecoderModule::process(juce::AudioBuffer<float>& buffer,
                                   const ControlValues& controls,
                                   const ProcessEnvironment&)
{
    widthSmoothed.setTargetValue(controls[0]);
    swapSmoothed.setTargetValue(controls[1] >= 0.5f ? 1.0f : 0.0f);
    sideMuteSmoothed.setTargetValue(controls[2] >= 0.5f ? 0.0f : 1.0f);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto mid = buffer.getSample(0, sample);
        if (buffer.getNumChannels() < 2)
        {
            widthSmoothed.getNextValue();
            sideMuteSmoothed.getNextValue();
            swapSmoothed.getNextValue();
            buffer.setSample(0, sample, mid);
            continue;
        }

        const auto side = buffer.getSample(1, sample)
                          * widthSmoothed.getNextValue()
                          * sideMuteSmoothed.getNextValue();
        const auto decodedLeft = mid + side;
        const auto decodedRight = mid - side;
        const auto swap = swapSmoothed.getNextValue();
        const auto left = decodedLeft
                          + (decodedRight - decodedLeft) * swap;
        const auto right = decodedRight
                           + (decodedLeft - decodedRight) * swap;
        buffer.setSample(0, sample, left);
        buffer.setSample(1, sample, right);
    }
}
} // namespace megadsp
