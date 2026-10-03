#include "ReaperEmbeddedUI.h"

#include "PluginProcessor.h"
#include "ui/GraphStyle.h"

#include <cstring>

namespace reaper
{
DEF_CLASS_IID(IReaperUIEmbedInterface)
} // namespace reaper

namespace megadsp
{
namespace
{
constexpr auto invalidate =
    Steinberg::TPtrInt { REAPER_FXEMBED_RETNOTIFY_INVALIDATE };

bool isContinuous(ControlKind kind)
{
    return kind == ControlKind::rotary
        || kind == ControlKind::horizontal
        || kind == ControlKind::level;
}

float rawValue(
    const MegaDSPAudioProcessor& processor, int slot, int control)
{
    if (const auto* value = processor.parameters.getRawParameterValue(
            controlParameterId(slot, control)))
        return value->load();
    return 0.5f;
}
} // namespace

ReaperEmbeddedUI::ReaperEmbeddedUI(
    MegaDSPAudioProcessor& processorToUse)
    : processor(processorToUse)
{
}

int32_t ReaperEmbeddedUI::queryIEditController(
    const Steinberg::TUID interfaceId, void** object)
{
    return queryInterface(interfaceId, object);
}

Steinberg::uint32 PLUGIN_API ReaperEmbeddedUI::addRef()
{
    return ++referenceCount;
}

Steinberg::uint32 PLUGIN_API ReaperEmbeddedUI::release()
{
    const auto current = referenceCount.load();
    return current > 1 ? --referenceCount : current;
}

Steinberg::tresult PLUGIN_API ReaperEmbeddedUI::queryInterface(
    const Steinberg::TUID interfaceId, void** object)
{
    if (object != nullptr
        && std::memcmp(
            interfaceId, reaper::IReaperUIEmbedInterface::iid,
            sizeof(Steinberg::TUID)) == 0)
    {
        addRef();
        *object = static_cast<reaper::IReaperUIEmbedInterface*>(this);
        return Steinberg::kResultOk;
    }
    if (object != nullptr)
        *object = nullptr;
    return Steinberg::kNoInterface;
}

ReaperEmbeddedUI::Layout ReaperEmbeddedUI::createLayout(
    juce::Rectangle<int> bounds, int context) const
{
    Layout layout;
    layout.count = juce::jlimit(
        0, slotCount, processor.getRack().activeSlotCount());
    layout.vertical =
        context == 2 || (context == 0 && bounds.getHeight() > bounds.getWidth());
    if (layout.count == 0)
        return layout;

    bounds = bounds.reduced(2);
    for (int slot = 0; slot < layout.count; ++slot)
    {
        if (layout.vertical)
        {
            const auto top = bounds.getY()
                + slot * bounds.getHeight() / layout.count;
            const auto bottom = bounds.getY()
                + (slot + 1) * bounds.getHeight() / layout.count;
            layout.tiles[static_cast<size_t>(slot)] = {
                bounds.getX(), top, bounds.getWidth(), bottom - top
            };
        }
        else
        {
            const auto left = bounds.getX()
                + slot * bounds.getWidth() / layout.count;
            const auto right = bounds.getX()
                + (slot + 1) * bounds.getWidth() / layout.count;
            layout.tiles[static_cast<size_t>(slot)] = {
                left, bounds.getY(), right - left, bounds.getHeight()
            };
        }
        layout.tiles[static_cast<size_t>(slot)].reduce(1, 1);
    }
    return layout;
}

int ReaperEmbeddedUI::primaryControl(int slot) const
{
    if (!juce::isPositiveAndBelow(
            slot, processor.getRack().activeSlotCount()))
        return -1;
    const auto type = processor.getRack().activeModuleType(slot);
    std::array<float, controlsPerSlot> values {};
    for (int control = 0; control < controlsPerSlot; ++control)
        values[static_cast<size_t>(control)] =
            rawValue(processor, slot, control);

    const auto findControl = [&](bool essential)
    {
        for (int control = 0; control < controlsPerSlot; ++control)
        {
            const auto& metadata = controlMetadata(type, control);
            if (metadata.essential != essential
                || !isContinuous(metadata.kind)
                || !isControlContextuallyVisible(type, control, values)
                || !isControlContextuallyEnabled(
                    type, control, values,
                    processor.hasStereoOutput(),
                    processor.hasExternalSidechain()))
                continue;
            return control;
        }
        return -1;
    };
    const auto essential = findControl(true);
    return essential >= 0 ? essential : findControl(false);
}

juce::Rectangle<int> ReaperEmbeddedUI::bypassBounds(
    juce::Rectangle<int> tile) const
{
    const auto size = juce::jlimit(
        10, 18, juce::jmin(tile.getWidth(), tile.getHeight()) / 3);
    return juce::Rectangle<int>(size, size)
        .withPosition(
            tile.getRight() - size - 3, tile.getY() + 3);
}

int ReaperEmbeddedUI::tileAt(
    juce::Point<int> point, const Layout& layout) const
{
    for (int slot = 0; slot < layout.count; ++slot)
        if (layout.tiles[static_cast<size_t>(slot)].contains(point))
            return slot;
    return -1;
}

void ReaperEmbeddedUI::paint(
    juce::Graphics& graphics, juce::Rectangle<int> bounds,
    int context)
{
    const auto background = ui::backgroundColour(
        processor.getBackgroundThemeIndex()).darker(0.28f);
    graphics.fillAll(background);
    const auto layout = createLayout(bounds, context);
    if (layout.count == 0)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.55f));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawFittedText(
            "megaDSP - empty chain", bounds.reduced(5),
            juce::Justification::centred, 1);
        return;
    }
    for (int slot = 0; slot < layout.count; ++slot)
        paintTile(
            graphics, layout.tiles[static_cast<size_t>(slot)],
            slot, layout.vertical);
}

void ReaperEmbeddedUI::paintTile(
    juce::Graphics& graphics, juce::Rectangle<int> tile,
    int slot, bool verticalLayout)
{
    if (tile.isEmpty())
        return;
    const auto type = processor.getRack().activeModuleType(slot);
    const auto accent = ui::modulePaletteColour(type);
    const auto bypassed = processor.isSlotBypassed(slot);
    const auto selected = processor.getSelectedTab() == slot;
    const auto surface = ui::moduleSurfaceColour(
        ui::backgroundColour(processor.getBackgroundThemeIndex()),
        type, bypassed ? 0.09f : 0.34f);
    auto area = tile.toFloat();
    const auto radius = juce::jmin(5.0f, area.getHeight() * 0.18f);
    graphics.setColour(surface.withMultipliedAlpha(
        bypassed ? 0.62f : 1.0f));
    graphics.fillRoundedRectangle(area, radius);
    graphics.setColour(
        selected ? juce::Colours::white.withAlpha(0.88f)
                 : accent.withAlpha(0.72f));
    graphics.drawRoundedRectangle(
        area.reduced(0.75f), radius, selected ? 1.6f : 0.8f);

    graphics.setColour(accent.withMultipliedAlpha(
        bypassed ? 0.42f : 1.0f));
    if (verticalLayout)
        graphics.fillRect(
            area.getX(), area.getY(), 3.0f, area.getHeight());
    else
        graphics.fillRect(
            area.getX(), area.getY(), area.getWidth(), 3.0f);

    const auto bypass = bypassBounds(tile);
    graphics.setColour(
        bypassed ? juce::Colour(0xffff8d83)
                 : juce::Colour(0xff63df9a));
    graphics.fillEllipse(bypass.toFloat().reduced(2.0f));
    graphics.setColour(juce::Colour(0xff111820));
    graphics.setFont(juce::FontOptions(
        juce::jmax(
            6.0f, static_cast<float>(bypass.getHeight()) * 0.42f),
        juce::Font::bold));
    graphics.drawText(
        bypassed ? "B" : "O", bypass,
        juce::Justification::centred);

    auto content = tile.reduced(5, 3);
    content.removeFromRight(bypass.getWidth() + 2);
    const auto compact = tile.getHeight() < 24 || tile.getWidth() < 40;
    const auto name = juce::String(slot + 1) + " "
        + moduleDefinition(type).displayName;
    graphics.setColour(juce::Colours::white.withAlpha(
        bypassed ? 0.48f : 0.90f));
    graphics.setFont(juce::FontOptions(
        compact ? 7.0f : 9.0f, juce::Font::bold));
    graphics.drawFittedText(
        name, compact ? content : content.removeFromTop(
            juce::jmin(15, content.getHeight())),
        juce::Justification::centredLeft, 1, 0.65f);
    if (compact || content.getHeight() < 9)
        return;

    const auto control = primaryControl(slot);
    if (control < 0)
        return;
    const auto value = rawValue(processor, slot, control);
    auto valueRow = content.removeFromBottom(
        juce::jmin(15, content.getHeight()));
    graphics.setColour(juce::Colours::white.withAlpha(
        bypassed ? 0.40f : 0.70f));
    graphics.setFont(juce::FontOptions(7.5f));
    const auto valueText =
        juce::String(controlMetadata(type, control).label)
        + " " + formatControlValue(type, control, value);
    graphics.drawFittedText(
        valueText, valueRow,
        juce::Justification::centredLeft, 1, 0.55f);

    auto bar = content.removeFromBottom(
        juce::jmin(5, content.getHeight())).toFloat();
    if (!bar.isEmpty())
    {
        graphics.setColour(juce::Colours::black.withAlpha(0.42f));
        graphics.fillRoundedRectangle(bar, 1.5f);
        graphics.setColour(accent.withMultipliedAlpha(
            bypassed ? 0.30f : 0.92f));
        graphics.fillRoundedRectangle(
            bar.withWidth(bar.getWidth() * value), 1.5f);
    }

    const auto rawActivity =
        std::abs(processor.getRack().slotMeter(slot));
    const auto activity = juce::jlimit(
        0.0f, 1.0f,
        rawActivity <= 1.0f ? rawActivity : rawActivity / 24.0f);
    if (activity > 0.001f)
    {
        auto meter = area.removeFromBottom(2.0f);
        graphics.setColour(juce::Colour(0xff63df9a).withAlpha(0.72f));
        graphics.fillRect(
            meter.withWidth(meter.getWidth() * activity));
    }
}

Steinberg::TPtrInt ReaperEmbeddedUI::paintToBitmap(
    reaper::REAPER_FXEMBED_IBitmap* bitmap,
    const reaper::REAPER_FXEMBED_DrawInfo* info)
{
    if (bitmap == nullptr || info == nullptr
        || bitmap->getWidth() <= 0 || bitmap->getHeight() <= 0)
        return 0;
    lastContext = info->context;
    lastWidth = bitmap->getWidth();
    lastHeight = bitmap->getHeight();
    if (!paintImage.isValid()
        || paintImage.getWidth() != lastWidth
        || paintImage.getHeight() != lastHeight)
        paintImage = juce::Image(
            juce::Image::ARGB, lastWidth, lastHeight, true);
    juce::Graphics graphics(paintImage);
    paint(graphics, paintImage.getBounds(), lastContext);

    juce::Image::BitmapData pixels(
        paintImage, juce::Image::BitmapData::readOnly);
    auto* destination = bitmap->getBits();
    const auto rowSpan = bitmap->getRowSpan();
    const auto bytesPerRow =
        static_cast<size_t>(pixels.pixelStride * pixels.width);
    for (int y = 0; y < bitmap->getHeight(); ++y)
    {
        const auto sourceY =
            bitmap->isFlipped() ? bitmap->getHeight() - 1 - y : y;
        std::memcpy(
            destination + y * rowSpan,
            pixels.getLinePointer(sourceY),
            bytesPerRow);
    }
    return 1;
}

Steinberg::TPtrInt ReaperEmbeddedUI::sizeHints(
    reaper::REAPER_FXEMBED_SizeHints* hints) const
{
    if (hints == nullptr)
        return 0;
    const auto context = hints->flags & 15;
    hints->preferred_aspect =
        context == 2 ? 32768 : 4 * 65536;
    hints->minimum_aspect =
        context == 2 ? 16384 : 65536;
    hints->min_width = context == 2 ? 54 : 120;
    hints->min_height = context == 2 ? 80 : 42;
    hints->max_width = 4096;
    hints->max_height = 4096;
    return 1;
}

void ReaperEmbeddedUI::beginPrimaryDrag(int slot, int mouseY)
{
    endPrimaryDrag();
    const auto control = primaryControl(slot);
    if (control < 0)
        return;
    auto* parameter = processor.parameters.getParameter(
        controlParameterId(slot, control));
    if (parameter == nullptr)
        return;
    processor.setSelectedTab(slot);
    dragSlot = slot;
    dragControl = control;
    dragStartY = mouseY;
    dragStartValue = parameter->getValue();
    parameter->beginChangeGesture();
}

void ReaperEmbeddedUI::updatePrimaryDrag(int mouseY)
{
    if (dragSlot < 0 || dragControl < 0)
        return;
    if (auto* parameter = processor.parameters.getParameter(
            controlParameterId(dragSlot, dragControl)))
        parameter->setValueNotifyingHost(juce::jlimit(
            0.0f, 1.0f,
            dragStartValue
                + static_cast<float>(dragStartY - mouseY) / 180.0f));
}

void ReaperEmbeddedUI::endPrimaryDrag()
{
    if (dragSlot >= 0 && dragControl >= 0)
        if (auto* parameter = processor.parameters.getParameter(
                controlParameterId(dragSlot, dragControl)))
            parameter->endChangeGesture();
    dragSlot = dragControl = -1;
}

bool ReaperEmbeddedUI::adjustPrimary(
    int slot, float normalizedDelta)
{
    const auto control = primaryControl(slot);
    if (control < 0)
        return false;
    auto* parameter = processor.parameters.getParameter(
        controlParameterId(slot, control));
    if (parameter == nullptr)
        return false;
    processor.setSelectedTab(slot);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(juce::jlimit(
        0.0f, 1.0f, parameter->getValue() + normalizedDelta));
    parameter->endChangeGesture();
    return true;
}

Steinberg::TPtrInt ReaperEmbeddedUI::mouseMessage(
    int message, const reaper::REAPER_FXEMBED_DrawInfo* info)
{
    if (info == nullptr)
        return 0;
    lastContext = info->context;
    lastWidth = info->width;
    lastHeight = info->height;
    const auto layout = createLayout(
        { 0, 0, info->width, info->height }, info->context);
    const auto point = juce::Point<int>(info->mouse_x, info->mouse_y);
    const auto slot = tileAt(point, layout);

    switch (message)
    {
        case REAPER_FXEMBED_WM_LBUTTONDOWN:
            if (slot < 0)
                return 0;
            processor.setSelectedTab(slot);
            if (bypassBounds(
                    layout.tiles[static_cast<size_t>(slot)])
                    .contains(point))
                processor.toggleSlotBypass(slot);
            else
                beginPrimaryDrag(slot, info->mouse_y);
            return invalidate;

        case REAPER_FXEMBED_WM_MOUSEMOVE:
            if (dragSlot >= 0)
            {
                updatePrimaryDrag(info->mouse_y);
                return invalidate;
            }
            return 0;

        case REAPER_FXEMBED_WM_LBUTTONUP:
            endPrimaryDrag();
            return invalidate;

        case REAPER_FXEMBED_WM_LBUTTONDBLCLK:
            if (slot >= 0)
            {
                const auto control = primaryControl(slot);
                if (control >= 0)
                {
                    if (auto* parameter =
                            processor.parameters.getParameter(
                                controlParameterId(slot, control)))
                    {
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(
                            moduleDefaults(
                                processor.getRack()
                                    .activeModuleType(slot))
                                [static_cast<size_t>(control)]);
                        parameter->endChangeGesture();
                    }
                }
                return invalidate;
            }
            return 0;

        case REAPER_FXEMBED_WM_MOUSEWHEEL:
            return slot >= 0
                    && adjustPrimary(
                        slot,
                        static_cast<float>(info->mousewheel_amt)
                            / 120.0f * 0.02f)
                ? invalidate : 0;
    }
    return 0;
}

Steinberg::TPtrInt ReaperEmbeddedUI::embed_message(
    int message, Steinberg::TPtrInt parameter2,
    Steinberg::TPtrInt parameter3)
{
    switch (message)
    {
        case REAPER_FXEMBED_WM_IS_SUPPORTED:
        case REAPER_FXEMBED_WM_NOBORDER:
            return 1;
        case REAPER_FXEMBED_WM_CREATE:
            return 1;
        case REAPER_FXEMBED_WM_DESTROY:
            endPrimaryDrag();
            return 1;
        case REAPER_FXEMBED_WM_PAINT:
            return paintToBitmap(
                reinterpret_cast<reaper::REAPER_FXEMBED_IBitmap*>(
                    parameter2),
                reinterpret_cast<reaper::REAPER_FXEMBED_DrawInfo*>(
                    parameter3));
        case REAPER_FXEMBED_WM_GETMINMAXINFO:
            return sizeHints(
                reinterpret_cast<reaper::REAPER_FXEMBED_SizeHints*>(
                    parameter3));
        case REAPER_FXEMBED_WM_HITTEST:
        {
            const auto originalMessage = static_cast<int>(parameter2);
            if (originalMessage == REAPER_FXEMBED_WM_RBUTTONDOWN
                || originalMessage == REAPER_FXEMBED_WM_RBUTTONUP
                || originalMessage == REAPER_FXEMBED_WM_RBUTTONDBLCLK)
                return 1;
            const auto* info =
                reinterpret_cast<reaper::REAPER_FXEMBED_DrawInfo*>(
                    parameter3);
            if (info == nullptr)
                return 1;
            const auto layout = createLayout(
                { 0, 0, info->width, info->height },
                info->context);
            return tileAt(
                       { info->mouse_x, info->mouse_y }, layout)
                    >= 0
                ? 0 : 1;
        }
        case REAPER_FXEMBED_WM_MOUSEMOVE:
        case REAPER_FXEMBED_WM_LBUTTONDOWN:
        case REAPER_FXEMBED_WM_LBUTTONUP:
        case REAPER_FXEMBED_WM_LBUTTONDBLCLK:
        case REAPER_FXEMBED_WM_MOUSEWHEEL:
            return mouseMessage(
                message,
                reinterpret_cast<reaper::REAPER_FXEMBED_DrawInfo*>(
                    parameter3));
        case REAPER_FXEMBED_WM_SETCURSOR:
        case REAPER_FXEMBED_WM_RBUTTONDOWN:
        case REAPER_FXEMBED_WM_RBUTTONUP:
        case REAPER_FXEMBED_WM_RBUTTONDBLCLK:
            return 0;
    }
    return 0;
}

#if defined(MEGADSP_TESTS)
juce::Image ReaperEmbeddedUI::renderForTesting(
    int width, int height, int context)
{
    juce::Image image(
        juce::Image::ARGB, width, height, true);
    juce::Graphics graphics(image);
    paint(graphics, image.getBounds(), context);
    return image;
}

std::array<juce::Rectangle<int>, slotCount>
ReaperEmbeddedUI::tileBoundsForTesting(
    int width, int height, int context) const
{
    return createLayout({ 0, 0, width, height }, context).tiles;
}

int ReaperEmbeddedUI::primaryControlForTesting(int slot) const
{
    return primaryControl(slot);
}

bool ReaperEmbeddedUI::dragPrimaryForTesting(
    int slot, int verticalDistance)
{
    const auto control = primaryControl(slot);
    if (control < 0)
        return false;
    auto* parameter = processor.parameters.getParameter(
        controlParameterId(slot, control));
    if (parameter == nullptr)
        return false;
    parameter->setValueNotifyingHost(0.5f);
    beginPrimaryDrag(slot, 100);
    updatePrimaryDrag(100 + verticalDistance);
    endPrimaryDrag();
    return verticalDistance < 0
        ? parameter->getValue() > 0.5f
        : parameter->getValue() < 0.5f;
}
#endif
} // namespace megadsp
