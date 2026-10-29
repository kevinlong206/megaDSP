#include "PluginProcessor.h"
#include "ui/ReEqView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace
{
juce::MouseEvent reEqMouse(juce::Component& target, juce::Point<float> point,
                           int modifiers = juce::ModifierKeys::leftButtonModifier,
                           juce::Point<float> origin = {}, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            point,
            juce::ModifierKeys(modifiers),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &target,
            &target,
            now,
            dragged ? origin : point,
            now,
            1,
            dragged};
}

class ReEqViewTests final : public juce::UnitTest
{
  public:
    ReEqViewTests() : juce::UnitTest("Native ReEQ2 interface", "megaDSP") {}

    void runTest() override
    {
        auto processor = std::make_unique<MegaDSPAudioProcessor>();
        processor->addModule(megadsp::ModuleType::equalizer);
        megadsp::ReEqSettings empty;
        empty.native = true;
        processor->setReEqSettings(0, empty, false);
        auto view = std::make_unique<megadsp::ui::ReEqView>(*processor, 0);
        // isShowing() requires a native peer, not just setVisible(true).
        // Keep this test window offscreen and unable to receive real input.
        view->setBounds(-10000, -10000, 640, 280);
        view->addToDesktop(juce::ComponentPeer::windowIsTemporary
                           | juce::ComponentPeer::windowIgnoresMouseClicks
                           | juce::ComponentPeer::windowIgnoresKeyPresses);
        view->setVisible(true);

        beginTest(
            "Blank double-click creates the source band; node double-click bypasses, Alt deletes");
        const auto point = view->plotAreaForTesting().getCentre().translated(25.0f, -20.0f);
        view->mouseDoubleClick(reEqMouse(*view, point));
        auto state = processor->reEqSettings(0);
        expectEquals(state.bands[0].enabled, 2);
        expectEquals(view->selectedBandForTesting(), 0);
        expect(state.bands[0].shape == megadsp::ReEqShape::bell);
        expect(state.bands[0].gainDb > 0.0f);
        const auto originalGain = state.bands[0].gainDb;
        view->mouseDoubleClick(reEqMouse(*view, view->bandPointForTesting(0)));
        state = processor->reEqSettings(0);
        expectEquals(state.bands[0].enabled, 1);
        expectWithinAbsoluteError(state.bands[0].gainDb, originalGain, 0.0001f);
        view->mouseDoubleClick(
            reEqMouse(*view, view->bandPointForTesting(0),
                      juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
        expectEquals(processor->reEqSettings(0).bands[0].enabled, 0);
        expect(processor->undo());
        expectEquals(processor->reEqSettings(0).bands[0].enabled, 1);

        beginTest(
            "A multi-update graph drag is one undo transaction and preserves host automation");
        processor->setReEqSettings(0, empty, false);
        view->refresh();
        view->mouseDoubleClick(reEqMouse(*view, point));
        auto* hostOutput = processor->parameters.getParameter(megadsp::controlParameterId(0, 9));
        expect(hostOutput != nullptr);
        if (hostOutput != nullptr)
            hostOutput->setValueNotifyingHost(0.65f);
        const auto beforeDrag = processor->reEqSettings(0);
        const auto origin = view->bandPointForTesting(0);
        view->mouseDown(reEqMouse(*view, origin));
        view->mouseDrag(reEqMouse(*view, origin.translated(12.0f, -6.0f),
                                  juce::ModifierKeys::leftButtonModifier, origin, true));
        view->mouseDrag(reEqMouse(*view, origin.translated(34.0f, -13.0f),
                                  juce::ModifierKeys::leftButtonModifier, origin, true));
        view->mouseUp(reEqMouse(*view, origin.translated(34.0f, -13.0f), 0, origin, true));
        const auto afterDrag = processor->reEqSettings(0);
        expect(afterDrag.bands[0].frequency > beforeDrag.bands[0].frequency);
        expect(afterDrag.bands[0].gainDb > beforeDrag.bands[0].gainDb);
        expect(processor->undo());
        expect(processor->reEqSettings(0) == beforeDrag);
        expect(processor->redo());
        expect(processor->reEqSettings(0) == afterDrag);
        if (hostOutput != nullptr)
            expectWithinAbsoluteError(hostOutput->getValue(), 0.65f, 0.00001f);
        view->refresh();

        beginTest("Command-drag changes frequency and Q without changing gain; wheel changes Q");
        const auto beforeQ = processor->reEqSettings(0);
        const auto qOrigin = view->bandPointForTesting(0);
        const auto commandDrag =
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier;
        view->mouseDown(reEqMouse(*view, qOrigin, commandDrag));
        view->mouseDrag(
            reEqMouse(*view, qOrigin.translated(18.0f, -24.0f), commandDrag, qOrigin, true));
        view->mouseUp(reEqMouse(*view, qOrigin.translated(18.0f, -24.0f),
                                juce::ModifierKeys::commandModifier, qOrigin, true));
        const auto afterQ = processor->reEqSettings(0);
        expect(afterQ.bands[0].frequency > beforeQ.bands[0].frequency);
        expect(afterQ.bands[0].q > beforeQ.bands[0].q);
        expectWithinAbsoluteError(afterQ.bands[0].gainDb, beforeQ.bands[0].gainDb, 0.0001f);
        juce::MouseWheelDetails wheel{};
        wheel.deltaY = 0.25f;
        view->mouseWheelMove(reEqMouse(*view, view->bandPointForTesting(0), 0), wheel);
        expect(processor->reEqSettings(0).bands[0].q > afterQ.bands[0].q);

        beginTest(
            "Box selection edits both bands and split copies preserve dynamic detector state");
        auto multi = empty;
        multi.bands[0].enabled = 2;
        multi.bands[0].frequency = 300.0f;
        multi.bands[0].gainDb = 4.0f;
        multi.bands[0].dynamic = true;
        multi.bands[0].externalSidechain = true;
        multi.bands[0].detectorLinked = false;
        multi.bands[0].detectorFrequency = 700.0f;
        multi.bands[1] = multi.bands[0];
        multi.bands[1].frequency = 1800.0f;
        processor->setReEqSettings(0, multi, false);
        view->refresh();
        const auto boxOrigin = view->plotAreaForTesting().getTopLeft().translated(3.0f, 3.0f);
        const auto boxEnd = view->plotAreaForTesting().getBottomRight().translated(-3.0f, -3.0f);
        view->mouseDown(reEqMouse(*view, boxOrigin));
        view->mouseDrag(
            reEqMouse(*view, boxEnd, juce::ModifierKeys::leftButtonModifier, boxOrigin, true));
        view->mouseUp(reEqMouse(*view, boxEnd, 0, boxOrigin, true));
        expect(view->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
        state = processor->reEqSettings(0);
        expectWithinAbsoluteError(state.bands[0].gainDb, 4.5f, 0.0001f);
        expectWithinAbsoluteError(state.bands[1].gainDb, 4.5f, 0.0001f);
        view->duplicateSelectionForTesting(true);
        state = processor->reEqSettings(0);
        expect(state.bands[0].group == megadsp::ReEqGroup::side);
        expect(state.bands[1].group == megadsp::ReEqGroup::side);
        expect(state.bands[2].group == megadsp::ReEqGroup::mid);
        expect(state.bands[3].group == megadsp::ReEqGroup::mid);
        expect(state.bands[2].dynamic && state.bands[2].externalSidechain &&
               !state.bands[2].detectorLinked);
        expectWithinAbsoluteError(state.bands[2].detectorFrequency, 700.0f, 0.0001f);

        beginTest("Shift auditions a band and deactivation reliably releases transient listen");
        expect(view->isShowing(), "Audition requires a non-minimised native component peer");
        view->setVisualizationActive(true);
        const auto listenPoint = view->bandPointForTesting(1);
        view->mouseMove(reEqMouse(*view, listenPoint, juce::ModifierKeys::shiftModifier));
        expect(processor->reEqSettings(0).listenBand >= 0);
        view->setVisualizationActive(false);
        expectEquals(processor->reEqSettings(0).listenBand, -1);
        expect(!processor->reEqSettings(0).listenDetector);

        beginTest("Freeform Shift audition follows frequency, width and drag level with all 16 "
                  "bands occupied");
        expect(view->isShowing(), "Freeform audition requires a non-minimised native component peer");
        auto occupied = empty;
        for (int i = 0; i < megadsp::reEqBandCount; ++i)
        {
            auto& b = occupied.bands[static_cast<size_t>(i)];
            b.enabled = 2;
            b.frequency = 60.0f * std::pow(1.4f, static_cast<float>(i));
        }
        occupied.bands[0].group = megadsp::ReEqGroup::mid;
        processor->setReEqSettings(0, occupied, false);
        view->refresh();
        view->setVisualizationActive(true);
        view->mouseMove(
            reEqMouse(*view, view->bandPointForTesting(0), juce::ModifierKeys::shiftModifier));
        expect(processor->reEqSettings(0).listenGroup == megadsp::ReEqGroup::mid);
        const auto area = view->plotAreaForTesting();
        const juce::Point<float> freePoint(area.getX() + area.getWidth() * 0.3f,
                                           area.getY() + area.getHeight() * 0.2f);
        view->mouseMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier));
        state = processor->reEqSettings(0);
        expectEquals(state.listenBand, -2);
        expectWithinAbsoluteError(state.listenFrequency, 10.0f * std::pow(3000.0f, 0.3f), 0.01f);
        expectWithinAbsoluteError(state.listenGainDb, 0.0f, 0.0001f);
        expect(state.listenGroup == megadsp::ReEqGroup::mid);
        expect(state.bands == occupied.bands);
        expect(state.toValueTree().isEquivalentTo(occupied.toValueTree()));
        wheel.deltaY = 0.2f;
        view->mouseWheelMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier), wheel);
        expect(processor->reEqSettings(0).listenWidthOctaves > state.listenWidthOctaves);
        wheel.deltaY = 100.0f;
        view->mouseWheelMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier), wheel);
        expect(processor->reEqSettings(0).listenWidthOctaves <= 6.931f);
        wheel.deltaY = -100.0f;
        view->mouseWheelMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier), wheel);
        expect(processor->reEqSettings(0).listenWidthOctaves >= 0.231f);
        expect(processor->reEqSettings(0).listenWidthOctaves < 0.232f);
        const auto shiftDrag =
            juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier;
        const juce::Point<float> freeDrag(area.getX() + area.getWidth() * 0.7f,
                                          area.getY() + area.getHeight() * 0.25f);
        view->mouseDown(reEqMouse(*view, freePoint, shiftDrag));
        view->mouseDrag(reEqMouse(*view, freeDrag, shiftDrag, freePoint, true));
        state = processor->reEqSettings(0);
        expectEquals(state.listenBand, -2);
        expectWithinAbsoluteError(state.listenFrequency, 10.0f * std::pow(3000.0f, 0.7f), 0.01f);
        expectWithinAbsoluteError(state.listenGainDb, 9.0f, 0.01f);
        expect(state.bands == occupied.bands);
        view->mouseUp(
            reEqMouse(*view, freeDrag, juce::ModifierKeys::shiftModifier, freePoint, true));
        expectWithinAbsoluteError(processor->reEqSettings(0).listenGainDb, 0.0f, 0.0001f);
        view->modifierKeysChanged(juce::ModifierKeys());
        expectEquals(processor->reEqSettings(0).listenBand, -1);
        view->mouseMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier));
        view->setVisible(false);
        expectEquals(processor->reEqSettings(0).listenBand, -1);
        view->setVisible(true);
        view->mouseMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier));
        view->setVisualizationActive(false);
        view->mouseMove(reEqMouse(*view, freePoint, juce::ModifierKeys::shiftModifier));
        expectEquals(processor->reEqSettings(0).listenBand, -1);
        expect(processor->reEqSettings(0).bands == occupied.bands);

        beginTest(
            "Editable musical units reach processor state and survive native state serialization");
        view->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        view->mouseDown(reEqMouse(*view, view->bandPointForTesting(0)));
        view->mouseUp(reEqMouse(*view, view->bandPointForTesting(0), 0));
        bool foundFrequency = false;
        for (auto* child : view->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child);
                label != nullptr && label->getTitle() == "Freq")
            {
                label->setText("1.25 kHz", juce::sendNotificationSync);
                foundFrequency = true;
                break;
            }
        expect(foundFrequency);
        state = processor->reEqSettings(0);
        const auto band = view->selectedBandForTesting();
        expect(band >= 0);
        if (band >= 0)
            expectWithinAbsoluteError(state.bands[static_cast<size_t>(band)].frequency, 1250.0f,
                                      0.01f);
        juce::MemoryBlock saved;
        processor->getStateInformation(saved);
        auto restored = std::make_unique<MegaDSPAudioProcessor>();
        restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        expect(restored->reEqSettings(0).toValueTree().isEquivalentTo(state.toValueTree()));
        expectEquals(restored->reEqSettings(0).listenBand, -1);

        beginTest("Resonant cut vertical gestures edit Q; Butterworth gestures never edit unused "
                  "gain or Q");
        auto cut = empty;
        cut.bands[0].enabled = 2;
        cut.bands[0].shape = megadsp::ReEqShape::lowCut;
        cut.bands[0].slope = 1;
        cut.bands[0].frequency = 800.0f;
        cut.bands[0].gainDb = 7.0f;
        processor->setReEqSettings(0, cut, false);
        view->refresh();
        const auto cutOrigin = view->bandPointForTesting(0);
        view->mouseDown(reEqMouse(*view, cutOrigin));
        view->mouseDrag(reEqMouse(*view, cutOrigin.translated(10.0f, -20.0f),
                                  juce::ModifierKeys::leftButtonModifier, cutOrigin, true));
        view->mouseUp(reEqMouse(*view, cutOrigin.translated(10.0f, -20.0f), 0, cutOrigin, true));
        state = processor->reEqSettings(0);
        expect(state.bands[0].q > cut.bands[0].q);
        expectWithinAbsoluteError(state.bands[0].gainDb, 7.0f, 0.0001f);
        cut.bands[0].shape = megadsp::ReEqShape::lowCutButterworth;
        processor->setReEqSettings(0, cut, false);
        view->refresh();
        const auto bwOrigin = view->bandPointForTesting(0);
        view->mouseDown(reEqMouse(*view, bwOrigin));
        view->mouseDrag(reEqMouse(*view, bwOrigin.translated(10.0f, -20.0f),
                                  juce::ModifierKeys::leftButtonModifier, bwOrigin, true));
        view->mouseUp(reEqMouse(*view, bwOrigin.translated(10.0f, -20.0f), 0, bwOrigin, true));
        state = processor->reEqSettings(0);
        expect(state.bands[0].frequency > cut.bands[0].frequency);
        expectWithinAbsoluteError(state.bands[0].gainDb, 7.0f, 0.0001f);
        expectWithinAbsoluteError(state.bands[0].q, cut.bands[0].q, 0.0001f);

        beginTest("Compact controls stay inside S/M/L cards with a 170-pixel response graph");
        for (const auto width : {480, 640, 800})
        {
            for (const auto height : {260, 280})
            {
                view->setSize(width, height);
                expect(view->plotAreaForTesting().getHeight() >= 170.0f);
                for (auto* child : view->getChildren())
                    if (child->isVisible())
                        expect(view->getLocalBounds().contains(child->getBounds()));
            }
        }
    }
};

ReEqViewTests reEqViewTests;
} // namespace
