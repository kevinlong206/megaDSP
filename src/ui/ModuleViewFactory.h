#pragma once

#include "../modules/ModuleRegistry.h"

#include <memory>

namespace megadsp::ui
{
class EffectGraph;
class ModuleView;

std::unique_ptr<ModuleView> createModuleView(
    ModulePresentation, EffectGraph&);
} // namespace megadsp::ui
