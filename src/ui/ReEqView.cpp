// Native ReEQ2 UI: (c) 2021-2026 Justin Johnson; (c) 2025-2026 Yuriy Ivantsov.
// MIT; see src/third_party/reeq2/LICENSE and THIRD_PARTY_NOTICES.md.
// ReJJ reference revision 281bf08f9d0b5e25a0c7db05d37b7a2f3afa6b41.
#include "ReEqView.h"

#include "../PluginProcessor.h"
#include "../modules/Equalizer.h"
#include "../modules/ReEqProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace megadsp::ui
{
namespace
{
constexpr float minimumFrequency = 10.0f;
constexpr float maximumFrequency = 30000.0f;
constexpr int spectrumPoints = 480;
constexpr int maximumFftSize = 16384;
constexpr std::array<int, 10> slopes{6, 12, 18, 24, 30, 36, 48, 72, 96, 120};
const juce::Colour background{0xff101416}, panel{0xff191f22};
const juce::Colour foreground{0xffdfe6e9}, muted{0xff89979f};
const juce::Colour accent{0xff5cc8ea};

juce::String frequencyText(float value)
{
    return value >= 1000.0f ? juce::String(value * 0.001f, 2) + " kHz"
                            : juce::String(value, value < 100.0f ? 1 : 0) + " Hz";
}

float parseNumber(juce::String text)
{
    text = text.trim().toLowerCase().replaceCharacter(juce::juce_wchar(0x2212), '-');
    const auto value = text.getFloatValue();
    return text.contains("k") ? value * 1000.0f : value;
}

int closestSlope(int slope)
{
    const auto db = reEqSlopeDb(slope);
    auto best = 0;
    for (int i = 1; i < static_cast<int>(slopes.size()); ++i)
        if (std::abs(slopes[static_cast<size_t>(i)] - db) <
            std::abs(slopes[static_cast<size_t>(best)] - db))
            best = i;
    return best;
}

float safeDb(double magnitude)
{
    return static_cast<float>(
        20.0 * std::log10(std::isfinite(magnitude) ? std::max(1.0e-12, magnitude) : 1.0));
}
bool dynamicShape(ReEqShape shape)
{
    return shape == ReEqShape::bell || shape == ReEqShape::lowShelf ||
           shape == ReEqShape::highShelf;
}

bool hasGain(ReEqShape shape)
{
    return shape == ReEqShape::bell || shape == ReEqShape::lowShelf ||
           shape == ReEqShape::highShelf || shape == ReEqShape::tiltShelf ||
           shape == ReEqShape::pultecLow || shape == ReEqShape::vintageLow ||
           shape == ReEqShape::vintageHigh;
}

bool hasQ(const ReEqBand& band)
{
    return band.shape != ReEqShape::lowCutButterworth &&
           band.shape != ReEqShape::highCutButterworth &&
           !((band.shape == ReEqShape::lowCut || band.shape == ReEqShape::highCut) &&
             band.slope == 0);
}

float nodeGain(const ReEqBand& band)
{
    if (band.shape == ReEqShape::lowCut || band.shape == ReEqShape::highCut)
    {
        const auto stages = (band.slope + 1) / 2;
        return stages == 0 ? 0.0f : static_cast<float>(stages) * (6.0f * std::log2(band.q) + 3.0f);
    }
    return hasGain(band.shape) ? band.gainDb : 0.0f;
}

void setNodeGain(ReEqBand& band, float gain)
{
    if (band.shape == ReEqShape::lowCut || band.shape == ReEqShape::highCut)
    {
        const auto stages = (band.slope + 1) / 2;
        if (stages > 0)
            band.q = juce::jlimit(
                0.1f, 40.0f, std::pow(2.0f, (gain / static_cast<float>(stages) - 3.0f) / 6.0f));
    }
    else if (hasGain(band.shape))
        band.gainDb = juce::jlimit(-18.0f, 18.0f, gain);
}
} // namespace

// Editable numbers, not rotary controls. Double-click types; vertical drag and
// arrow keys adjust in the displayed musical domain, with Shift for fine moves.
class ReEqView::NumericField final : public juce::Label
{
  public:
    NumericField()
    {
        setEditable(false, true, false);
        setJustificationType(juce::Justification::centred);
        setWantsKeyboardFocus(true);
        setColour(textColourId, foreground);
        setColour(backgroundColourId, panel);
        setColour(outlineColourId, juce::Colour(0xff303c42));
        setFont(juce::Font(juce::FontOptions(11.0f)));
        setMinimumHorizontalScale(0.65f);
        onEditorShow = [this]
        {
            if (auto* editor = getCurrentTextEditor())
            {
                editor->setText(juce::String(value, 3));
                editor->selectAll();
            }
        };
        onTextChange = [this]
        {
            if (updating)
                return;
            const auto next = parseNumber(getText());
            if (std::isfinite(next))
            {
                if (onBegin)
                    onBegin();
                assign(next, true);
                if (onEnd)
                    onEnd();
            }
            updateText();
        };
    }

    void configure(juce::String captionIn, float minimumIn, float maximumIn, float defaultIn,
                   bool logarithmicIn, std::function<juce::String(float)> formatIn)
    {
        caption = std::move(captionIn);
        minimum = minimumIn;
        maximum = maximumIn;
        defaultValue = defaultIn;
        logarithmic = logarithmicIn;
        format = std::move(formatIn);
        setTitle(caption);
        setDescription(caption + ". Drag up to increase; double-click to type. "
                                 "Arrow keys adjust; Home resets; Shift adjusts finely.");
    }

    void assign(float next, bool notify = false)
    {
        next = juce::jlimit(minimum, maximum, next);
        const auto changed = !juce::exactlyEqual(value, next);
        value = next;
        if (!isBeingEdited())
            updateText();
        if (notify && changed && onChange)
        {
            const auto callback = onChange;
            callback(next);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::Label::mouseDown(event);
        if (event.mods.isPopupMenu() || isBeingEdited())
            return;
        origin = value;
        dragging = false;
        if (onBegin)
            onBegin();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (isBeingEdited() || !event.mods.isLeftButtonDown())
            return;
        dragging = true;
        auto distance = static_cast<float>(-event.getDistanceFromDragStartY());
        if (event.mods.isShiftDown() || event.mods.isAltDown())
            distance *= 0.15f;
        assign(logarithmic ? origin * std::pow(2.0f, distance / 80.0f)
                           : origin + distance * (maximum - minimum) / 300.0f,
               true);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::Label::mouseUp(event);
        if (onEnd)
            onEnd();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (isBeingEdited())
            return;
        if (onBegin)
            onBegin();
        assign(logarithmic ? value * std::pow(2.0f, wheel.deltaY)
                           : value + wheel.deltaY * (maximum - minimum) / 20.0f,
               true);
        if (onEnd)
            onEnd();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        if (code != juce::KeyPress::upKey && code != juce::KeyPress::downKey &&
            code != juce::KeyPress::homeKey)
            return juce::Label::keyPressed(key);
        if (onBegin)
            onBegin();
        const auto direction = code == juce::KeyPress::upKey ? 1.0f : -1.0f;
        const auto step = key.getModifiers().isShiftDown() ? 0.001f : 0.01f;
        assign(code == juce::KeyPress::homeKey ? defaultValue
               : logarithmic                   ? value * std::pow(2.0f, direction * step * 4.0f)
                                               : value + direction * step * (maximum - minimum),
               true);
        if (onEnd)
            onEnd();
        return true;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (onEnd)
            onEnd();
        if (event.mods.isCommandDown())
        {
            if (onBegin)
                onBegin();
            assign(defaultValue, true);
            if (onEnd)
                onEnd();
        }
        else
            juce::Label::mouseDoubleClick(event);
    }

    std::function<void(float)> onChange;
    std::function<void()> onBegin, onEnd;

  private:
    void updateText()
    {
        const juce::ScopedValueSetter<bool> guard(updating, true);
        setText(caption + " " + (format ? format(value) : juce::String(value, 1)),
                juce::dontSendNotification);
    }

    juce::String caption;
    std::function<juce::String(float)> format;
    float value = 0.0f, origin = 0.0f, minimum = 0.0f, maximum = 1.0f;
    float defaultValue = 0.0f;
    bool logarithmic = false, updating = false, dragging = false;
};

struct ReEqView::Analyzer
{
    struct Transform
    {
        explicit Transform(int order) : fft(order), size(1 << order)
        {
            for (int type = 0; type < 4; ++type)
            {
                auto& window = windows[static_cast<size_t>(type)];
                window.resize(static_cast<size_t>(size), 1.0f);
                const std::array<juce::dsp::WindowingFunction<float>::WindowingMethod, 4> methods{
                    juce::dsp::WindowingFunction<float>::hamming,
                    juce::dsp::WindowingFunction<float>::blackmanHarris,
                    juce::dsp::WindowingFunction<float>::blackman,
                    juce::dsp::WindowingFunction<float>::rectangular};
                juce::dsp::WindowingFunction<float>::fillWindowingTables(
                    window.data(), window.size(), methods[static_cast<size_t>(type)], true);
            }
        }
        juce::dsp::FFT fft;
        int size;
        std::array<std::vector<float>, 4> windows;
    };

    Analyzer()
    {
        for (int i = 0; i < 4; ++i)
            transforms[static_cast<size_t>(i)] = std::make_unique<Transform>(11 + i);
        for (auto& values : spectrum)
            values.fill(-200.0f);
        for (auto& values : peaks)
            values.fill(-200.0f);
    }

    std::array<std::unique_ptr<Transform>, 4> transforms;
    std::array<std::array<float, maximumFftSize>, 4> history{};
    std::array<std::array<float, maximumFftSize>, 4> capture{};
    std::array<float, maximumFftSize * 2> work{};
    std::array<std::array<float, spectrumPoints>, 4> spectrum, peaks;
    uint64_t position = 0;
    int write = 0, available = 0;
    bool valid = false;
    float inputPeak = -100.0f, outputPeak = -100.0f;
    std::array<float, reEqBandCount> dynamicGain{};
    std::array<float, reEqBandCount> detectorLevel{};
};

ReEqView::ReEqView(MegaDSPAudioProcessor& owner, int slotIndex)
    : processor(owner), slot(slotIndex), analyzer(std::make_unique<Analyzer>())
{
    setWantsKeyboardFocus(true);
    setTitle("ReEQ2 equalizer");
    setDescription("Sixteen-band equalizer. Double-click empty graph to add a band; "
                   "double-click a band to bypass; Alt-double-click deletes. "
                   "Drag frequency and gain, wheel Q, Command-drag Q, "
                   "Command-wheel slope; Shift auditions bands or blank-graph frequencies. "
                   "Shift-wheel on blank graph sets audition width; Shift-drag sets level. "
                   "Alt moves finely.");
    settings = processor.reEqSettings(slot);
    for (auto& field : globalFields)
    {
        field = std::make_unique<NumericField>();
        addAndMakeVisible(*field);
        field->onBegin = [this] { beginGesture("ReEQ2 level"); };
        field->onEnd = [this] { endGesture(); };
    }
    const auto db = [](float v) { return juce::String(v, 1) + " dB"; };
    globalFields[0]->configure("IN", -136.0f, 30.0f, 0.0f, false, db);
    globalFields[1]->configure("OUT", -154.0f, 48.0f, 0.0f, false, db);
    globalFields[1]->setTooltip(
        "Total output trim: native OUT plus retained host Output "
        "automation. Editing changes only native OUT; host automation is preserved.");
    globalFields[2]->configure("Scale", 0.0f, 200.0f, 100.0f, false,
                               [](float v) { return juce::String(v, 0) + "%"; });
    globalFields[0]->onChange = [this](float v)
    {
        settings.inputDb = v;
        publish();
    };
    globalFields[1]->onChange = [this](float v)
    {
        settings.outputDb = v - retainedOutputDb();
        publish();
    };
    globalFields[2]->onChange = [this](float v)
    {
        settings.scale = v * 0.01f;
        publish();
    };

    for (auto& field : bandFields)
    {
        field = std::make_unique<NumericField>();
        addAndMakeVisible(*field);
        field->onBegin = [this] { beginGesture("ReEQ2 band control"); };
        field->onEnd = [this] { endGesture(); };
    }
    for (auto* button :
         {&autoGain, &quality, &limit, &analyzerButton, &settingsButton, &actions, &dynamicButton})
    {
        addAndMakeVisible(*button);
        button->setColour(juce::TextButton::buttonColourId, panel);
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff194b5b));
        button->setColour(juce::TextButton::textColourOffId, foreground);
        button->setColour(juce::TextButton::textColourOnId, accent);
    }
    autoGain.setTooltip("Automatic gain compensation");
    quality.setTooltip("High-quality processing");
    limit.setTooltip("Limit output");
    autoGain.onClick = [this]
    { apply("ReEQ2 auto gain", [](auto& s) { s.autoGain = !s.autoGain; }); };
    quality.setButtonText("IIR");
    quality.setEnabled(false);
    quality.setTooltip("ReEQ2's retained HQ flag: the upstream digital filters no longer "
                       "use Eco/HQ oversampling. Decramped digital IIR filters are always used.");
    limit.onClick = [this]
    { apply("ReEQ2 limit", [](auto& s) { s.limitOutput = !s.limitOutput; }); };
    analyzerButton.onClick = [this] { showAnalyzerMenu(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    actions.onClick = [this] { showBandMenu(); };
    dynamicButton.setTooltip("Enable dynamic processing for the selected bands");
    dynamicButton.onClick = [this]
    {
        if (!hasSelection())
            return;
        const auto next = !settings.bands[static_cast<size_t>(selectedBand)].dynamic;
        applySelected("ReEQ2 dynamics",
                      [next](auto& band)
                      {
                          if (dynamicShape(band.shape))
                              band.dynamic = next;
                      });
    };
    for (auto* combo : {&bandChoice, &shapeChoice, &groupChoice, &slopeChoice, &pageChoice})
    {
        addAndMakeVisible(*combo);
        combo->setColour(juce::ComboBox::backgroundColourId, panel);
        combo->setColour(juce::ComboBox::textColourId, foreground);
        combo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff303c42));
    }
    initializeChoices();
    syncControls();
}

void ReEqView::updateAnalyzer()
{
    auto& data = processor.getVisualizationData().slotData(slot);
    auto& state = *analyzer;
    const auto position = data.outputLeft.position();
    if (position == state.position && state.valid)
        return;
    if (data.inputLeft.position() != position
        || data.inputRight.position() != position
        || data.outputRight.position() != position)
        return;
    const auto count = position >= state.position
                           ? std::min<uint64_t>(position - state.position, maximumFftSize)
                           : std::min<uint64_t>(position, maximumFftSize);
    if (position < state.position)
    {
        state.available = 0;
        state.write = 0;
    }
    const auto inputLeftCount = data.inputLeft.copyLatest(state.capture[0]);
    const auto inputRightCount = data.inputRight.copyLatest(state.capture[1]);
    const auto outputLeftCount = data.outputLeft.copyLatest(state.capture[2]);
    const auto outputRightCount = data.outputRight.copyLatest(state.capture[3]);
    if ((position != 0 && outputLeftCount == 0)
        || inputLeftCount != outputLeftCount || inputRightCount != outputLeftCount
        || outputRightCount != outputLeftCount
        || data.inputLeft.position() != position
        || data.inputRight.position() != position
        || data.outputLeft.position() != position
        || data.outputRight.position() != position)
        return;
    state.position = position;
    float inputPeak = 0.0f, outputPeak = 0.0f;
    for (size_t sample = maximumFftSize - static_cast<size_t>(count); sample < maximumFftSize;
         ++sample)
    {
        for (size_t channel = 0; channel < state.history.size(); ++channel)
        {
            const auto v = state.capture[channel][sample];
            state.history[channel][static_cast<size_t>(state.write)] = std::isfinite(v) ? v : 0.0f;
        }
        inputPeak = std::max(inputPeak, std::max(std::abs(state.capture[0][sample]),
                                                 std::abs(state.capture[1][sample])));
        outputPeak = std::max(outputPeak, std::max(std::abs(state.capture[2][sample]),
                                                   std::abs(state.capture[3][sample])));
        state.write = (state.write + 1) % maximumFftSize;
    }
    state.available = std::min(maximumFftSize, state.available + static_cast<int>(count));
    state.inputPeak = std::max(safeDb(inputPeak), state.inputPeak - 0.8f);
    state.outputPeak = std::max(safeDb(outputPeak), state.outputPeak - 0.8f);
    auto& transform = *state.transforms[static_cast<size_t>(settings.fftOrder - 11)];
    const auto n = transform.size;
    const auto& window = transform.windows[static_cast<size_t>(settings.windowType)];
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto channels = settings.spectrumMode == 3 || settings.spectrumMode == 6 ? 2 : 1;
    for (int trace = 0; trace < (settings.showPreEq ? 4 : channels); ++trace)
    {
        if (trace % 2 == 1 && channels == 1)
            continue;
        const auto pre = trace >= 2;
        const auto first = pre ? 0u : 2u;
        std::fill(state.work.begin(), state.work.end(), 0.0f);
        for (int sample = 0; sample < n; ++sample)
        {
            if (sample < n - state.available)
                continue;
            const auto index =
                static_cast<size_t>((state.write - n + sample + maximumFftSize) % maximumFftSize);
            const auto left = state.history[first][index];
            const auto right = state.history[first + 1][index];
            auto value = 0.5f * (left + right);
            switch (settings.spectrumMode)
            {
            case 2:
                value = 0.5f * (left - right);
                break;
            case 3:
                value = trace % 2 == 1 ? 0.5f * (left - right) : value;
                break;
            case 4:
                value = left;
                break;
            case 5:
                value = right;
                break;
            case 6:
                value = trace % 2 == 1 ? right : left;
                break;
            default:
                break;
            }
            // Full is an energy spectrum rather than a mono fold-down: analyze
            // L and R separately below so anti-phase material remains visible.
            if (settings.spectrumMode == 0)
                value = left;
            state.work[static_cast<size_t>(sample)] = value * window[static_cast<size_t>(sample)];
        }
        transform.fft.performFrequencyOnlyForwardTransform(state.work.data(), true);
        std::array<float, maximumFftSize / 2 + 1> fullLeft{};
        if (settings.spectrumMode == 0)
        {
            std::copy_n(state.work.begin(), n / 2 + 1, fullLeft.begin());
            std::fill(state.work.begin(), state.work.end(), 0.0f);
            for (int sample = std::max(0, n - state.available); sample < n; ++sample)
            {
                const auto index = static_cast<size_t>((state.write - n + sample + maximumFftSize) %
                                                       maximumFftSize);
                state.work[static_cast<size_t>(sample)] =
                    state.history[first + 1][index] * window[static_cast<size_t>(sample)];
            }
            transform.fft.performFrequencyOnlyForwardTransform(state.work.data(), true);
            for (int bin = 0; bin <= n / 2; ++bin)
            {
                const auto l = fullLeft[static_cast<size_t>(bin)];
                const auto r = state.work[static_cast<size_t>(bin)];
                state.work[static_cast<size_t>(bin)] = std::sqrt(0.5f * (l * l + r * r));
            }
        }
        const auto octaveWidth = settings.smoothing == 0
                                     ? 0.0f
                                     : 1.0f / static_cast<float>(48 >> (settings.smoothing - 1));
        for (int point = 0; point < spectrumPoints; ++point)
        {
            const auto frequency =
                minimumFrequency *
                std::pow(maximumFrequency / minimumFrequency,
                         static_cast<float>(point) / static_cast<float>(spectrumPoints - 1));
            const auto bin = frequency * static_cast<float>(n / sampleRate);
            auto magnitude = 0.0f;
            if (bin <= static_cast<float>(n / 2))
            {
                const auto lower = juce::jlimit(
                    0, n / 2, static_cast<int>(bin * std::pow(2.0f, -octaveWidth * 0.5f)));
                const auto upper = juce::jlimit(
                    lower, n / 2, static_cast<int>(bin * std::pow(2.0f, octaveWidth * 0.5f)));
                if (upper <= lower)
                {
                    const auto nearest = juce::jlimit(0, n / 2, static_cast<int>(bin));
                    const auto next = std::min(n / 2, nearest + 1);
                    const auto fraction = bin - static_cast<float>(nearest);
                    magnitude = state.work[static_cast<size_t>(nearest)] * (1.0f - fraction) +
                                state.work[static_cast<size_t>(next)] * fraction;
                }
                else
                {
                    double energy = 0.0;
                    for (int i = lower; i <= upper; ++i)
                    {
                        const auto v = state.work[static_cast<size_t>(i)];
                        energy += static_cast<double>(v) * v;
                    }
                    magnitude = static_cast<float>(std::sqrt(energy / (upper - lower + 1)));
                }
            }
            const auto db = safeDb(magnitude * 2.0f / static_cast<float>(n)) +
                            settings.tilt * std::log2(frequency / 1000.0f);
            auto& current = state.spectrum[static_cast<size_t>(trace)][static_cast<size_t>(point)];
            current = !state.valid ? db : std::max(db, current - 1.2f);
            auto& peak = state.peaks[static_cast<size_t>(trace)][static_cast<size_t>(point)];
            peak = !state.valid ? current : std::max(current, peak - 0.08f);
        }
    }
    state.valid = count > 0 || state.valid;
}

void ReEqView::paintAnalyzer(juce::Graphics& g)
{
    if (!active || !analyzer->valid || settings.displayMode == 2)
        return;
    const auto dual = settings.spectrumMode == 3 || settings.spectrumMode == 6;
    const auto yForDb = [this](float db)
    {
        return plot.getBottom() -
               plot.getHeight() *
                   juce::jlimit(0.0f, 1.0f,
                                (db - static_cast<float>(settings.floorDb)) /
                                    static_cast<float>(settings.ceilingDb - settings.floorDb));
    };
    for (int trace : {2, 3, 0, 1})
    {
        if ((trace >= 2 && !settings.showPreEq) || (trace % 2 == 1 && !dual))
            continue;
        const auto colour = trace >= 2   ? (trace == 2 ? muted : juce::Colour(0xff85798d))
                            : trace == 1 ? juce::Colour(0xffc48aba)
                                         : accent;
        juce::Path line, fill, peak;
        fill.startNewSubPath(plot.getX(), plot.getBottom());
        for (int i = 0; i < spectrumPoints; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * static_cast<float>(i) /
                                             static_cast<float>(spectrumPoints - 1);
            const auto db = analyzer->spectrum[static_cast<size_t>(trace)][static_cast<size_t>(i)];
            const auto y = yForDb(db);
            if (i == 0)
            {
                line.startNewSubPath(x, y);
                peak.startNewSubPath(x, yForDb(analyzer->peaks[static_cast<size_t>(trace)][0]));
            }
            else
            {
                line.lineTo(x, y);
                peak.lineTo(
                    x, yForDb(analyzer->peaks[static_cast<size_t>(trace)][static_cast<size_t>(i)]));
            }
            fill.lineTo(x, y);
            if (settings.displayMode == 3 && trace < 2)
            {
                const auto level =
                    juce::jlimit(0.0f, 1.0f,
                                 (db - static_cast<float>(settings.floorDb)) /
                                     static_cast<float>(settings.ceilingDb - settings.floorDb));
                g.setColour(juce::Colour(0xff5c3456)
                                .interpolatedWith(juce::Colour(0xfff05662), level)
                                .withAlpha(0.2f + level * 0.55f));
                g.fillRect(x, y, plot.getWidth() / static_cast<float>(spectrumPoints) + 1.0f,
                           plot.getBottom() - y);
            }
        }
        fill.lineTo(plot.getRight(), plot.getBottom());
        fill.closeSubPath();
        if (settings.displayMode == 0 && trace < 2)
        {
            g.setColour(colour.withAlpha(0.13f));
            g.fillPath(fill);
        }
        g.setColour(colour.withAlpha(trace >= 2 ? 0.4f : 0.6f));
        g.strokePath(line, juce::PathStrokeType(1.0f));
        if (settings.showPeaks && trace < 2)
        {
            g.setColour(colour.withAlpha(0.25f));
            g.strokePath(peak, juce::PathStrokeType(1.0f));
        }
    }
}

void ReEqView::select(int band, bool additive)
{
    if (!additive)
        selected.reset();
    if (juce::isPositiveAndBelow(band, reEqBandCount) &&
        settings.bands[static_cast<size_t>(band)].enabled != 0)
    {
        selected.set(static_cast<size_t>(band));
        selectedBand = band;
    }
    else
        selectedBand = -1;
    syncControls();
    repaint();
}

void ReEqView::createBand(juce::Point<float> point)
{
    int empty = -1;
    for (int i = 0; i < reEqBandCount; ++i)
        if (settings.bands[static_cast<size_t>(i)].enabled == 0)
        {
            empty = i;
            break;
        }
    if (empty < 0)
        return;
    const auto group = hasSelection() ? settings.bands[static_cast<size_t>(selectedBand)].group
                                      : ReEqGroup::stereo;
    apply("Add ReEQ2 band",
          [this, empty, point, group](auto& state)
          {
              auto& band = state.bands[static_cast<size_t>(empty)];
              band = {};
              band.enabled = 2;
              band.autoTiming = true;
              band.group = group;
              band.slope = 1;
              band.frequency = frequencyForX(point.x);
              band.gainDb = juce::jlimit(-18.0f, 18.0f, gainForY(point.y));
              if (band.frequency < 20.0f)
              {
                  band.shape = ReEqShape::lowCut;
                  band.gainDb = 0.0f;
              }
              else if (band.frequency < 30.0f)
              {
                  band.shape = ReEqShape::lowShelf;
                  band.q = 0.36f;
              }
              else if (band.frequency >= 15000.0f)
              {
                  band.shape = ReEqShape::highCut;
                  band.gainDb = 0.0f;
              }
              else if (band.frequency >= 10000.0f)
              {
                  band.shape = ReEqShape::highShelf;
                  band.q = 0.36f;
              }
              setNodeGain(band, gainForY(point.y));
          });
    select(empty, false);
}

void ReEqView::deleteSelection()
{
    stopListen();
    applySelected("Delete ReEQ2 bands", [](auto& b) { b.enabled = 0; });
    selected.reset();
    selectedBand = -1;
    syncControls();
    repaint();
}

void ReEqView::duplicateSelection(bool split)
{
    if (!hasSelection())
        return;
    const auto original = selected;
    std::bitset<reEqBandCount> copies;
    apply(split ? "Split ReEQ2 bands to Mid/Side" : "Duplicate ReEQ2 bands",
          [original, &copies, split](auto& state)
          {
              for (int i = 0; i < reEqBandCount; ++i)
              {
                  if (!original[static_cast<size_t>(i)] ||
                      state.bands[static_cast<size_t>(i)].enabled == 0)
                      continue;
                  for (int target = 0; target < reEqBandCount; ++target)
                  {
                      if (state.bands[static_cast<size_t>(target)].enabled != 0)
                          continue;
                      state.bands[static_cast<size_t>(target)] =
                          state.bands[static_cast<size_t>(i)];
                      if (split)
                      {
                          state.bands[static_cast<size_t>(target)].group = ReEqGroup::mid;
                          state.bands[static_cast<size_t>(i)].group = ReEqGroup::side;
                      }
                      copies.set(static_cast<size_t>(target));
                      break;
                  }
              }
          });
    if (copies.any())
    {
        selected = split ? (original | copies) : copies;
        for (int i = 0; i < reEqBandCount; ++i)
            if (copies[static_cast<size_t>(i)])
            {
                selectedBand = i;
                break;
            }
        syncControls();
        repaint();
    }
}

void ReEqView::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    lastMouse = event.position;
    if (!plot.contains(event.position))
        return;
    const auto band = bandAt(event.position);
    if (event.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            if (!selected[static_cast<size_t>(band)])
                select(band, event.mods.isCommandDown());
            selectedBand = band;
            showBandMenu();
        }
        else
            showSettingsMenu();
        return;
    }
    if (event.mods.isShiftDown())
    {
        updateListen(event.position, event.mods);
        return;
    }
    selectionBeforeBox = selected;
    dragOrigin = event.position;
    if (band >= 0)
    {
        if (!selected[static_cast<size_t>(band)])
            select(band, event.mods.isCommandDown());
        selectedBand = band;
        beginGesture("Move ReEQ2 bands");
        dragStart = settings;
        dragBand = band;
        drag = Drag::nodes;
        syncControls();
        return;
    }
    const auto detector = detectorAt(event.position);
    if (detector >= 0)
    {
        beginGesture("ReEQ2 detector frequency");
        dragStart = settings;
        dragBand = detector;
        drag = Drag::detector;
        return;
    }
    if (hasSelection())
    {
        const auto& b = settings.bands[static_cast<size_t>(selectedBand)];
        const auto rangeY = juce::jlimit(plot.getY() + 5.0f, plot.getBottom() - 5.0f,
                                         yForGain(b.gainDb + b.rangeDb));
        if (b.dynamic && std::abs(event.position.x - bandPoint(selectedBand).x) < 10.0f &&
            std::abs(event.position.y - rangeY) < 8.0f)
        {
            beginGesture("ReEQ2 dynamics range");
            dragStart = settings;
            dragBand = selectedBand;
            drag = Drag::range;
            return;
        }
    }
    drag = Drag::box;
    if (!event.mods.isCommandDown())
        select(-1, false);
    selectionBox = {event.position, event.position};
}

void ReEqView::mouseDrag(const juce::MouseEvent& event)
{
    lastMouse = event.position;
    if (event.mods.isShiftDown())
    {
        updateListen(event.position, event.mods);
        return;
    }
    if (drag == Drag::box)
    {
        selectionBox = juce::Rectangle<float>(dragOrigin, event.position).getIntersection(plot);
        selected = event.mods.isCommandDown() ? selectionBeforeBox : std::bitset<reEqBandCount>{};
        selectedBand = -1;
        for (int i = 0; i < reEqBandCount; ++i)
            if (settings.bands[static_cast<size_t>(i)].enabled != 0 &&
                selectionBox.contains(bandPoint(i)))
            {
                selected.set(static_cast<size_t>(i));
                selectedBand = i;
            }
        if (selectedBand < 0)
            for (int i = 0; i < reEqBandCount; ++i)
                if (selected[static_cast<size_t>(i)])
                {
                    selectedBand = i;
                    break;
                }
        syncControls();
        repaint();
        return;
    }
    if (drag == Drag::none || dragBand < 0)
        return;
    auto delta = event.position - dragOrigin;
    if (event.mods.isAltDown())
        delta *= 1.0f / 6.0f;
    if (drag == Drag::detector)
    {
        auto& band = settings.bands[static_cast<size_t>(dragBand)];
        const auto initial = dragStart.bands[static_cast<size_t>(dragBand)].detectorFrequency;
        band.detectorFrequency = frequencyForX(xForFrequency(initial) + delta.x);
    }
    else if (drag == Drag::range)
    {
        auto& band = settings.bands[static_cast<size_t>(dragBand)];
        band.rangeDb = juce::jlimit(-24.0f, 24.0f,
                                    dragStart.bands[static_cast<size_t>(dragBand)].rangeDb +
                                        gainForY(dragOrigin.y + delta.y) - gainForY(dragOrigin.y));
    }
    else
    {
        const auto ratio = std::pow(maximumFrequency / minimumFrequency,
                                    delta.x / std::max(1.0f, plot.getWidth()));
        const auto db = -delta.y * (2.0f * static_cast<float>(settings.rangeDb)) /
                        std::max(1.0f, plot.getHeight());
        for (int i = 0; i < reEqBandCount; ++i)
        {
            if (!selected[static_cast<size_t>(i)])
                continue;
            auto& band = settings.bands[static_cast<size_t>(i)];
            const auto& original = dragStart.bands[static_cast<size_t>(i)];
            band.frequency =
                juce::jlimit(minimumFrequency, maximumFrequency, original.frequency * ratio);
            if (event.mods.isCommandDown() && hasQ(band))
                band.q = juce::jlimit(0.1f, 40.0f, original.q * std::pow(2.0f, -delta.y / 60.0f));
            else if (!event.mods.isCommandDown())
                setNodeGain(band, nodeGain(original) + db);
        }
    }
    publish();
}

void ReEqView::mouseUp(const juce::MouseEvent& event)
{
    if (drag == Drag::nodes && event.getDistanceFromDragStart() < 3 && event.mods.isCommandDown() &&
        dragBand >= 0 && selectionBeforeBox[static_cast<size_t>(dragBand)])
    {
        selected.reset(static_cast<size_t>(dragBand));
        selectedBand = -1;
        for (int i = 0; i < reEqBandCount; ++i)
            if (selected[static_cast<size_t>(i)])
            {
                selectedBand = i;
                break;
            }
    }
    drag = Drag::none;
    dragBand = -1;
    selectionBox = {};
    endGesture();
    syncControls();
    if (!event.mods.isShiftDown())
        stopListen();
    else
        updateListen(event.position, event.mods);
    repaint();
}

void ReEqView::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!plot.contains(event.position) || event.mods.isPopupMenu())
        return;
    endGesture();
    drag = Drag::none;
    const auto band = bandAt(event.position);
    if (band < 0)
    {
        if (!event.mods.isAltDown())
            createBand(event.position);
        return;
    }
    if (!selected[static_cast<size_t>(band)])
        select(band, false);
    if (event.mods.isAltDown())
    {
        stopListen();
        apply("Delete ReEQ2 band",
              [band](auto& state) { state.bands[static_cast<size_t>(band)].enabled = 0; });
        selected.reset(static_cast<size_t>(band));
        selectedBand = -1;
        for (int i = 0; i < reEqBandCount; ++i)
            if (selected[static_cast<size_t>(i)])
            {
                selectedBand = i;
                break;
            }
        syncControls();
    }
    else
    {
        const auto next = settings.bands[static_cast<size_t>(band)].enabled == 2 ? 1 : 2;
        apply("ReEQ2 band bypass",
              [band, next](auto& state) { state.bands[static_cast<size_t>(band)].enabled = next; });
    }
}

void ReEqView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!plot.contains(event.position) || wheel.deltaY == 0.0f)
        return;
    const auto detector = detectorAt(event.position);
    if (detector >= 0 && bandAt(event.position) < 0)
    {
        apply("ReEQ2 detector width",
              [detector, &wheel](auto& state)
              {
                  auto& b = state.bands[static_cast<size_t>(detector)];
                  b.detectorWidth =
                      juce::jlimit(0.25f, 8.0f, b.detectorWidth * std::pow(2.0f, wheel.deltaY));
              });
        return;
    }
    auto band = bandAt(event.position);
    if (band < 0)
    {
        if (event.mods.isShiftDown() && active)
        {
            updateListen(event.position, event.mods);
            const auto octaves = std::log2(maximumFrequency / minimumFrequency);
            settings.listenWidthOctaves =
                juce::jlimit(0.02f * octaves, 0.6f * octaves,
                             settings.listenWidthOctaves + wheel.deltaY * octaves * 0.1f);
            processor.setReEqSettings(slot, settings, false);
            repaint();
        }
        return;
    }
    const auto targets = selected[static_cast<size_t>(band)]
                             ? selected
                             : std::bitset<reEqBandCount>().set(static_cast<size_t>(band));
    apply(event.mods.isCommandDown() ? "ReEQ2 filter slope" : "ReEQ2 Q",
          [targets, &event, &wheel](auto& state)
          {
              for (int i = 0; i < reEqBandCount; ++i)
              {
                  if (!targets[static_cast<size_t>(i)])
                      continue;
                  auto& b = state.bands[static_cast<size_t>(i)];
                  if (event.mods.isCommandDown() && isCut(b.shape))
                  {
                      const auto index =
                          juce::jlimit(0, static_cast<int>(slopes.size()) - 1,
                                       closestSlope(b.slope) + (wheel.deltaY > 0.0f ? 1 : -1));
                      b.slope = slopes[static_cast<size_t>(index)] / 6 - 1;
                  }
                  else if (!event.mods.isCommandDown() && hasQ(b))
                      b.q = juce::jlimit(
                          0.1f, 40.0f,
                          b.q * std::pow(2.0f,
                                         wheel.deltaY * (event.mods.isAltDown() ? 0.25f : 1.5f)));
              }
          });
}

void ReEqView::setListen(int band, bool detector)
{
    if (!active || !juce::isPositiveAndBelow(band, reEqBandCount))
        return;
    if (!gesture)
        settings = processor.reEqSettings(slot);
    const auto group = settings.bands[static_cast<size_t>(band)].group;
    if (settings.listenBand == band && settings.listenDetector == detector &&
        settings.listenGroup == group && juce::exactlyEqual(settings.listenGainDb, 0.0f))
        return;
    settings.listenBand = band;
    settings.listenDetector = detector;
    settings.listenGroup = group;
    settings.listenGainDb = 0.0f;
    processor.setReEqSettings(slot, settings, false);
    repaint();
}

void ReEqView::stopListen()
{
    if (!gesture)
        settings = processor.reEqSettings(slot);
    if (settings.listenBand != -1)
    {
        settings.listenBand = -1;
        settings.listenDetector = false;
        settings.listenGainDb = 0.0f;
        processor.setReEqSettings(slot, settings, false);
    }
    momentaryListen = false;
}

void ReEqView::updateListen(juce::Point<float> point, juce::ModifierKeys modifiers)
{
    if (!active || !isShowing() || !modifiers.isShiftDown() || !plot.contains(point))
    {
        if (momentaryListen)
            stopListen();
        return;
    }
    const auto band = bandAt(point);
    if (band >= 0)
    {
        momentaryListen = true;
        setListen(band,
                  modifiers.isCommandDown() && settings.bands[static_cast<size_t>(band)].dynamic);
    }
    else
    {
        momentaryListen = true;
        if (!gesture)
            settings = processor.reEqSettings(slot);
        const auto frequency = frequencyForX(point.x);
        const auto gain =
            modifiers.isLeftButtonDown() ? juce::jlimit(-30.0f, 30.0f, gainForY(point.y)) : 0.0f;
        if (settings.listenBand == -2 && !settings.listenDetector &&
            juce::exactlyEqual(settings.listenFrequency, frequency) &&
            juce::exactlyEqual(settings.listenGainDb, gain))
            return;
        settings.listenBand = -2;
        settings.listenDetector = false;
        settings.listenFrequency = frequency;
        settings.listenGainDb = gain;
        processor.setReEqSettings(slot, settings, false);
    }
}

void ReEqView::mouseMove(const juce::MouseEvent& event)
{
    lastMouse = event.position;
    hoverBand = plot.contains(event.position) ? bandAt(event.position) : -1;
    updateListen(event.position, event.mods);
    repaint();
}

void ReEqView::mouseExit(const juce::MouseEvent&)
{
    hoverBand = -1;
    if (momentaryListen)
        stopListen();
    repaint();
}

void ReEqView::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    updateListen(lastMouse, modifiers);
}

void ReEqView::focusLost(FocusChangeType)
{
    if (momentaryListen)
        stopListen();
}

void ReEqView::visibilityChanged()
{
    if (!isShowing())
    {
        stopListen();
        endGesture();
    }
}

bool ReEqView::keyPressed(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::escapeKey)
    {
        stopListen();
        drag = Drag::none;
        endGesture();
        select(-1, false);
        return true;
    }
    if (key.getModifiers().isCommandDown() && (code == 'a' || code == 'A'))
    {
        for (int i = 0; i < reEqBandCount; ++i)
            if (settings.bands[static_cast<size_t>(i)].enabled != 0)
            {
                selected.set(static_cast<size_t>(i));
                selectedBand = i;
            }
        syncControls();
        repaint();
        return true;
    }
    if (code == juce::KeyPress::insertKey || code == '+')
    {
        createBand(plot.getCentre());
        return true;
    }
    if (!hasSelection())
        return false;
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (key.getModifiers().isCommandDown() && (code == 'd' || code == 'D'))
    {
        duplicateSelection(key.getModifiers().isShiftDown());
        return true;
    }
    if (code == juce::KeyPress::spaceKey)
    {
        const auto next = settings.bands[static_cast<size_t>(selectedBand)].enabled == 2 ? 1 : 2;
        applySelected("ReEQ2 band bypass", [next](auto& b) { b.enabled = next; });
        return true;
    }
    if (code == juce::KeyPress::homeKey)
    {
        applySelected("Zero ReEQ2 gain", [](auto& b) { b.gainDb = 0.0f; });
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey ||
        code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        applySelected("Adjust ReEQ2 bands",
                      [&key, code](auto& b)
                      {
                          const auto fine = key.getModifiers().isAltDown();
                          if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
                              b.frequency *=
                                  std::pow(2.0f, (code == juce::KeyPress::rightKey ? 1.0f : -1.0f) /
                                                     (fine ? 120.0f : 12.0f));
                          else if (key.getModifiers().isCommandDown() && hasQ(b))
                              b.q *= std::pow(2.0f, (code == juce::KeyPress::upKey ? 1.0f : -1.0f) *
                                                        (fine ? 0.01f : 0.1f));
                          else if (!key.getModifiers().isCommandDown())
                              setNodeGain(b, nodeGain(b) +
                                                 (code == juce::KeyPress::upKey ? 1.0f : -1.0f) *
                                                     (fine ? 0.1f : 0.5f));
                      });
        return true;
    }
    if (code == juce::KeyPress::returnKey ||
        (code == juce::KeyPress::F10Key && key.getModifiers().isShiftDown()))
    {
        showBandMenu();
        return true;
    }
    return false;
}

void ReEqView::initializeChoices()
{
    bandChoice.setTitle("Selected band");
    bandChoice.setTextWhenNothingSelected("Select band");
    shapeChoice.setTitle("Filter shape");
    groupChoice.setTitle("Stereo placement");
    slopeChoice.setTitle("Filter slope");
    pageChoice.setTitle("Control page");
    for (int i = 0; i < 14; ++i)
        shapeChoice.addItem(reEqShapeName(static_cast<ReEqShape>(i)), i + 1);
    for (int i = 0; i < 5; ++i)
        groupChoice.addItem(reEqGroupName(static_cast<ReEqGroup>(i)), i + 1);
    for (const auto slope : slopes)
        slopeChoice.addItem(juce::String(slope) + " dB/oct", slope);
    pageChoice.addItemList({"Band", "Dynamics", "Detector", "I/O"}, 1);
    pageChoice.setSelectedId(1, juce::dontSendNotification);
    bandChoice.onChange = [this]
    {
        if (!syncing)
            select(bandChoice.getSelectedId() - 1, false);
    };
    shapeChoice.onChange = [this]
    {
        if (!syncing)
            applySelected("ReEQ2 shape", [this](auto& b)
                          { b.shape = static_cast<ReEqShape>(shapeChoice.getSelectedId() - 1); });
    };
    groupChoice.onChange = [this]
    {
        if (!syncing)
            applySelected("ReEQ2 placement", [this](auto& b)
                          { b.group = static_cast<ReEqGroup>(groupChoice.getSelectedId() - 1); });
    };
    slopeChoice.onChange = [this]
    {
        if (!syncing)
            applySelected("ReEQ2 slope",
                          [this](auto& b) { b.slope = slopeChoice.getSelectedId() / 6 - 1; });
    };
    pageChoice.onChange = [this]
    {
        panelPage = pageChoice.getSelectedId() - 1;
        configureFields();
        resized();
        repaint();
    };
    syncControls();
}

ReEqView::~ReEqView()
{
    stopListen();
    endGesture();
}

void ReEqView::setVisualizationActive(bool value)
{
    if (active == value)
        return;
    responseDirty = true;
    active = value;
    if (!active)
    {
        stopListen();
        endGesture();
        analyzer->valid = false;
        analyzer->available = 0;
        analyzer->position = processor.getVisualizationData().slotData(slot).outputLeft.position();
    }
    repaint();
}

void ReEqView::beginGesture(const juce::String& name)
{
    if (!gesture)
    {
        // Read the latest state before editing, so a stale frame cannot replace
        // an undo, a snapshot recall, or another processor-side update.
        settings = processor.reEqSettings(slot);
        processor.beginUndoTransaction(name);
        gesture = true;
    }
}

void ReEqView::endGesture()
{
    if (gesture)
    {
        gesture = false;
        processor.endUndoTransaction();
    }
}

void ReEqView::publish()
{
    settings.sanitize();
    processor.setReEqSettings(slot, settings, false);
    responseDirty = true;
    syncControls();
    repaint();
}

void ReEqView::apply(const juce::String& name, const Mutation& mutation)
{
    endGesture();
    beginGesture(name);
    mutation(settings);
    publish();
    endGesture();
}

bool ReEqView::hasSelection() const
{
    return selectedBand >= 0 && selectedBand < reEqBandCount &&
           settings.bands[static_cast<size_t>(selectedBand)].enabled != 0;
}

void ReEqView::applySelected(const juce::String& name,
                             const std::function<void(ReEqBand&)>& mutation)
{
    if (!hasSelection())
        return;
    apply(name,
          [this, &mutation](auto& state)
          {
              for (int i = 0; i < reEqBandCount; ++i)
                  if (selected[static_cast<size_t>(i)] &&
                      state.bands[static_cast<size_t>(i)].enabled != 0)
                      mutation(state.bands[static_cast<size_t>(i)]);
          });
}

bool ReEqView::isCut(ReEqShape shape)
{
    return shape == ReEqShape::lowCut || shape == ReEqShape::lowCutButterworth ||
           shape == ReEqShape::highCut || shape == ReEqShape::highCutButterworth;
}

void ReEqView::configureFields()
{
    const auto selectedValid = hasSelection();
    const ReEqBand band =
        selectedValid ? settings.bands[static_cast<size_t>(selectedBand)] : ReEqBand{};
    const auto db = [](float v) { return juce::String(v, 1) + " dB"; };
    const auto number = [](float v) { return juce::String(v, 2); };
    const auto ms = [](float v) { return juce::String(v, v < 10.0f ? 2 : 1) + " ms"; };
    struct Field
    {
        const char* name;
        float minimum, maximum, reset, value;
        bool log;
        std::function<juce::String(float)> format;
    };
    std::array<Field, 6> fields;
    if (panelPage == 1)
        fields = {{{"Thresh", -60, 0, -24, band.thresholdDb, false, db},
                   {"Ratio", 1, 20, 4, band.ratio, true,
                    [](float v) { return juce::String(v, 1) + ":1"; }},
                   {"Range", -24, 24, -6, band.rangeDb, false, db},
                   {"Attack", 0.1f, 200, 5, band.attackMs, true, ms},
                   {"Release", 5, 2000, 80, band.releaseMs, true, ms},
                   {"Freq", 10, 30000, 1000, band.frequency, true, frequencyText}}};
    else if (panelPage == 2)
        fields = {{{"Detect", 20, 20000, 1000, band.detectorFrequency, true, frequencyText},
                   {"Width", 0.25f, 8, 2, band.detectorWidth, true,
                    [](float v) { return juce::String(v, 2) + " oct"; }},
                   {"Thresh", -60, 0, -24, band.thresholdDb, false, db},
                   {"Range", -24, 24, -6, band.rangeDb, false, db},
                   {"Attack", 0.1f, 200, 5, band.attackMs, true, ms},
                   {"Release", 5, 2000, 80, band.releaseMs, true, ms}}};
    else if (panelPage == 3)
        fields = {
            {{settings.midSide ? "Mid" : "Left", -136, 30, 0, settings.firstGainDb, false, db},
             {settings.midSide ? "Side" : "Right", -136, 30, 0, settings.secondGainDb, false, db},
             {"IN", -136, 30, 0, settings.inputDb, false, db},
             {"OUT", -154, 48, 0, settings.outputDb + retainedOutputDb(), false, db},
             {"Scale", 0, 200, 100, settings.scale * 100.0f, false,
              [](float v) { return juce::String(v, 0) + "%"; }},
             {"Look", 0, 10, 0, settings.lookaheadMs, false, ms}}};
    else
        fields = {{{"Freq", 10, 30000, 500, band.frequency, true, frequencyText},
                   {"Gain", -18, 18, 0, band.gainDb, false, db},
                   {"Q", 0.1f, 40, 0.70710678f, band.q, true, number},
                   {"Thresh", -60, 0, -24, band.thresholdDb, false, db},
                   {"Range", -24, 24, -6, band.rangeDb, false, db},
                   {"Ratio", 1, 20, 4, band.ratio, true,
                    [](float v) { return juce::String(v, 1) + ":1"; }}}};

    for (size_t i = 0; i < bandFields.size(); ++i)
    {
        auto& control = *bandFields[i];
        const auto& field = fields[i];
        control.configure(field.name, field.minimum, field.maximum, field.reset, field.log,
                          field.format);
        control.assign(field.value);
        const auto dynamicControl = panelPage == 1 || panelPage == 2 || (panelPage == 0 && i >= 3);
        const auto automaticTiming = band.autoTiming && ((panelPage == 1 && (i == 3 || i == 4)) ||
                                                         (panelPage == 2 && i >= 4));
        const auto linkedDetector = panelPage == 2 && i < 2 && band.detectorLinked;
        const auto unusedGainOrQ =
            panelPage == 0 && ((i == 1 && !hasGain(band.shape)) || (i == 2 && !hasQ(band)));
        control.setEnabled(panelPage == 3 ||
                           (selectedValid &&
                            (!dynamicControl || (band.dynamic && dynamicShape(band.shape))) &&
                            !automaticTiming && !linkedDetector && !unusedGainOrQ));
        control.setTooltip(
            automaticTiming                   ? "Automatic timing active; manual value retained"
            : linkedDetector                  ? "Detector follows band; independent value retained"
            : panelPage == 3 && i == 3        ? "Total native plus retained host Output automation"
            : dynamicControl && !band.dynamic ? "Enable DYN to edit dynamic processing"
                                              : control.getDescription());
        control.onChange = [this, i](float v)
        {
            if (panelPage == 3)
            {
                if (i == 0)
                    settings.firstGainDb = v;
                if (i == 1)
                    settings.secondGainDb = v;
                if (i == 2)
                    settings.inputDb = v;
                if (i == 3)
                    settings.outputDb = v - retainedOutputDb();
                if (i == 4)
                    settings.scale = v * 0.01f;
                if (i == 5)
                    settings.lookaheadMs = v;
            }
            else
            {
                for (int n = 0; n < reEqBandCount; ++n)
                {
                    if (!selected[static_cast<size_t>(n)] ||
                        settings.bands[static_cast<size_t>(n)].enabled == 0)
                        continue;
                    auto& b = settings.bands[static_cast<size_t>(n)];
                    if (panelPage == 0)
                    {
                        if (i == 0)
                            b.frequency = v;
                        if (i == 1)
                        {
                            if (hasGain(b.shape))
                                b.gainDb = v;
                        }
                        if (i == 2)
                        {
                            if (hasQ(b))
                                b.q = v;
                        }
                        if (i == 3)
                        {
                            if (dynamicShape(b.shape) && b.dynamic)
                                b.thresholdDb = v;
                        }
                        if (i == 4)
                        {
                            if (dynamicShape(b.shape) && b.dynamic)
                                b.rangeDb = v;
                        }
                        if (i == 5)
                        {
                            if (dynamicShape(b.shape) && b.dynamic)
                                b.ratio = v;
                        }
                    }
                    else if (panelPage == 1)
                    {
                        if (!dynamicShape(b.shape) || !b.dynamic)
                            continue;
                        if (i == 0)
                            b.thresholdDb = v;
                        if (i == 1)
                            b.ratio = v;
                        if (i == 2)
                            b.rangeDb = v;
                        if (i == 3)
                        {
                            if (!b.autoTiming)
                                b.attackMs = v;
                        }
                        if (i == 4)
                        {
                            if (!b.autoTiming)
                                b.releaseMs = v;
                        }
                        if (i == 5)
                            b.frequency = v;
                    }
                    else
                    {
                        if (!dynamicShape(b.shape) || !b.dynamic)
                            continue;
                        if (i == 0)
                        {
                            if (!b.detectorLinked)
                                b.detectorFrequency = v;
                        }
                        if (i == 1)
                        {
                            if (!b.detectorLinked)
                                b.detectorWidth = v;
                        }
                        if (i == 2)
                            b.thresholdDb = v;
                        if (i == 3)
                            b.rangeDb = v;
                        if (i == 4)
                        {
                            if (!b.autoTiming)
                                b.attackMs = v;
                        }
                        if (i == 5)
                        {
                            if (!b.autoTiming)
                                b.releaseMs = v;
                        }
                    }
                }
            }
            publish();
        };
    }
}

void ReEqView::syncControls()
{
    const juce::ScopedValueSetter<bool> guard(syncing, true);
    globalFields[0]->assign(settings.inputDb);
    globalFields[1]->assign(settings.outputDb + retainedOutputDb());
    globalFields[2]->assign(settings.scale * 100.0f);
    autoGain.setToggleState(settings.autoGain, juce::dontSendNotification);
    quality.setToggleState(false, juce::dontSendNotification);
    limit.setToggleState(settings.limitOutput, juce::dontSendNotification);
    bandChoice.clear(juce::dontSendNotification);
    for (int i = 0; i < reEqBandCount; ++i)
    {
        const auto& b = settings.bands[static_cast<size_t>(i)];
        if (b.enabled != 0)
            bandChoice.addItem(juce::String(i + 1) + (b.enabled == 1 ? " Bypassed" : "") +
                                   (b.dynamic ? " DYN" : ""),
                               i + 1);
        else
            selected.reset(static_cast<size_t>(i));
    }
    if (!hasSelection())
        selectedBand = -1;
    bandChoice.setSelectedId(selectedBand + 1, juce::dontSendNotification);
    const auto valid = hasSelection();
    shapeChoice.setEnabled(valid);
    groupChoice.setEnabled(valid);
    actions.setEnabled(valid);
    dynamicButton.setEnabled(valid &&
                             dynamicShape(settings.bands[static_cast<size_t>(selectedBand)].shape));
    dynamicButton.setToggleState(valid && settings.bands[static_cast<size_t>(selectedBand)].dynamic,
                                 juce::dontSendNotification);
    if (valid)
    {
        const auto& band = settings.bands[static_cast<size_t>(selectedBand)];
        shapeChoice.setSelectedId(static_cast<int>(band.shape) + 1, juce::dontSendNotification);
        groupChoice.setSelectedId(static_cast<int>(band.group) + 1, juce::dontSendNotification);
        slopeChoice.setSelectedId(reEqSlopeDb(band.slope), juce::dontSendNotification);
        slopeChoice.setEnabled(isCut(band.shape));
    }
    else
        slopeChoice.setEnabled(false);
    configureFields();
}

void ReEqView::refresh()
{
    auto changed = responseDirty;
    const auto previousPosition = analyzer->position;
    const auto previouslyValid = analyzer->valid;
    const auto latest = processor.reEqSettings(slot);
    if (!gesture && latest != settings)
    {
        settings = latest;
        syncControls();
        responseDirty = true;
        changed = true;
    }
    if (!gesture)
    {
        const auto trim = retainedOutputDb();
        changed = changed || !juce::exactlyEqual(trim, lastOutputTrimDb);
        lastOutputTrimDb = trim;
        globalFields[1]->assign(settings.outputDb + trim);
        if (panelPage == 3)
            bandFields[3]->assign(settings.outputDb + trim);
    }
    if (active && isShowing())
    {
        const auto* module =
            dynamic_cast<const EqualizerModule*>(processor.getRack().activeModuleInstance(slot));
        if (module != nullptr)
            for (int band = 0; band < reEqBandCount; ++band)
            {
                const auto index = static_cast<size_t>(band);
                const auto delta = module->reEqProcessor().dynamicGainDb(band);
                if (std::abs(delta - analyzer->dynamicGain[index]) > 0.025f)
                {
                    analyzer->dynamicGain[index] = delta;
                    responseDirty = true;
                }
                const auto detector = module->reEqProcessor().detectorLevelDb(band);
                changed = changed
                    || !juce::exactlyEqual(detector, analyzer->detectorLevel[index]);
                analyzer->detectorLevel[index] = detector;
            }
        if (momentaryListen && !juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
            stopListen();
        if (settings.displayMode != 2)
            updateAnalyzer();
    }
    if (changed || responseDirty || previousPosition != analyzer->position
        || previouslyValid != analyzer->valid)
        repaint();
}

void ReEqView::resized()
{
    const auto compact = getHeight() < 280;
    const auto rowHeight = compact ? 22 : 24;
    auto bounds = getLocalBounds().reduced(compact ? 3 : 5);
    auto header = bounds.removeFromTop(rowHeight);
    const auto unit = header.getWidth() / 9;
    for (auto& field : globalFields)
        field->setBounds(header.removeFromLeft(unit).reduced(1, 0));
    for (auto* button : {&autoGain, &quality, &limit, &analyzerButton, &settingsButton})
    {
        const auto width = button == &analyzerButton || button == &settingsButton
                               ? (header.getWidth() / (button == &analyzerButton ? 2 : 1))
                               : unit * 2 / 3;
        button->setBounds(header.removeFromLeft(width).reduced(1, 0));
    }
    bounds.removeFromTop(compact ? 3 : 4);
    auto numeric = bounds.removeFromBottom(rowHeight);
    const auto fieldWidth = numeric.getWidth() / 6;
    for (size_t i = 0; i < bandFields.size(); ++i)
        bandFields[i]->setBounds(
            numeric.removeFromLeft(i == bandFields.size() - 1 ? numeric.getWidth() : fieldWidth)
                .reduced(1, 0));
    bounds.removeFromBottom(compact ? 2 : 3);
    auto row = bounds.removeFromBottom(rowHeight);
    const std::array<int, 7> weights{14, 22, 14, 13, 14, 7, 16};
    const auto rowWidth = row.getWidth();
    std::array<juce::Component*, 7> controls{&bandChoice,  &shapeChoice, &groupChoice,
                                             &slopeChoice, &pageChoice,  &dynamicButton,
                                             &actions};
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setBounds(row.removeFromLeft(i == controls.size() - 1
                                                      ? row.getWidth()
                                                      : rowWidth * weights[i] / 100)
                                   .reduced(1, 0));
    bounds.removeFromBottom(compact ? 12 : 20);
    plot = bounds.toFloat();
    responseDirty = true;
}

float ReEqView::xForFrequency(float frequency) const
{
    return plot.getX() + plot.getWidth() *
                             std::log(juce::jlimit(minimumFrequency, maximumFrequency, frequency) /
                                      minimumFrequency) /
                             std::log(maximumFrequency / minimumFrequency);
}

float ReEqView::frequencyForX(float x) const
{
    return minimumFrequency *
           std::pow(maximumFrequency / minimumFrequency,
                    juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / std::max(1.0f, plot.getWidth())));
}

float ReEqView::yForGain(float gain) const
{
    return plot.getCentreY() -
           gain * plot.getHeight() / (2.0f * static_cast<float>(settings.rangeDb));
}

float ReEqView::gainForY(float y) const
{
    return (plot.getCentreY() - y) * (2.0f * static_cast<float>(settings.rangeDb)) /
           std::max(1.0f, plot.getHeight());
}

juce::Point<float> ReEqView::bandPoint(int band) const
{
    const auto& b = settings.bands[static_cast<size_t>(band)];
    return {xForFrequency(b.frequency),
            juce::jlimit(plot.getY() + 8.0f, plot.getBottom() - 8.0f, yForGain(nodeGain(b)))};
}

int ReEqView::bandAt(juce::Point<float> point) const
{
    int nearest = -1;
    auto distance = 15.0f;
    for (int i = reEqBandCount - 1; i >= 0; --i)
    {
        if (settings.bands[static_cast<size_t>(i)].enabled == 0)
            continue;
        const auto next = bandPoint(i).getDistanceFrom(point);
        if (next < distance)
        {
            nearest = i;
            distance = next;
        }
    }
    return nearest;
}

int ReEqView::detectorAt(juce::Point<float> point) const
{
    if (!hasSelection())
        return -1;
    const auto& band = settings.bands[static_cast<size_t>(selectedBand)];
    if (band.dynamic && !band.detectorLinked &&
        juce::Point<float>(xForFrequency(band.detectorFrequency), plot.getBottom() - 13.0f)
                .getDistanceFrom(point) < 11.0f)
        return selectedBand;
    return -1;
}

juce::Colour ReEqView::bandColour(int band)
{
    static constexpr std::array<juce::uint32, reEqBandCount> colours{
        0xffe6bc62, 0xffe89261, 0xffdf6c77, 0xffd983b5, 0xffb594e2, 0xff8a9ce9,
        0xff6baee9, 0xff64c5d5, 0xff6ad0b4, 0xff86cf8d, 0xffb8cd73, 0xffddd673,
        0xffc69467, 0xffc78087, 0xffa093c7, 0xff7faead};
    return juce::Colour(colours[static_cast<size_t>(band)]);
}

void ReEqView::rebuildResponse()
{
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    if (!responseDirty && responseSettings == settings &&
        juce::exactlyEqual(sampleRate, responseSampleRate))
        return;
    responseDirty = false;
    responseSettings = settings;
    responseSampleRate = sampleRate;
    groupPresent.fill(false);
    for (auto& curve : bandCurves)
        curve.clear();
    for (auto& curve : groupCurves)
        curve.clear();
    for (int i = 0; i < reEqBandCount; ++i)
    {
        const auto& band = settings.bands[static_cast<size_t>(i)];
        if (band.enabled == 2)
            groupPresent[static_cast<size_t>(band.group)] = true;
    }
    for (int point = 0; point <= 320; ++point)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float>(point) / 320.0f;
        const auto frequency = frequencyForX(x);
        std::array<float, 5> gain{};
        for (int i = 0; i < reEqBandCount; ++i)
        {
            const auto& band = settings.bands[static_cast<size_t>(i)];
            if (band.enabled == 0)
                continue;
            auto displayBand = band;
            displayBand.enabled = 2;
            if (band.dynamic && band.enabled == 2 && active && dynamicShape(band.shape))
                displayBand.gainDb += analyzer->dynamicGain[static_cast<size_t>(i)];
            const auto db = safeDb(ReEqProcessor::bandMagnitude(
                displayBand, sampleRate, static_cast<double>(frequency), settings.scale));
            const auto y =
                juce::jlimit(plot.getY() - 100.0f, plot.getBottom() + 100.0f, yForGain(db));
            if (point == 0)
                bandCurves[static_cast<size_t>(i)].startNewSubPath(x, y);
            else
                bandCurves[static_cast<size_t>(i)].lineTo(x, y);
            if (band.enabled == 2)
                gain[static_cast<size_t>(band.group)] += db;
        }
        for (int group = 0; group < 5; ++group)
        {
            const auto db = gain[static_cast<size_t>(group)] + (group == 0 ? 0.0f : gain[0]);
            const auto y =
                juce::jlimit(plot.getY() - 100.0f, plot.getBottom() + 100.0f, yForGain(db));
            if (point == 0)
                groupCurves[static_cast<size_t>(group)].startNewSubPath(x, y);
            else
                groupCurves[static_cast<size_t>(group)].lineTo(x, y);
        }
    }
}

void ReEqView::paintPiano(juce::Graphics& g)
{
    if (!settings.showPiano)
        return;
    const auto bottom = plot.getBottom();
    for (int note = 0; note < 128; ++note)
    {
        const auto frequency = 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
        if (frequency < minimumFrequency || frequency > maximumFrequency)
            continue;
        const auto x = xForFrequency(frequency);
        const auto next = xForFrequency(frequency * std::pow(2.0f, 1.0f / 12.0f));
        const auto pitch = note % 12;
        const auto black = pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
        g.setColour(black ? juce::Colour(0xff182126) : juce::Colour(0xff6a767b));
        g.fillRect(x, bottom - (black ? 8.0f : 12.0f), std::max(1.0f, next - x - 1.0f),
                   black ? 8.0f : 12.0f);
        if (pitch == 0 && getWidth() >= 640)
        {
            g.setColour(foreground);
            g.setFont(9.0f);
            g.drawText("C" + juce::String(note / 12 - 1),
                       juce::Rectangle<float>(x, bottom - 23.0f, 24.0f, 10.0f),
                       juce::Justification::left);
        }
    }
}

void ReEqView::paint(juce::Graphics& g)
{
    g.fillAll(background);
    if (plot.getWidth() < 1.0f || plot.getHeight() < 1.0f)
        return;
    rebuildResponse();
    g.setFont(10.0f);
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(plot.toNearestInt());
        paintAnalyzer(g);
        for (const auto frequency :
             {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f})
        {
            g.setColour(juce::Colour(0xff253036));
            g.drawVerticalLine(juce::roundToInt(xForFrequency(frequency)), plot.getY(),
                               plot.getBottom());
        }
        const auto step = settings.rangeDb / 3;
        for (int gain = -settings.rangeDb; gain <= settings.rangeDb; gain += std::max(1, step))
        {
            const auto y = yForGain(static_cast<float>(gain));
            g.setColour(gain == 0 ? juce::Colour(0xff526068) : juce::Colour(0xff253036));
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
            g.setColour(muted);
            g.drawText((gain > 0 ? "+" : "") + juce::String(gain),
                       juce::Rectangle<float>(plot.getRight() - 30.0f, y + 2.0f, 27.0f, 11.0f),
                       juce::Justification::right);
        }
        paintPiano(g);
        for (int i = 0; i < reEqBandCount; ++i)
        {
            const auto& b = settings.bands[static_cast<size_t>(i)];
            if (b.enabled == 0)
                continue;
            const auto colour = bandColour(i);
            g.setColour(colour.withAlpha(b.enabled == 1                     ? 0.18f
                                         : selected[static_cast<size_t>(i)] ? 0.64f
                                                                            : 0.27f));
            g.strokePath(bandCurves[static_cast<size_t>(i)], juce::PathStrokeType(1.0f));
        }
        const std::array<juce::Colour, 5> groupColours{
            juce::Colour(0xfff3dcb2), accent, juce::Colour(0xffd595cf), juce::Colour(0xff78d3a6),
            juce::Colour(0xffe7a26e)};
        int legendX = static_cast<int>(plot.getX()) + 6;
        for (int group = 0; group < 5; ++group)
        {
            if (!groupPresent[static_cast<size_t>(group)])
                continue;
            g.setColour(groupColours[static_cast<size_t>(group)]);
            g.strokePath(groupCurves[static_cast<size_t>(group)], juce::PathStrokeType(1.8f));
            g.drawText(reEqGroupName(static_cast<ReEqGroup>(group)), legendX,
                       static_cast<int>(plot.getY()) + 4, 48, 12, juce::Justification::left);
            legendX += 50;
        }
        if (hasSelection())
        {
            const auto& band = settings.bands[static_cast<size_t>(selectedBand)];
            if (band.dynamic)
            {
                const auto centre = bandPoint(selectedBand);
                const auto rangeY = juce::jlimit(plot.getY() + 5.0f, plot.getBottom() - 5.0f,
                                                 yForGain(band.gainDb + band.rangeDb));
                g.setColour(bandColour(selectedBand).withAlpha(0.75f));
                g.drawLine(centre.x, centre.y, centre.x, rangeY, 2.0f);
                g.fillRect(centre.x - 6.0f, rangeY - 2.0f, 12.0f, 4.0f);
                if (!band.detectorLinked)
                {
                    const auto dx = xForFrequency(band.detectorFrequency);
                    g.drawEllipse(dx - 6.0f, plot.getBottom() - 19.0f, 12.0f, 12.0f, 1.5f);
                    g.drawText(
                        "DET",
                        juce::Rectangle<float>(dx - 14.0f, plot.getBottom() - 33.0f, 28.0f, 11.0f),
                        juce::Justification::centred);
                }
            }
        }
    }
    paintNodesAndLabels(g);
}

void ReEqView::showMenu(juce::PopupMenu menu, std::function<void(int)> callback)
{
    const juce::Component::SafePointer<ReEqView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withStandardItemHeight(24)
                           .withMinimumWidth(210),
                       [safe, onResult = std::move(callback)](int result)
                       {
                           if (safe != nullptr && result != 0)
                               onResult(result);
                       });
}

void ReEqView::showBandMenu()
{
    if (!hasSelection())
        return;
    const auto& band = settings.bands[static_cast<size_t>(selectedBand)];
    juce::PopupMenu menu, shapes, groups, slopeMenu, dynamics;
    menu.addSectionHeader(
        "Band " + juce::String(selectedBand + 1) +
        (selected.count() > 1
             ? " + " + juce::String(static_cast<int>(selected.count() - 1)) + " selected"
             : ""));
    menu.addItem(1, band.enabled == 2 ? "Bypass selected" : "Enable selected");
    menu.addItem(2, "Invert gain", hasGain(band.shape));
    menu.addItem(3, "Zero gain", hasGain(band.shape));
    for (int i = 0; i < 14; ++i)
        shapes.addItem(100 + i, reEqShapeName(static_cast<ReEqShape>(i)), true,
                       static_cast<int>(band.shape) == i);
    for (int i = 0; i < 5; ++i)
        groups.addItem(200 + i, reEqGroupName(static_cast<ReEqGroup>(i)), true,
                       static_cast<int>(band.group) == i);
    for (size_t i = 0; i < slopes.size(); ++i)
        slopeMenu.addItem(300 + static_cast<int>(i), juce::String(slopes[i]) + " dB/oct", true,
                          reEqSlopeDb(band.slope) == slopes[i]);
    menu.addSubMenu("Shape", shapes);
    menu.addSubMenu("Stereo placement", groups);
    menu.addSubMenu("Slope", slopeMenu, isCut(band.shape));
    menu.addSeparator();
    menu.addItem(4, "Duplicate selected    Cmd+D");
    menu.addItem(5, "Split selected to Mid / Side    Cmd+Shift+D");
    menu.addItem(6, "Delete selected    Delete");
    menu.addSeparator();
    dynamics.addItem(7, "Dynamic processing", dynamicShape(band.shape), band.dynamic);
    dynamics.addItem(8, "Automatic attack / release", true, band.autoTiming);
    const auto* sidechain = processor.getBus(true, 1);
    const auto sidechainAvailable = sidechain != nullptr && sidechain->isEnabled();
    dynamics.addItem(9,
                     sidechainAvailable ? "External sidechain"
                                        : "External sidechain (unavailable; input fallback)",
                     sidechainAvailable, band.externalSidechain);
    dynamics.addItem(13, "Internal sidechain", true, !band.externalSidechain);
    dynamics.addItem(10, "Detector follows band", true, band.detectorLinked);
    dynamics.addItem(11, "Listen to detector", band.dynamic,
                     settings.listenBand == selectedBand && settings.listenDetector);
    dynamics.addItem(12, "Listen to band", true,
                     settings.listenBand == selectedBand && !settings.listenDetector);
    dynamics.addSeparator();
    dynamics.addItem(14, "Edit dynamics controls");
    dynamics.addItem(15, "Edit detector controls");
    menu.addSubMenu("Dynamics / detector", dynamics, dynamicShape(band.shape));
    menu.addSeparator();
    menu.addItem(16, "Select all bands    Cmd+A");
    menu.addItem(17, "Add band", bandChoice.getNumItems() < reEqBandCount);
    showMenu(std::move(menu),
             [this](int result)
             {
                 if (!hasSelection() && result != 17)
                     return;
                 if (result >= 100 && result < 114)
                     applySelected("ReEQ2 shape", [result](auto& b)
                                   { b.shape = static_cast<ReEqShape>(result - 100); });
                 else if (result >= 200 && result < 205)
                     applySelected("ReEQ2 placement", [result](auto& b)
                                   { b.group = static_cast<ReEqGroup>(result - 200); });
                 else if (result >= 300 && result < 310)
                     applySelected(
                         "ReEQ2 slope", [result](auto& b)
                         { b.slope = slopes[static_cast<size_t>(result - 300)] / 6 - 1; });
                 else if (result == 1)
                 {
                     const auto next =
                         settings.bands[static_cast<size_t>(selectedBand)].enabled == 2 ? 1 : 2;
                     applySelected("ReEQ2 band bypass", [next](auto& b) { b.enabled = next; });
                 }
                 else if (result == 2)
                     applySelected("Invert ReEQ2 gain", [](auto& b) { b.gainDb = -b.gainDb; });
                 else if (result == 3)
                     applySelected("Zero ReEQ2 gain", [](auto& b) { b.gainDb = 0.0f; });
                 else if (result == 4 || result == 5)
                     duplicateSelection(result == 5);
                 else if (result == 6)
                     deleteSelection();
                 else if (result == 7 || result == 8 || result == 9 || result == 10 || result == 13)
                 {
                     const auto& current = settings.bands[static_cast<size_t>(selectedBand)];
                     const auto next = result == 7    ? !current.dynamic
                                       : result == 8  ? !current.autoTiming
                                       : result == 10 ? !current.detectorLinked
                                                      : result == 9;
                     applySelected("ReEQ2 dynamics",
                                   [result, next](auto& b)
                                   {
                                       if (result == 7 && dynamicShape(b.shape))
                                           b.dynamic = next;
                                       if (result == 8)
                                           b.autoTiming = next;
                                       if (result == 9 || result == 13)
                                           b.externalSidechain = next;
                                       if (result == 10)
                                           b.detectorLinked = next;
                                   });
                 }
                 else if (result == 11 || result == 12)
                 {
                     if (settings.listenBand == selectedBand &&
                         settings.listenDetector == (result == 11))
                         stopListen();
                     else
                         setListen(selectedBand, result == 11);
                 }
                 else if (result == 14 || result == 15)
                     pageChoice.setSelectedId(result == 14 ? 2 : 3);
                 else if (result == 16)
                     keyPressed(juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0));
                 else if (result == 17)
                     createBand(plot.getCentre());
             });
}

void ReEqView::showAnalyzerMenu()
{
    juce::PopupMenu menu, display, spectrum, fft, window, smoothing, tilt, ceiling, floor;
    menu.addSectionHeader("Analyzer");
    const std::array<const char*, 4> displayNames{"Fill", "Line", "Off", "Heat"};
    for (int i = 0; i < 4; ++i)
        display.addItem(100 + i, displayNames[static_cast<size_t>(i)], true,
                        settings.displayMode == i);
    const std::array<const char*, 7> spectrumNames{
        "Full (stereo energy)", "Mid", "Side", "Mid + Side", "Left", "Right", "Left + Right"};
    for (int i = 0; i < 7; ++i)
        spectrum.addItem(200 + i, spectrumNames[static_cast<size_t>(i)], true,
                         settings.spectrumMode == i);
    for (int order = 11; order <= 14; ++order)
        fft.addItem(300 + order, juce::String(1 << order), true, settings.fftOrder == order);
    const std::array<const char*, 4> windowNames{"Hamming", "Blackman-Harris", "Blackman",
                                                 "Rectangular"};
    for (int i = 0; i < 4; ++i)
        window.addItem(400 + i, windowNames[static_cast<size_t>(i)], true,
                       settings.windowType == i);
    const std::array<const char*, 5> smoothNames{"Off", "1/48 octave", "1/24 octave", "1/12 octave",
                                                 "1/6 octave"};
    for (int i = 0; i < 5; ++i)
    {
        smoothing.addItem(500 + i, smoothNames[static_cast<size_t>(i)], true,
                          settings.smoothing == i);
        tilt.addItem(600 + i, juce::String(static_cast<float>(i) * 1.5f, 1) + " dB/oct", true,
                     std::abs(settings.tilt - static_cast<float>(i) * 1.5f) < 0.1f);
    }
    for (int i = 0; i < 3; ++i)
        ceiling.addItem(700 + i, juce::String(i * 20) + " dBFS", true,
                        settings.ceilingDb == i * 20);
    const std::array<int, 3> floors{-90, -140, -200};
    for (int i = 0; i < 3; ++i)
        floor.addItem(800 + i, juce::String(floors[static_cast<size_t>(i)]) + " dBFS", true,
                      settings.floorDb == floors[static_cast<size_t>(i)]);
    menu.addSubMenu("Display", display);
    menu.addItem(1, "Show Pre-EQ", true, settings.showPreEq);
    menu.addSubMenu("Spectrum", spectrum);
    menu.addItem(2, "Peak hold", true, settings.showPeaks);
    menu.addItem(3, "Piano keyboard", true, settings.showPiano);
    menu.addSeparator();
    menu.addSubMenu("FFT size", fft);
    menu.addSubMenu("Window", window);
    menu.addSubMenu("Smoothing", smoothing);
    menu.addSubMenu("Tilt", tilt);
    menu.addSubMenu("Analyzer ceiling", ceiling);
    menu.addSubMenu("Analyzer floor", floor);
    menu.addItem(4, "Reset peak hold");
    showMenu(std::move(menu),
             [this, floors](int result)
             {
                 if (result == 4)
                 {
                     for (auto& values : analyzer->peaks)
                         values.fill(-200.0f);
                     repaint();
                     return;
                 }
                 apply("ReEQ2 analyzer",
                       [result, floors](auto& s)
                       {
                           if (result == 1)
                               s.showPreEq = !s.showPreEq;
                           if (result == 2)
                               s.showPeaks = !s.showPeaks;
                           if (result == 3)
                               s.showPiano = !s.showPiano;
                           if (result >= 100 && result < 104)
                               s.displayMode = result - 100;
                           if (result >= 200 && result < 207)
                               s.spectrumMode = result - 200;
                           if (result >= 311 && result <= 314)
                               s.fftOrder = result - 300;
                           if (result >= 400 && result < 404)
                               s.windowType = result - 400;
                           if (result >= 500 && result < 505)
                               s.smoothing = result - 500;
                           if (result >= 600 && result < 605)
                               s.tilt = static_cast<float>(result - 600) * 1.5f;
                           if (result >= 700 && result < 703)
                               s.ceilingDb = (result - 700) * 20;
                           if (result >= 800 && result < 803)
                               s.floorDb = floors[static_cast<size_t>(result - 800)];
                       });
                 analyzer->valid = false;
                 for (auto& values : analyzer->peaks)
                     values.fill(-200.0f);
                 responseDirty = true;
             });
}

void ReEqView::showSettingsMenu()
{
    juce::PopupMenu menu, range, lookahead, dynamics;
    menu.addSectionHeader("ReEQ2");
    menu.addItem(1, "Mid / Side trim domain", true, settings.midSide);
    menu.addItem(2, settings.midSide ? "Invert Mid polarity" : "Invert Left polarity", true,
                 settings.firstPolarity);
    menu.addItem(3, settings.midSide ? "Invert Side polarity" : "Invert Right polarity", true,
                 settings.secondPolarity);
    menu.addItem(4, "Automatic gain compensation", true, settings.autoGain);
    menu.addItem(5, "Limit output", true, settings.limitOutput);
    menu.addItem(6, "Edit domain trims / lookahead");
    menu.addSeparator();
    for (int i = 1; i <= 5; ++i)
        range.addItem(100 + i, juce::String(i * 6) + " dB", true, settings.rangeDb == i * 6);
    const std::array<float, 4> ahead{0.0f, 1.0f, 5.0f, 10.0f};
    for (int i = 0; i < 4; ++i)
        lookahead.addItem(
            200 + i, i == 0 ? "Off" : juce::String(ahead[static_cast<size_t>(i)], 0) + " ms", true,
            juce::exactlyEqual(settings.lookaheadMs, ahead[static_cast<size_t>(i)]));
    menu.addSubMenu("Graph range", range);
    menu.addSubMenu("Lookahead (adds latency)", lookahead);
    dynamics.addItem(20, "Enable dynamics on all active bands");
    dynamics.addItem(21, "Disable dynamics on all active bands");
    dynamics.addItem(22, "Automatic timing on all active bands");
    dynamics.addItem(23, "Manual timing on all active bands");
    menu.addSubMenu("Global dynamics", dynamics);
    menu.addItem(7, "Stop listening", settings.listenBand != -1);
    menu.addSeparator();
    menu.addItem(8, "Add band", bandChoice.getNumItems() < reEqBandCount);
    menu.addItem(9, "Select all bands");
    menu.addItem(10, "Reset native EQ", bandChoice.getNumItems() > 0);
    menu.addSeparator();
    menu.addSectionHeader("Drag: Frequency / Gain   Wheel: Q");
    menu.addSectionHeader("Cmd-drag: Frequency / Q   Cmd-wheel: Slope");
    menu.addSectionHeader("Shift: Listen   Cmd+Shift: Detector Listen");
    menu.addSectionHeader("Shift blank graph: Frequency   Wheel: Width   Drag: Level");
    menu.addSectionHeader("Alt: Fine   Alt-double-click: Delete");
    showMenu(std::move(menu),
             [this, ahead](int result)
             {
                 if (result == 6)
                 {
                     pageChoice.setSelectedId(4);
                     return;
                 }
                 if (result == 7)
                 {
                     stopListen();
                     return;
                 }
                 if (result == 8)
                 {
                     createBand(plot.getCentre());
                     return;
                 }
                 if (result == 9)
                 {
                     keyPressed(juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0));
                     return;
                 }
                 if (result == 10)
                     stopListen();
                 apply("ReEQ2 options",
                       [result, ahead](auto& s)
                       {
                           if (result == 1)
                               s.midSide = !s.midSide;
                           if (result == 2)
                               s.firstPolarity = !s.firstPolarity;
                           if (result == 3)
                               s.secondPolarity = !s.secondPolarity;
                           if (result == 4)
                               s.autoGain = !s.autoGain;
                           if (result == 5)
                               s.limitOutput = !s.limitOutput;
                           if (result == 10)
                           {
                               s = {};
                               s.native = true;
                           }
                           if (result > 100 && result <= 105)
                               s.rangeDb = (result - 100) * 6;
                           if (result >= 200 && result < 204)
                               s.lookaheadMs = ahead[static_cast<size_t>(result - 200)];
                           if (result >= 20 && result <= 23)
                               for (auto& band : s.bands)
                                   if (band.enabled == 2)
                                   {
                                       if ((result == 20 || result == 21) &&
                                           dynamicShape(band.shape))
                                           band.dynamic = result == 20;
                                       if (result == 22 || result == 23)
                                           band.autoTiming = result == 22;
                                   }
                       });
             });
}
void ReEqView::paintNodesAndLabels(juce::Graphics& g)
{
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(plot.toNearestInt());
        if (settings.listenBand == -2)
        {
            const auto halfWidth = settings.listenWidthOctaves * 0.5f;
            const auto left = xForFrequency(settings.listenFrequency * std::pow(2.0f, -halfWidth));
            const auto right = xForFrequency(settings.listenFrequency * std::pow(2.0f, halfWidth));
            const auto colour = juce::Colour(0xfff0654c);
            g.setColour(colour.withAlpha(0.08f));
            g.fillRect(juce::Rectangle<float>(left, plot.getY(), right - left, plot.getHeight()));
            g.setColour(colour.withAlpha(0.85f));
            g.drawVerticalLine(juce::roundToInt(left), plot.getY(), plot.getBottom());
            g.drawVerticalLine(juce::roundToInt(right), plot.getY(), plot.getBottom());
            if (!juce::exactlyEqual(settings.listenGainDb, 0.0f))
                g.drawLine(left, yForGain(settings.listenGainDb), right,
                           yForGain(settings.listenGainDb), 1.5f);
        }
        for (int i = 0; i < reEqBandCount; ++i)
        {
            const auto& band = settings.bands[static_cast<size_t>(i)];
            if (band.enabled == 0)
                continue;
            const auto centre = bandPoint(i);
            const auto colour = bandColour(i);
            if (selected[static_cast<size_t>(i)] || hoverBand == i)
            {
                g.setColour(colour.withAlpha(0.18f));
                g.fillEllipse(centre.x - 13.0f, centre.y - 13.0f, 26.0f, 26.0f);
            }
            g.setColour(band.enabled == 1 ? background : colour);
            g.fillEllipse(centre.x - 8.0f, centre.y - 8.0f, 16.0f, 16.0f);
            g.setColour(colour);
            g.drawEllipse(centre.x - 8.0f, centre.y - 8.0f, 16.0f, 16.0f,
                          selected[static_cast<size_t>(i)] ? 2.0f : 1.0f);
            g.setColour(band.enabled == 1 ? colour : background);
            g.setFont(10.0f);
            g.drawText(juce::String(i + 1),
                       juce::Rectangle<float>(centre.x - 9.0f, centre.y - 7.0f, 18.0f, 14.0f),
                       juce::Justification::centred);
            if (band.enabled == 1)
            {
                g.setColour(colour);
                g.drawLine(centre.x - 6.0f, centre.y + 7.0f, centre.x + 6.0f, centre.y - 7.0f);
            }
            if (band.dynamic)
            {
                g.setColour(colour);
                g.drawText("D",
                           juce::Rectangle<float>(centre.x + 9.0f, centre.y - 13.0f, 10.0f, 12.0f),
                           juce::Justification::centred);
            }
        }
        if (drag == Drag::box)
        {
            g.setColour(accent.withAlpha(0.1f));
            g.fillRect(selectionBox);
            g.setColour(accent.withAlpha(0.7f));
            g.drawRect(selectionBox, 1.0f);
        }
        if (!hasSelection() && selected.none() && settings.listenBand == -1)
        {
            g.setColour(muted.withAlpha(0.7f));
            g.drawText("Double-click to add a band",
                       plot.withHeight(18.0f).withY(plot.getBottom() - 24.0f),
                       juce::Justification::centred);
        }
        if (settings.listenBand != -1)
        {
            g.setColour(accent);
            const auto title =
                settings.listenBand == -2
                    ? "FREE LISTEN  " + frequencyText(settings.listenFrequency) + "  " +
                          juce::String(settings.listenWidthOctaves, 2) + " oct  " +
                          juce::String(settings.listenGainDb, 1) + " dB  " +
                          reEqGroupName(settings.listenGroup)
                    : juce::String(settings.listenDetector ? "DETECTOR LISTEN" : "BAND LISTEN");
            g.drawText(title, plot.withHeight(16.0f).withY(plot.getY() + 18.0f),
                       juce::Justification::centred);
        }
    }
    g.setFont(10.0f);
    for (const auto frequency :
         {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f})
    {
        if (getWidth() < 600 && (frequency == 50.0f || frequency == 200.0f || frequency == 2000.0f))
            continue;
        g.setColour(muted);
        g.drawText(frequency < 1000.0f ? juce::String(frequency, 0)
                                       : juce::String(frequency / 1000.0f, 0) + "k",
                   juce::Rectangle<float>(xForFrequency(frequency) - 17.0f, plot.getBottom() + 2.0f,
                                          34.0f, 12.0f),
                   juce::Justification::centred);
    }
    const auto infoBand = settings.listenBand == -2 ? -1
                          : hoverBand >= 0          ? hoverBand
                                                    : selectedBand;
    if (infoBand >= 0 && settings.bands[static_cast<size_t>(infoBand)].enabled != 0)
    {
        const auto& b = settings.bands[static_cast<size_t>(infoBand)];
        const auto text = juce::String(infoBand + 1) + "  " + reEqShapeName(b.shape) + "  " +
                          frequencyText(b.frequency) + "  " + juce::String(b.gainDb, 1) +
                          " dB  Q " + juce::String(b.q, 2) + (b.enabled == 1 ? "  BYPASSED" : "");
        const auto width = std::min(plot.getWidth() - 10.0f, 350.0f);
        const juce::Rectangle<float> readout(plot.getX() + 5.0f, plot.getY() + 23.0f, width, 16.0f);
        g.setColour(background.withAlpha(0.88f));
        g.fillRoundedRectangle(readout, 3.0f);
        g.setColour(bandColour(infoBand));
        g.drawText(text, readout.reduced(3.0f, 0.0f), juce::Justification::left);
        if (b.dynamic && dynamicShape(b.shape) && active)
        {
            g.setColour(bandColour(infoBand));
            const auto dynamics =
                "Dynamic " + juce::String(analyzer->dynamicGain[static_cast<size_t>(infoBand)], 1) +
                " dB   Detect " +
                juce::String(analyzer->detectorLevel[static_cast<size_t>(infoBand)], 1) + " dBFS";
            g.drawText(dynamics,
                       juce::Rectangle<float>(
                           plot.getX() + 8.0f,
                           plot.getBottom() - (settings.showPiano ? 39.0f : 17.0f), 300.0f, 13.0f),
                       juce::Justification::left);
        }
    }
    if (analyzer->valid && active)
    {
        g.setColour(analyzer->outputPeak >= 0.0f ? juce::Colours::orange : muted);
        g.setFont(9.0f);
        g.drawText(
            "IN " + juce::String(analyzer->inputPeak, 1) + "  OUT " +
                juce::String(analyzer->outputPeak, 1) + " dBFS",
            juce::Rectangle<float>(plot.getRight() - 158.0f, plot.getY() + 4.0f, 150.0f, 12.0f),
            juce::Justification::right);
    }
}
float ReEqView::retainedOutputDb() const
{
    const auto* parameter = processor.parameters.getParameter(controlParameterId(slot, 9));
    return parameter != nullptr ? -18.0f + 36.0f * parameter->convertFrom0to1(parameter->getValue())
                                : 0.0f;
}
} // namespace megadsp::ui
