# Keeper / megaDSP AU UI investigation

Date: 2026-10-30. Installed megaDSP AU examined: `2a99835`, JUCE 8.0.10.
Keeper was running its `.build/release-app/Keeper.app` executable. Keeper's
working tree contained existing changes, so source references describe the
inspected working tree, not a claim that its binary exactly matches HEAD.
No Keeper source, build configuration, or project was modified.

## Conclusion

Two megaDSP-side defects reproduce without running Keeper:

1. A worker-initialized JUCE message-thread identity prevents native module-menu
   completion on the macOS main thread. Keeper's background AU construction
   exposes this plug-in initialization assumption.
2. Ultra layout hides every module-insertion control, even when the rack is
   empty. This is independent of the host.

Both have been fixed in megaDSP source and covered by focused regressions.
There is no demonstrated Keeper source defect requiring a host patch for these
two mechanisms. The existing Keeper session itself was not manipulated to
reproduce the user's clicks; attribution combines retained logs, the code
trace, and an isolated deterministic reproduction.

## Evidence from the incident

Retained Keeper diagnostics and megaDSP diagnostics correlate:

| Local time | Keeper | megaDSP |
|---|---|---|
| 20:16:06.893 | Requests playback graph generation 40 after adding a plug-in | |
| 20:16:07.027 | Prepares generation 40 on worker thread `183445` | Creates instance 2 at 20:16:07.030; prepares at 48 kHz |
| 20:16:07.037 | Commits generation 40 on main thread `17332d` | |
| 20:16:11.452 | Prepares generation 44 on worker thread `181b2b` after a plug-in transfer | Creates instance 3 at 20:16:11.455; restores an empty schema-14 rack |
| 20:16:11.488 | Commits generation 44 on the main thread | Closes the displaced editor and destroys instance 2 |
| 20:16:41.130 | | Recalls another snapshot in instance 3 |
| 20:17:17 | Closes the plug-in editor without republishing | Logs editor closure |

There are no logged successful module additions between instance 3's empty-rack
restore and editor closure. However, the installed build does not log popup
selection results, so absence of topology events alone does not prove that a
selection callback was reached or explain why it failed.

AppKit also logged a JUCE view not conforming to `NSTextInputClient` at
20:15:43.370. This is not evidence of a mouse-selection failure; no causal
connection to either reproduced defect was established.

The diagnostics file also includes earlier pluginval runs and older build
headers. Those unrelated instance lifecycles were not treated as Keeper
failures. The relevant installed-build session starts at 18:35:57.978 with
commit `2a99835`.

## Module-menu failure: exact code trace

- Keeper's `Sources/DAWUI/PlaybackGraphUpdateQueue.swift:40-45` runs graph
  preparation in `Task.detached`.
- Keeper's `Sources/DAWEngine/DAWAudioEngine.cpp:5782-5793` constructs effect
  Audio Units with `AudioComponentInstanceNew` during that preparation.
- Keeper's `Sources/DAWUI/AudioUnitEditorWindowController.swift:5, 280-360`
  creates the Cocoa editor on the main actor.
- JUCE's AU wrapper owns a `ScopedJuceInitialiser_GUI`
  (`juce_audio_plugin_client_AU_1.mm:126`). On cold initialization,
  `juce_MessageManager.cpp:38-40` records the constructing thread as the
  message thread.
- On macOS, `juce_MessageQueue_mac.h:40-56` nevertheless delivers JUCE
  messages through the actual main CFRunLoop.
- `juce_Component.cpp:1497-1523` completes modal state only when
  `isThisTheMessageThread()` is true. Otherwise it asynchronously calls
  `exitModalState()` again. With a stale worker identity, main-loop delivery
  repeatedly takes that asynchronous branch.
- Popup selection calls this path through `juce_PopupMenu.cpp:525-550`.
  The completion callback used by megaDSP's
  `src/PluginEditor.cpp:4860-4912` therefore does not add the selected module.
  megaDSP's main-thread-gated undo recording is affected by the same identity.

The fix in `src/PluginProcessor.cpp:6902-6915` binds JUCE to the macOS main
thread before constructing the editor, only when `pthread_main_np()` confirms
that execution really is on that thread. It does not assign a background
thread as the UI thread or move AU/DSP preparation onto the main thread.
When correction is needed it emits:

```text
editor rebound message thread to macOS main instance=<id>
```

## Missing initial plus button

The previous Ultra branch in `MegaDSPAudioProcessorEditor::resized()` hid both
`chainInsertButtons` and `addButton` and reserved no add-button row. All other
layouts use chain insertion buttons, but those are deliberately hidden in
Ultra. An empty Ultra rack therefore had no visible way to add its first
processor.

`src/PluginEditor.cpp:4158-4186` now reserves a row and shows **+ ADD MODULE**
for empty and partially populated Ultra racks. It hides that control when all
eight slots are occupied. README describes the restored workflow.

The global widget-size preference read during investigation was `medium`.
That does not establish which mode was active when the user saw the missing
button. If absence recurs in S/M/L, capture the actual widget mode, editor
dimensions, selected snapshot, and active-slot count rather than assuming it
is the Ultra defect.

## Reproduction and validation

`tests/ProcessorTests.cpp:1212-1300` deliberately assigns JUCE's message-thread
identity to a worker, creates the native editor on the main thread, activates
the real **Compressor** menu item through its accessibility press action,
pumps the main run loop with a bounded deadline, and checks menu completion,
module insertion, and undo availability.

`tests/ProcessorTests.cpp:1302-1335` checks the Ultra add control in an empty
rack, after inserting a module, and after filling all eight slots.

The pre-fix reproductions failed the main-thread/menu and Ultra-control checks.
The corrected regressions and complete megaDSP test suite pass. VST3 and AU
were rebuilt and both pass pluginval strictness 10 with GUI tests skipped and
strictness 5 with editor tests enabled. Both bundle signatures verify deeply.
The corrected builds were not installed because Keeper remained running with
the AU loaded; do not overwrite that in-use bundle.

## Follow-up: current Keeper session at 22:27

At 22:27, Keeper PID 26141 maps the installed
`~/Library/Audio/Plug-Ins/Components/megaDSP.component/Contents/MacOS/megaDSP`.
That binary still embeds `2a99835`, was built at 18:17:07, and lacks the new
message-thread correction diagnostic string. The current diagnostics session
also identifies `2a99835`. Neither UI fix has been installed.

This incident cannot yet be classified as the same modal-completion failure.
The current log records EQ insertion at 22:21:51, Compressor additions at
22:23:04 and 22:24:50, and reordering at 22:26:27 and 22:26:31. At 22:26:41,
Keeper prepared graph generation 43 on worker `1a8a9b`; megaDSP instance 3
restored the three-module rack and the displaced instance 2/editor closed.
Successful earlier topology operations do not prove every subsequent UI
interaction works, and the installed build does not log menu selections.

A non-mutating two-second thread sample at 22:28:21 shows the main thread
mostly waiting in AppKit's event loop, with no sampled JUCE modal-exit loop.
It does not capture or rule out an intermittent menu failure. The sample is
retained as `keeper-ui-current-sample.txt` in the session artifacts.

Install the validated correction only with Keeper closed, restart Keeper, and
repeat the failing interaction before attributing this incident to a new
Keeper or megaDSP defect.

## Keeper-agent handoff

No Keeper source patch is requested based on this evidence. Keep background
graph/DSP preparation off the main thread. Do not globally move AU construction
and initialization onto the main actor as a workaround for this plug-in.

Useful host qualification would cold-load a real JUCE AU during background
graph preparation, create its Cocoa editor on the main actor, and activate a
native plug-in menu. Existing
`Tests/DAWUITests/AudioUnitNativeEditorTests.swift:1-185` uses injected `NSView`
factories with an Apple delay unit for sizing/lifetime checks; it does not
exercise this JUCE cold-start/modal path.

After installing the corrected AU with Keeper closed, reopen Keeper and check:

1. A freshly inserted megaDSP can add Compressor using its category menu.
2. The menu closes and the module appears; megaDSP undo removes that insertion.
3. Repeat after moving the host insert to another track and reopening its editor.
4. An empty Ultra rack exposes **+ ADD MODULE**; the control disappears only
   after filling eight slots.

Investigation artifacts are in the Copilot session's `files/` directory:
`megadsp-keeper-ui.log`, `keeper-ui-retained.log`, `keeper-au-system.log`,
`keeper-ui-before-fix.log`, `keeper-ui-after-fix.log`, and
`keeper-ui-full-tests.log`. Original megaDSP diagnostics are under
`~/Library/megaDSP/Diagnostics/`; Keeper diagnostics use the macOS unified log
subsystem `com.lightdaw`.
