# MixHouse DJ: plugin automation feasibility and build plan

Date: 2026-09-10. Steps 1 and 2 are complete. The realtime/offline adapters are implemented and under test; the application UI is still unchanged.

## Recommendation

Extend the current Audacity 4 fork. The inspected code provides clip gain automation, realtime effects, offline effect rendering, and some parameter metadata. A fresh fork would not remove the missing plugin automation work and would require reintegrating the DJ library.

The requested design is feasible, but drawing curves is only the interface layer. Reliable timing, effect identity, project storage, edit operations, and matching playback/export are the core work. Compatibility must be verified per plugin family; not every plugin exposes automatable controls.

Automation normally drives exposed plugin parameters over time (for example cutoff, mix, or output gain). It does not derive those parameters from the plugin's audio output. Audio-reactive modulation would be a separate feature.

## Findings in this checkout

| Area | Existing foundation | Required extension |
| --- | --- | --- |
| Volume automation | `src/automation/internal/au3/au3clipgaininteraction.cpp` reads/edits WaveClip's envelope, reports changes, and commits/cancels undoable gestures. `au3/libraries/au3-wave-track/WaveClip.cpp` copies, moves, stretches, and serializes it. | Keep this envelope separate from all plugin curves. Test editing operations instead of assuming new curves inherit their behavior. |
| Waveform and gain | `src/projectscene/view/tracksitemsview/au3/minmaxrmspainter.cpp` uses the clip envelope; `au3/libraries/au3-mixer/MixerSource.cpp` consumes envelope values. | Selecting/editing plugin automation must not write into the clip gain envelope or rescale the waveform as though it were volume. Existing volume still affects playback when hidden. |
| Curve UI | `src/projectscene/qml/Audacity/ProjectScene/tracksitemsview/ClipItem.qml` contains ClipGainModel and PolylinePlot. | Separate parameter models, parameter-specific ranges/units, selection and hit testing, dimmed noninteractive curves, and optional sublanes. |
| VST3 parameters | `src/effects/effects_base/effectstypes.h` and `src/effects/vst/internal/vstparameterextractorservice.cpp` expose names, IDs, ranges, units, flags, and value setting. | Use actual automatable parameters; distinguish continuous, logarithmic, stepped, and enumerated values. |
| Audio Units | `au3/libraries/au3-audio-unit/AudioUnitWrapper.cpp` already reads parameter lists/info. | `AudioUnitInstance` now resolves writable AU parameters, schedules sample-offset events, and restores baselines on finalize. Native AUDelay verification is host-dependent; the current machine exposes no AUDelay component, so that test reports SKIP. |
| Playback timing | The shared immutable evaluator now receives explicit per-sample project times, including loops, reverse playback and warps. VST3 queues and Audio Unit render notifications receive timed parameter events. | Complete host-level playback/export verification, especially effect tails and plugins with unusual latency or parameter metadata. Do not use a UI timer as the automation clock. |
| Export | `au3/libraries/au3-import-export/ExportPluginHelpers.cpp` includes track/master effects in its mixer. `au3/libraries/au3-mixer/EffectStage.cpp` processes effect settings per block. | Feed the same automation evaluation semantics into offline rendering, accounting for processing position and latency. |
| Identity/storage | `au3/libraries/au3-components/EffectInterface.cpp` assigns incrementing runtime instance IDs. `RealtimeEffectState.cpp::WriteXML` writes plugin ID and settings, not persistent instance UUIDs or timed curves. | Persistent identity per effect instance, parameter bindings, curves, schema versioning, and safe restoration of unavailable plugins. Two copies of the same plugin must remain distinct. |
| Panel widths | `src/appshell/qml/Audacity/AppShell/ProjectPage/ProjectPage.qml` sets effects width to 240 and fixes the combined panel minimum/maximum width. | Two coordinated resizers with persisted widths, minimum usable sizes, and aligned track headers/timeline. |

The Muse dependency also contains `AutomationControlNode` and an accepted volume/pan automation design (`muse/docs/adr/00103-Adding-automation-support-to-the-audio-engine.md`). However, this app registers `Au3Playback` in `src/playback/playbackmodule.cpp`. The Muse node is useful reference material, not evidence that this app already has arbitrary plugin automation wired into its active engine.

## Ownership and behavior

- Track effect chains own plugin instances. Song/clip automation uses clip-local time and references the intended effect instance and parameter. Horizontal movement carries the curves; track reordering preserves ownership.
- Preserve the current clip volume envelope. Showing another curve only changes editing focus, not which automation plays. Plugin processing may change audible level, but must not overwrite volume points.
- One selected curve is bright/editable. Other curves are dimmed or hidden and remain active. Labels, units, and selection markers complement color.
- Persist a distinct color per plugin instance. Use a Logic-inspired palette with light/dark contrast variants; keep the global interface accent independent.
- Expand the host effect row for parameter controls. Do not assume third-party plugin editor windows support arbitrary resizing.
- Master automation should use a separate project-time lane, not attach ambiguously to an individual song.

Important boundary: overlapping clips on one track share a track effect instance. Two independent values cannot simultaneously control the same parameter on that shared processor. The initial implementation should support independent processing on separate tracks and explicitly flag conflicting overlapping bindings, rather than silently choosing a winner. Truly independent effects on overlapping clips would require per-clip processing chains.

Before enabling cross-track moves, define an explicit binding transfer/remap workflow. Never bind by plugin display name or slot alone. Splits, trims, stretching, duplication, and undo must each have tested curve transformations; duplication must distinguish newly created identities from identities restored by undo.

## Implementation milestones

1. **Feasibility review — complete.** Source-level findings above; no claim of runtime validation.
2. **Automation data foundation.** Persistent effect-instance identity, separate parameter curves in clip-local time, versioned save/load, undo, copy/move/split/trim/stretch semantics. Unavailable plugins retain recoverable bindings and data.
3. **One working end-to-end parameter.** Start with an exposed continuous VST3 parameter and manually drawn curves in Read mode. Prove audible playback, offline export, seek, clip movement, and reopen using a deterministic test processor before widening scope.
4. **Audio Unit support.** Implement the parameter adapter and processing path, then verify AUBandpass/AUDelay. This is required for the user's illustrated workflow, not optional polish.
5. **Track-oriented interface.** Track automation button, effect/parameter selector, value and unit display, two resizable panels, expanded host effect rows. Keep the existing volume editing route accessible.
6. **Multiple curves and colors.** Selected/dimmed/hidden states, expandable sublanes, persistent per-instance colors, theme-aware palette. Visibility never acts as bypass.
7. **Recording modes.** Add native-editor/host control gesture capture and timestamping for Touch and Latch. Add Write only with clear overwrite behavior. Current begin/end-edit methods are not a complete automation recorder.
8. **Regression and deployment.** Verify a compatibility matrix, then build and validate the actual target behind `MixHouse DJ 2 (dev).app`. Preserve user projects and the DJ library. Do not replace an app while it has unsaved work.

## Updated scope: clip Bezier automation (September 11)

The user's later instructions supersede the recording-mode proposal below:

- The effect row uses the graph icon and an explicit AU/VST3 parameter selector, not Read/Touch/Latch/Write controls. Unsupported built-in effects are disabled with the requested compatibility tooltip.
- WaveClip owns version-2 segment data (sequence-relative endpoints plus curvature). The legacy linear envelope is a compatibility cache, not the clip's volume envelope. XML and project-binary round trips preserve curvature.
- A QQuickPaintedItem draws the same quadratic used by the evaluator. Option-drag bends segments; selected-range dragging creates a four-boundary-node shelf; Escape cancels and double-click inserts a node.
- Control-thread linearization bounds normalized interpolation error to 1e-5. Immutable snapshots retain trimmed playback bounds; no Bezier evaluation occurs in the audio callback.
- AU/VST3 adapter-reported parameter interactions select the visible curve. Parameters without notifications remain available through the explicit menu. This is focus selection, not gesture recording.
- Editing currently requires stopped playback: live immutable-snapshot replacement is not implemented. Master automation is excluded from clip-bound lanes. Other curves remain stored and active while hidden.

Verification on September 11: the full `audacity` build succeeded (`/tmp/mixhouse-bezier-build7.log`). Standalone math and Qt painting/gesture tests pass. All data-suite cases pass, including real WaveClip XML/binary serialization, duplicate/split and volume isolation, Apple AUDelay rendered-audio automation, and native VST3 parameter queues. The AU test uses independent reference samples and separate input channels because the host permits in-place processing. These results do not establish universal plugin compatibility.

The exact Desktop dev launcher was verified to run the rebuilt executable. After the library-open wait cleared, the project and library displayed normally. In the running app, Classic EQ's parameter selector displayed a Band 2 Gain curve; dragging the curve changed its position and produced a Plugin automation history entry. Option-drag and shelf construction have automated Qt coverage; native-editor click-and-view behavior across third-party plugins and live-playback editing remain outside this verified checkpoint.

## Earlier implementation checkpoint (historical)

Step 2 is implemented in the working tree:

- `au3/libraries/au3-wave-track/ClipParameterAutomation.h/.cpp` stores versioned, clip-relative normalized curves keyed by persistent effect-instance UUID plus parameter ID. It validates malformed data, refuses unsafe/conflicting pastes, and supports movement, stretching, insertion, collapse, and copy/paste transformations.
- `WaveClip` serializes this collection separately from its existing gain envelope and carries it through clip copies, cut lines, tempo/rate changes, and edits. Selecting or editing a plugin curve therefore cannot replace waveform amplitude automation.
- `RealtimeEffectState` now generates and serializes a persistent `instanceuuid`. Undo snapshots retain it; explicit insertion copies can request a fresh identity. Legacy effect XML without the attribute receives a new identity.
- An opt-in isolated test target (`MIXHOUSE_BUILD_AUTOMATION_DATA_TESTS=ON`) exercises standalone XML, Audacity binary project serialization, malformed-data refusal, multiple parameters, duplicate-instance identity, clip transformations, cut restoration, stereo clips, volume isolation, overlap evaluation, block consistency, serial latency, and the native VST3 queue. All deterministic groups pass on this host; the Apple AUDelay integration test reports SKIP because no AUDelay component is installed.

The test target is intentionally opt-in and does not alter the normal app build. The evaluator and VST3/AU delivery paths are now present. Remaining work is host-level playback/export verification and the track UI/recording modes; no UI timer or current-value write is used for automation timing.

## Realtime/offline adapter checkpoint

- `ParameterAutomationSnapshot` is the shared immutable evaluator and reports overlap conflicts instead of silently selecting a winner.
- VST3 queues receive per-sample points with baseline fallback, stepped quantization, and controller/read-only validation.
- Audio Unit instances resolve writable parameters and schedule frame-offset events from the render notification callback; baselines are restored when processing ends.
- Playback and realtime effect code carry explicit project timestamps through loops, seeks, reverse playback, and serial effect latency.

## UI checkpoint

- `WaveTrackItem.qml` now exposes a compact, themed per-track automation-focus button beside mute/solo. It opens the existing effects panel without toggling or replacing clip gain automation.
- `TracksPanel.qml` adds a bounded horizontal effects-panel resize handle (220–520 px).
- `RealtimeEffectListItem.qml` has an expandable automation control surface with Read/Touch/Latch/Write selection. Parameter-lane creation remains disabled until the parameter metadata model is connected, preventing a UI control from implying that a curve was saved when no curve editor is attached yet.
- Automation mode is now stored on `RealtimeEffectState`, copied with undo/duplicate semantics, and serialized as `automationmode`; changing the mode therefore survives save/reopen even while curve recording is still a separate engine milestone.
- Effect-row colors are derived from the persisted effect-instance UUID, so the same effect keeps its color after reordering or reopening.
- The opt-in target is registered with CTest; run `ctest --test-dir build/src/au3wrap/au3-libraries/au3-wave-track -R mixhouse-automation-data-tests --output-on-failure` for the repeatable regression gate.

## Acceptance gates

- Volume envelope points and waveform behavior remain unchanged by plugin-curve selection/editing.
- Two tracks with distinct curves produce distinct expected processing; two instances of one plugin do not exchange curves after reordering or reopening.
- Clip moves, splits, trims, stretches, duplicates, and undo/redo preserve the specified time relationship.
- Playback and export agree within a defined tolerance using a deterministic processor; test multiple sample rates/block sizes, seek, looping, stop/restart, bypass, and effect tails.
- Hidden/dimmed curves continue processing; dimmed curves cannot accidentally receive edits.
- Missing plugins retain automation data; incompatible cross-track bindings and overlap conflicts are visible and recoverable.
- Existing projects without automation open unchanged. New data survives save/reopen. No UI-thread dependency or unbounded allocation/locking in the realtime processing path.

## Logic references

Apple distinguishes track-time automation from region-attached automation. The requested “move the song and its curves together” behavior is best represented here by clip-relative storage: [Track automation vs. region automation](https://support.apple.com/en-ie/guide/logicpro/lgcp967649d0/mac).

Read, Touch, and Latch have different playback/recording semantics; copying their menu labels is not sufficient: [Choose automation modes](https://support.apple.com/guide/logicpro/choose-automation-modes-lgcpb1a6ab26/mac).

This report is an implementation proposal grounded in the inspected source, not a completed feature or a guarantee of universal plugin compatibility.
