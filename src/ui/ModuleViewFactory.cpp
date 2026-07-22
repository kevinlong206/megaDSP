#include "ModuleViewFactory.h"

#include "EffectGraph.h"
#include "ModuleView.h"
#include "modules/ModuleViewCreators.h"

#include <array>

namespace megadsp::ui
{
namespace
{
class EmptyModuleView final : public ModuleView
{
public:
    using ModuleView::ModuleView;
    void paint(juce::Graphics&, juce::Rectangle<float>) override {}
};

std::unique_ptr<ModuleView> createEmptyView(EffectGraph& graph)
{
    return std::make_unique<EmptyModuleView>(graph);
}

using ViewCreator = std::unique_ptr<ModuleView> (*)(EffectGraph&);
constexpr std::array<ViewCreator,
                     static_cast<size_t>(ModulePresentation::count)>
    viewCreators {
        &createEmptyView,
        &createEqualizerView,
        &createCompressorView,
        &createSaturatorView,
        &createDelayView,
        &createLimiterView,
        &createAlgorithmicReverbView,
        &createStereoWidthView,
        &createMidSideDecoderView,
        &createTremoloView,
        &createRotarySpeakerView,
        &createConvolutionReverbView,
        &createDynamicEqualizerView,
        &createRandomGranulizerView,
        &createVintageChorusView,
        &createBeatPermuterView,
        &createSpectralPrismView,
        &createResonantMatrixView,
        &createWavefoldGardenView
    };

static_assert([]
{
    for (const auto creator : viewCreators)
        if (creator == nullptr)
            return false;
    return true;
}());
} // namespace

std::unique_ptr<ModuleView> createModuleView(
    ModulePresentation presentation, EffectGraph& graph)
{
    const auto index = static_cast<size_t>(presentation);
    if (index >= viewCreators.size())
        return createEmptyView(graph);
    return viewCreators[index](graph);
}
} // namespace megadsp::ui
