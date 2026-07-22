#pragma once

#include <memory>

namespace megadsp::ui
{
class EffectGraph;
class ModuleView;

std::unique_ptr<ModuleView> createEqualizerView(EffectGraph&);
std::unique_ptr<ModuleView> createCompressorView(EffectGraph&);
std::unique_ptr<ModuleView> createSaturatorView(EffectGraph&);
std::unique_ptr<ModuleView> createDelayView(EffectGraph&);
std::unique_ptr<ModuleView> createLimiterView(EffectGraph&);
std::unique_ptr<ModuleView> createAlgorithmicReverbView(EffectGraph&);
std::unique_ptr<ModuleView> createConvolutionReverbView(EffectGraph&);
std::unique_ptr<ModuleView> createStereoWidthView(EffectGraph&);
std::unique_ptr<ModuleView> createMidSideDecoderView(EffectGraph&);
std::unique_ptr<ModuleView> createTremoloView(EffectGraph&);
std::unique_ptr<ModuleView> createRotarySpeakerView(EffectGraph&);
std::unique_ptr<ModuleView> createDynamicEqualizerView(EffectGraph&);
std::unique_ptr<ModuleView> createRandomGranulizerView(EffectGraph&);
std::unique_ptr<ModuleView> createVintageChorusView(EffectGraph&);
std::unique_ptr<ModuleView> createBeatPermuterView(EffectGraph&);
std::unique_ptr<ModuleView> createSpectralPrismView(EffectGraph&);
std::unique_ptr<ModuleView> createResonantMatrixView(EffectGraph&);
std::unique_ptr<ModuleView> createWavefoldGardenView(EffectGraph&);
std::unique_ptr<ModuleView> createGateExpanderView(EffectGraph&);
std::unique_ptr<ModuleView> createTransientDesignerView(EffectGraph&);
std::unique_ptr<ModuleView> createMultibandCompressorView(EffectGraph&);
std::unique_ptr<ModuleView> createStudioPhaserView(EffectGraph&);
std::unique_ptr<ModuleView> createStudioFlangerView(EffectGraph&);
std::unique_ptr<ModuleView> createDiffusionDelayView(EffectGraph&);
std::unique_ptr<ModuleView> createPitchBloomView(EffectGraph&);
std::unique_ptr<ModuleView> createFrequencyLabView(EffectGraph&);
std::unique_ptr<ModuleView> createSpatialOrbitView(EffectGraph&);
std::unique_ptr<ModuleView> createSignalDecayView(EffectGraph&);
} // namespace megadsp::ui
