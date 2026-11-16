# Lessons

Every mistake or surprise, so it never happens twice. Newest first.
Format: **Symptom** -> cause -> fix. Add the short version to the Pitfalls list in AGENTS.md.

## 2026-11-08 (timeline test run)

- **Ctrl+Z after muting undid the previous edit instead of the mute** -> Tracktion's `AudioTrack::setMute/setSolo` write with a null UndoManager on purpose -> Session sets `IDs::mute` / `IDs::solo` through the Edit's UndoManager so Undo reverses the last thing the user did.
- **A test expectation was wrong, not the code** (3.1 s = 6.2 beats snaps to 8 beats, not 4) -> compute expected values from first principles in a comment next to the check.

## 2026-11-08 (timeline build)

- **Build "failed" after compiling cleanly** -> EzDAWsIt was open, so Windows couldn't replace the .exe -> build.bat now waits until EzDAWsIt is closed, and says so if the copy fails.

## 2026-11-08 (timeline review)

- **A periodic re-layout fought the user's drag** (the clip jumped back 5x a second) -> views never re-position while being dragged.
- **Full repaints on a timer cost CPU even when idle** -> periodic refresh compares a cheap per-clip fingerprint and only touches clips that changed; full repaint only on zoom/scroll.
- **Tracktion merges edits into the current undo transaction** -> every discrete user action starts one (`beginNewTransaction`), continuous gestures start one on drag start.
- **Toggling `looping` while recording ends the take** (TransportControl::stopIfRecording) -> refused while recording.
- **A deleted clip object stays alive until views rebuild**; `moveTo` on it would resurrect it -> Session refuses edits on clips with no track.

## 2026-11-08 (routing test run)

- **A test passed on its first run and failed on its second** -> it relied on a virtual MIDI device saved in the test engine's settings by the previous run, which the headless engine never loads -> tests set up everything they need themselves (create + explicit `rescanMidiDeviceList()`), and remove it afterwards. Rule: a test must pass on its first run AND every run after.

## 2026-11-08 (routing review)

- **`TrackOutput::getOutputDeviceID()` returns the device NAME, not its ID** -> a direct-out track showed a blank output -> use `getOutputDevice(false)->getDeviceID()`. Inputs are also matched by device ID (an audio and a MIDI input can share a name, e.g. "MC-707").
- **Tracktion writes the song to `outputChannelData[indexInDevice]`** -> anything else writing to outputs (the test beep) uses the same indexing, so it plays exactly where the song plays.
- **An extra `juce::AudioIODeviceCallback` is safe next to Tracktion's**: JUCE gives only the first callback the real buffers and sums the rest. Always fully write your buffer.
- **Audio-thread state written back each block can swallow a new request** -> pass requests through a separate `std::atomic<bool>` and `exchange()` it on the audio thread.
- **Headless tests can't cover hardware routing** (direct outs, main output, beep channels): checked by hand on the real interface.

## 2026-11-08 (Phase 2A review)

- **An offline-render test could never pass** (and its "silent" checks could never fail) -> `Renderer::measureStatistics` runs through `UIBehaviour::runTaskWithProgressBar`, which does nothing by default -> the test engine uses `TestUIBehaviour` (runs the task on a thread while pumping the message loop, like Tracktion's own TestRunner).
- **MIDI routing had no test** because the headless engine has no MIDI devices -> tests create a virtual MIDI input and check it targets and arms the new track.
- **View flags set through the Edit's UndoManager** let Ctrl+Z hide the plugin slots -> UI-only view state is set with a null UndoManager.
- **Async-loading plugins (macOS AU) aren't handled yet**: `addInstrumentTrack` rejects a plugin that has no instance right after creation. Fine for VST3 on Windows; fix before the macOS build.

## 2026-11-08 (first run of the self-tests on Windows)

- **Autosave never wrote anything in the tests** -> `Edit::hasChangedSinceSaved()` only works after Tracktion registers its undo listener via `callAsync` (first message loop turn) and updates asynchronously; tests run before any loop turn -> autosave compares a content fingerprint (state XML minus TRANSPORT) with the last write instead of trusting the flag.
- **One failing test crashed the whole run** (later tests dereferenced a song that an earlier failure had closed) -> every test block starts with `ensureSongIsOpen()` and never dereferences a missing song.
- **Crash logs had no function names** -> Release builds now keep a .pdb next to the exe.
- **"Restore" could replace a song with a blank one** -> Tracktion's `loadEditFromFile` treats an EMPTY file as a valid new song, and a crash mid-autosave can leave a 0-byte file -> a readable song must be non-empty and parse as an `EDIT`.
- **`File::setLastModificationTime` on a missing file can create an empty one on Windows** -> only call it on files that exist.

## 2026-11-08 (from the first independent code review)

- **Closing a song while Tracktion held a SaveInhibitor discarded unsaved work silently** (a postponed save was treated as "saved") -> `writeSongNow (true)` when closing: write an emergency copy and tell the user instead of postponing.
- **Re-opening the open song showed a false "didn't close properly" prompt** -> its own minute-by-minute autosave looked like a crash -> skip the check for the song that is already open.
- **A failed open could delete the only good copy of a song** -> the autosave was deleted before the song was checked -> check first; autosaves and backups are renamed aside with a timestamp, never deleted.
- **A failed save was only logged** -> the user could work for an hour into an unwritable folder -> every failed save writes an emergency copy and raises `Session::Listener::sessionProblem`, which the UI shows; songs are saved once on open to prove the folder is writable.
- **Imported audio stayed in its original location** -> deleting it later broke the song -> imports are copied into the song's "Imported Audio" folder.
- **Tests ran against the real app settings and audio device, and logged into Documents** -> could clash with the running app and delete real logs -> tests use a headless engine (`autoInitialiseDeviceManager() = false`) named "EzDAWsIt-Tests", and `--run-tests` points every folder at `app\test-run` before anything is written.
- **Two tests would have passed with the code broken** (close() saved anyway; a check that was always true) -> verify saves with a second, independent Session, and make every test able to fail.
- **Replacing a `ScopedMessageBox` inside its own callback destroys the running lambda** -> separate members for questions and errors; errors are shown via `MessageManager::callAsync`.
- **Dialogs holding engine references could outlive the engine on Windows shutdown** -> `Dialogs::closeAll()` before the engine is destroyed.
- **Saving during playback can cause dropouts** (the save lowers audio thread priority) -> save requests during playback wait until stop; autosave while playing uses Tracktion's background write.

## 2026-11-08

- **`songFile = {};` didn't compile** -> `juce::File` has several assignment operators, so `{}` is ambiguous -> write `juce::File()`.
- **Test constructor couldn't see `category`** -> inside a `juce::UnitTest` subclass, `category` names the base class's private member, not our constant -> write `ez::Tests::category`.
- **Autosave would always report failure** -> `EditFileOperations::saveTempVersion(false)` hands the write to a background thread and returns `false`, so failures can't be detected -> use `saveTempVersion(true)` (synchronous XML), and only while the transport is stopped, because the write briefly lowers audio thread priority.
- **Re-opening the open song could undo a crash recovery** -> the old Edit is saved on close *after* the new one is loaded, overwriting the restored file -> `Session::openSong` closes the current song first when the paths match.
- **Tracktion's JUCE submodule would fail to clone on a normal PC** -> `.gitmodules` uses `git@github.com:` (SSH needs keys) -> `build.bat` fetches the pinned JUCE commit over HTTPS itself.
- **Tracktion example `Components.h` can only be included once** -> it `#include`s `Components.cpp` at the bottom -> all example code is confined to `UI/TracktionExampleBridge.cpp`.

## 2026-11-09 — Scripted edits can overwrite each other
- A Python edit script replaced range A, then replaced range B, where B's anchors were looked up again in the edited text. The new code from A sat inside B, so it was silently deleted, and so was most of Session.cpp.
- Typecheck still passed, because the header declared the functions and `-fsyntax-only` doesn't link. Missing *definitions* show up only as link errors on Windows.
- Rules:
  - (1) Assert that replacement ranges don't overlap. Apply later ranges first.
  - (2) After a bulk edit, list the defined functions (grep `Session::`) and compare against the header.
  - (3) Keep the last committed copy (C:\NEW DAW) as the restore point.

## 2026-11-09 — 2B-2 review findings
- **A copied clip pasted into another song would be silent** -> a clip's audio source is stored relative to its song file -> the clipboard is emptied when a song closes.
- **A drop that made a new track took two undos, and the second also undid the previous edit** -> each Session command must call `beginUndoStep()` first, and a compound action (new track + imports) is one step -> `importAudioFilesAt`.
- **Async menu/dialog callbacks must not hold `te::Clip::Ptr`** -> if the song closes first, releasing the last Ptr runs the Clip destructor against a freed Edit -> capture `EditItemID` and look it up with `te::findClipForID`.
- **Track indexes held by views go stale after delete/move** -> state changes request a structure check on the next timer tick. Never rebuild synchronously: the caller may be the view being deleted.
- **`splitClips` only splits strictly inside a clip (1 ms margin)** -> use the same test before deciding what to split.
- **MIDI notes must be placed with `getTimeOfContentBeat`**, not clip start + note beat; otherwise trimmed clips draw notes in the wrong place.
- **`juce::Viewport` takes keyboard focus by default** and eats Home/End/arrows -> `setWantsKeyboardFocus(false)`.

## 2026-11-09 — Shell refactor review
- **The same action got a second entry point without the first one's guard:** the new app-bar output switch skipped Routing's "not while recording" check. Fix: guards live in Session (`Session::setMainOutput`); UI calls Session, never AudioEngine, for anything that changes state.
- **"Planned" uses `setEnabled(false)`, which the command manager undoes on command-bound buttons.** `Planned::mark` now asserts the button has no command.
- **Placeholder numbers in greyed widgets read as real values.** Planned readouts show "-".
- **Components that repaint at 30 Hz should be opaque.** Otherwise each frame also repaints the parent and everything drawn over it.
- **Don't hold a reference into a UI list across a plugin load.** A plugin can run the message loop, and a rebuild in that loop leaves the reference dangling. Copy the item first.
- **Quitting during a plugin scan waits for the plugin being scanned.** A plugin that hangs while scanning also hangs quitting. Known and accepted for now.

## 2026-11-09 — A test assumed how many tracks exist
- **Problem:** "moving off the last track is refused" moved clips +5 tracks, assuming fewer than 6 tracks. Earlier tests had added more, so the move was legal and happened. Four later tests then failed because the clips were on other tracks.
- **Fix:** tests derive limits from the current state (`getNumAudioTracks()`), never from a guess about what earlier tests left behind. A test that expects a refusal also checks that nothing changed, so a wrongly-accepted action fails right there instead of in later tests.

## 2026-11-09 — Devices panel review
- **Reordering plugins in the ValueTree updated the list but not the sound.** Tracktion's PluginList ignores child-order changes, so the audio graph keeps the old order. Fix: Session listens for plugin reorders on track states (which also covers undo/redo) and calls `edit->restartPlayback()`. Rule: after any change Tracktion might not watch (order, not add/remove), check what rebuilds the graph.
- **Offline renders don't prove what live playback does:** the Renderer builds a fresh graph from the state. Test the rebuild itself (a counter), not a render.
- **UI buttons captured a list index, so a double-click on Remove deleted two effects.** Buttons capture the item's ID and act only if the slot still holds that item.
- **Layout that depends on content size has to use the new content's size.** The scrollbar check measured the old chain width.

## 2026-11-11 — Piano roll review
- **JUCE calls mouseDoubleClick after the second mouseUp.** Anything started there that "stops on mouse-up" never stops. Guide notes from a double-click use Tracktion's autorelease.
- **A global "last click was in here" check must ignore other windows.** A ComboBox popup is its own top-level window; counting it as "outside" sent Delete to the arrangement and deleted the clip being edited. Compare `getTopLevelComponent()` first.
- **Item IDs restart in every song.** A view that remembers an EditItemID must forget it on `sessionSongClosing`, or it can find an unrelated clip in the next song.
- **An undo step that changes nothing makes the next Ctrl+Z undo the action before it.** Tests must not undo a command that may have been a no-op; drags begin their own step in Session (a `continuesGesture` flag), not in the UI.
- **Don't seek the transport on a plain click in an editor that also uses double-click:** the first click of every double-click seeks too.
- **A `Clip::Ptr` (or `auto` from `insertMIDIClip`) that outlives `closeSong()` crashes in `~Clip`.** It only showed up when unrelated changes moved memory around. Scope clip pointers in tests, or reset them before anything can close the song.

## Piano roll controls pass (2026-11)
- A python `s.replace(old, new)` where `old` came from a bad slice was an empty string and inserted text between every character (719k-line file). Always `assert 0 < a < b` and `assert s.count(old) == 1` before replacing.
- Swing is "fraction of half a grid step" (Session::quantiseMidiNotes). A test that expected a full half step was wrong; read the header comment before writing expected values.
- Snap code at triplet grids needs a small negative tolerance (-1e-9) before wrapping, or a note at the clip start lands one step late.
- Don't `std::exchange` a selection away before calling a Session command; keep it until the command succeeds so a refusal doesn't lose it.
- After mouse-up, a saved te::MidiNote* may be gone (undo mid-drag): prune, then re-select only if still present.
- Command active-state (Delete) must also consider the piano roll's edit focus, not only the timeline's selected clip.

## Drum machine (2D, 2026-11)
- A drag joined to the previous undo step when its first move changed nothing (Session returned early without beginUndoStep, then the view passed continuesGesture = true). Fix: start the step whenever continuesGesture is false, before any early return; JUCE never keeps an empty transaction.
- With swing on, checking "is this step on?" over [swungStart, swungStart + step) reaches into the next step. Keep the step range on the straight grid and pass where the hit goes separately (setDrumStep's placeAt).
- A copy-then-replace pattern command can't be used for cut + paste (the source is gone). Lift the hits into plain values first (Session::getDrumHits), then replaceDrumRange.
- A custom plugin's audio thread must not free memory: swap snapshots with try-locks, hand the old one back, and ask the message thread (triggerAsyncUpdate) to free it, or the next swap stalls.
- A text field that keeps focus while you click elsewhere (pads don't take focus) applies its text to whatever is selected *then*. Remember which item the field was showing, and commit it before the selection changes.
- Quantise with swing must look at the neighbouring lines too: a fully swung hit sits exactly between two straight lines, and plain rounding sends it to the next step. Ties go to the earlier line.
- Choke must start at the choking hit's sample, not at the start of the audio block (an open hat and a closed hat in one block would never sound).
- A "make + name" action is one undo step only if the name is set inside the same Session call (createMidiClip's name).
- Messages for problems the view finds itself must be passed explicitly: Session's last error is stale.
- Panel layouts: an optional column must never squeeze the main editor below a usable size, even when the user forces it on.

## Tempo, time signature, key, sections (2E, 2026-11)
- In Tracktion's default engine behaviour a beat is the time signature's bottom note: in 6/8 a "beat" is an eighth. Changing 4/4 to 6/8 while keeping clips on their beats doubles the speed of the song. x/8 is refused until it is designed properly (also tempo must be re-expressed).
- insertTempo doesn't remap clips. Inside a glide a new marker reshapes the glide, so do the insert and the value change inside one remap (EditTimecodeRemapperSnapshot), not "insert with the same tempo, then set with remap".
- Commit a drag at the snapped value you computed, not at a value re-read from the rounded pixel position.
- When moving or changing an earlier time signature, later changes must be put back on their bars, or they end up mid-bar.
- Grids that step by N beats miss bar lines in 3/4: step by whole bars once the grid is a bar or more (Session::getGridLines).
- A lane that never takes the keyboard can't see Esc in keyPressed: check KeyPress::isKeyCurrentlyDown during the drag.
- A component that grabs keyboard focus for its own Delete keeps it after clicks on non-focusing components: gate its keys on a ClickFocusTracker, or Delete hits the wrong thing.
- An "add, then type its value" action is one undo step only if the typed value continues the add's step.
- Tracktion's ClipTrack::insertSpace walks clips backwards and stops at the first one before the insert point, assuming the list is sorted; the list is only sorted asynchronously, so clips added out of order get left behind. Move the later clips with our own loop.
- deleteRegionOfTracks removes tempo, meter and key changes inside the range, including one exactly at its start: put back what the later music was playing at, in the same undo step.
- When a later change keeps its bar, "can't move past the next one" must compare bars, not the next one's old beat.
- Grid lines must restart at every bar line, or a 2-beat grid in 3/4 misses every other bar.

## Meters (P3, 2026-11)
- te::LevelMeasurer::Client::getAndClearOverload() returns the client's "please clear" flag (true at start and after every reset), not an overload. Take clips from the level.
- Reading a LevelMeasurer channel clears it: read each channel once, then copy for mono.
- Keep per-track view state by EditItemID, not index, or deleting one track wipes every track below it.
- Master plugins run before the master volume in Tracktion: a tap there is pre-fader.
- A tooltip whose text changes every frame never appears (TooltipWindow restarts its delay).
- New LevelMeasurer clients size their lists on the audio thread unless sized first (setOverload (7, false)).
- A new Tracktion edit's master volume starts at -3 dB (Edit::Options::defaultMasterVolumedB). Tests that check levels after the master volume must allow for it.
- A Button bound with setCommandToTrigger takes its toggle state from the command's isTicked: make the view switch a command and tick it, never set the button's state by hand.

## Mix view (P3, 2026-11)
- getPluginNamesOnTrack includes the Volume & Pan and Level Meter plugins every track has: never index it against getEffectInfo. Build insert lists from getInstrumentInfo + getEffectInfo.
- When a view hides the arrange, the arrange's editing commands (Delete, copy/paste, split, select) must do nothing, or they edit clips you can't see.
- An editable juce::Label wants keyboard focus, and a juce::Viewport eats arrow keys for scrolling: turn both off in the shell.
- A layout's minimum height must be the sum of its fixed rows plus a usable control; drop optional rows as the space shrinks.
- Work out whether a Viewport's scrollbar will show before sizing its content, or the content is a scrollbar too tall or short.
- Not every Session change notifies listeners (volume, rename): views that show those must also poll slowly.
- Tab reaches the key mappings even while a TextEditor has focus: commands bound to Tab must check for one.
- One LevelMeasurer can't give peak and RMS at once: in RMS mode clips go unseen.
- Tracktion's volume/pan parameter value catches up with the saved CachedValue asynchronously after an undo. Read volume and pan from the CachedValue (plugin->volume / ->pan), or an undo looks like it did nothing until the next message loop.

## Sends and buses (P3, 2026-11)
- Tracktion stores "this track plays into track N" by track NUMBER (TrackOutput, "track N"), resolved only when that text changes. Moving, adding or deleting a track, or an undo, leaves it pointing at the wrong track or none. Keep the bus's EditItemID on the source track and rewrite the number after every such change (Session::syncBusRouting), without the undo manager: undo replays the old text in the middle of its transaction, before the tracks are back in order.
- Rewriting the same text doesn't make Tracktion look the number up again (no property change): clear it, then write it.
- A send's pre/post is only its place in the plugin list (before or after VolumeAndPan). New effects must go before the first pre-fader send, or they land between the send and the fader.
- Tracktion's AuxSendPlugin::setMute parks the level at -100 dB, losing it: mute a send by bypassing the plugin.
- AuxSendPlugin and AuxReturnPlugin are in every track's plugin list: anything listing "effects" must skip them (and VolumeAndPan, LevelMeter).
- Returns must never play into a bus and never send: with buses able to send, either one makes a feedback loop in the audio graph.
- A copied return keeps its bus number: give the copy a free bus, or both play every send twice.
- A component that listens to its children's mouse events (addMouseListener (this, true)) also gets their wheel events: a strip that scrolls on the wheel scrolled the mixer while a fader under the mouse changed too.
- Every strip must reserve the same rows (sends included, empty on returns and the master) or the faders stop lining up.
- LookAndFeel_V4::drawRotarySlider shrinks the bounds by 10 px a side: an 18 px knob draws nothing, a 30 px one a 10 px arc. Our LookAndFeel draws knobs into their full bounds.
- juce::Slider::setVelocityModeParameters with userCanPressKeyToSwapMode = false ignores the modifier: "Shift for fine" needs true.
- Each strip sized from its own height picks its own rows: compute the rows once from the strip height and give the master the same height and bottom row.
- Edit::setAuxBusName writes names without the undo manager, but creates AUXBUSNAMES with it: undoing the first return detached that tree for good. Create it outside any undo step and derive the names from the return tracks.
- Match keys like [ and ] by the typed character as well: AltGr layouts send Ctrl+Alt and a different key code.
- Tests that check routing must check what is stored (the "track N" text), not only Tracktion's in-memory destination, or they pass without the code they test.

## Automation lanes (P3, 2026-11)
- Turning Read off leaves every automated control where the lane last put it, and playback has already written that value into the stored CachedValue (an async update), so there is no "value before automation" to go back to. Read off means: controls hold, and move by hand.
- Tracktion records automation only for tracks in touch/latch/write mode, and a new track is in read: with Write on, nothing recorded until we set touch (without the undo manager) when a gesture begins.
- A fader destroyed mid-drag (a rebuild) never gets onDragEnd: end its parameter gesture in the destructor, or Tracktion's gesture count stays up and that control never records again.
- Read and Write are saved with the song (the transport state): switch Write off when a song opens.
- With one point, a curve's value is that point everywhere: the first point on an empty lane also gets an anchor at the start with the control's value.
- Never seek the transport from a click in a lane that also takes double-clicks (the first click seeks).
- A track's row is no longer trackHeight tall: anything that maps y to a track must use TimelineViewState::topOfTrack / trackAt / rowHeight, never index * trackHeight.
- A control locked by automation must not open an error box on every drag step: Session::showHint puts the reason in the status bar instead.

## Built-in effects and the Vocal Chain (P3, 2026-11)
- te::Plugin's notifyListenersOfDeletion deletes the plugin's parameters: detach them from their CachedValues before calling it, and keep AutomatableParameter::Ptr (not raw pointers) like Tracktion's own plugins.
- Undo/redo changes a plugin's stored CachedValue but deliberately not its AutomatableParameter (what the DSP reads). Our EffectPlugin brings the parameter along in valueTreePropertyChanged while the undo manager is undoing; a test must check get(), not just the stored value.
- restorePluginStateFromValueTree must call updateFromAttachedValue too, as Tracktion's plugins do.
- A pitch shifter's two taps half a grain apart cancel when that is half a period: make the grain a whole number of periods (pitch-synchronous). Delay the dry path by the shifter's average delay and report it as latency, or switching between them flams.
- Fixed-size analysis (lags, windows) in samples breaks at 96/192 kHz: decimate to a fixed analysis rate. Spread a big search over the hop instead of running it inside one sample (CPU spikes at small buffers).
- A gate whose hold can be 0 must also be open while the level is above the threshold, not only while holding.
- A look-ahead limiter needs a true sliding maximum over the window; resetting a held peak drops louder peaks still in the delay.
- juce::Slider starts a drag on right-click unless something intercepts it: use MenuSlider (Widgets.h) when the knob has a menu.
- Test renders (Renderer::measureStatistics) skip the master plugins, the master fader included.
- Keep DSP in Engine/Dsp.h and test it natively (tools/dsp-tests): it caught a de-esser that made esses louder and a tuner that missed low notes at 96 kHz, before any build on Windows.
- Never keep a std::initializer_list as a member of a struct stored in a container: its array dies with the braced expression (ASan: stack-use-after-return). Use a bitmask or an array.
- A delay-line pitch shifter may only change its grain while a tap is at its (silent) jump; with both taps centred on a fixed delay, the other tap is unaffected. Changing it anywhere else clicked up to 460x the input's second difference.
- An undo that removes a property makes a CachedValue "use default", and Tracktion's parameter then writes the default back through the undo manager while it is undoing (a JUCE assertion). Write every control's property when the plugin is made, without the undo manager.
- A view's rebuild signature must include ids, not just names: two effects of the same name swapping places changed nothing it looked at, and every button on their cards silently stopped working.
- A card that reads "the effect at slot i" on a timer must check the slot still holds the same effect (its id), and a drag must end exactly the gesture it began (keep the ParamRef).
- Tracktion allows 16 plugins a track by default (fader, meter, instrument and sends included) and drops any more SILENTLY: insertPlugin returns nothing useful. We raise the limit (AudioEngine::Behaviour) and check the count after every insert.

## Export (Nov 2026)
- Tracktion only applies the master fader (MasterVolumePlugin) together with the master plugins
  (`createMasterPluginsNode`): a full-mix render needs `useMasterPlugins = true`, or the master fader is ignored.
  So "skip the mastering chain" can't be a render flag; it would have to bypass the master effects themselves.
- `EditRenderer` / `RenderQueue` hold an `Edit::ScopedRenderStatus (edit, false)`: it frees the playback graph and
  never builds it again. Session puts it back (`ensureContextAllocated`) once nothing is rendering.
- A stem job mutes the tracks that feed it (`ScopedTrackMuter`) for the length of the render: never save the song
  while an export runs (the timer skips autosave), or the mutes go into the file.
- The RenderQueue's callbacks come on the message thread; destroy the queue only outside them (clearExportStatus,
  the next export, closing the song).

## Master (Nov 2026)
- Order of the master plugin list: mastering chain, main meter, reference. The meter keeps measuring the mix while
  the reference plays (B), so loudness matching the reference uses a real mix reading. Tracktion's master fader comes
  after the whole list, so the reference's gain takes the master fader back out.
- A plugin's enabled switch (`setEnabled`) is an undoable CachedValue: for a temporary switch (an export without the
  chain) set `state[IDs::enabled]` with a nullptr undo manager, and put it back the same way.
- `PluginRenderContext::isRendering` tells an export from playback: the reference passes the mix in renders.

## Nov 2026: genres and Phase 5
- `DeviceManager::setAllWaveInputsToNumChannels` (mono inputs / stereo pairs) deletes and recreates every
  WaveInputDevice (an OwnedArray). Anything registered on an old device (a LevelMeasurer::Client) must check the
  device is still listed before detaching, and tracks lose inputs whose IDs changed.
- MIDI from controllers: `PhysicalMidiInputDevice::Listener` (MIDI thread). Hold the devices by their shared_ptr
  so removing the listener stays safe after the device list changes; hand messages to the message thread (FIFO).
- "The control the user just touched": `ParameterChangeHandler::Listener::pluginParameterChanged` (ignore
  isFollowingAutomation and calls off the message thread). `AutomatableParameter::setParameter` with
  sendNotification asserts the message thread.
- A gate in front of a big drive must close in decibels (`Gate::smoothInDb`): a linear release leaves -50 dB of
  hiss for half a second, and 60 dB of gain after it makes that loud.
- Loop recording makes takes on its own (WaveInputDevice splits the file per pass); `Clip::setCurrentTake` picks one.
  Punch uses the loop range as in/out (`Edit::recordingPunchInOut`).
- Time-stretch (clip transpose, auto tempo) needs a stretcher compiled in (SoundTouch here) and the clip's mode set
  off `disabled` (`TimeStretcher::defaultMode`).
- Sidechain: `CompressorPlugin::useSidechainTrigger` + `Plugin::setSidechainSourceID` + `guessSidechainRouting()`.
- Plug-in windows: many Tracktion settings are plain ValueTree properties, not parameters (4OSC waveShapeN, voiceMode,
  filterType, the fx On flags; Delay `length`; Chorus/Phaser values). List them as layout `settings`.
- After `UndoManager::undo()` a parameter attached to a CachedValue updates on the next message loop turn: tests check
  `plugin.state`, not `getCurrentValue()`.
- `FourOscPlugin::flushPluginStateToValueTree()` is private: call it through `te::Plugin&`.
- `juce::String (double, 0)` means "full precision", not "no decimals": round to int instead.
- Tracktion's Delay grows its buffer inside applyToBuffer when the time gets longer (allocates on the audio thread).
