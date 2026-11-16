# AGENTS.md - EzDAWsIt

EzDAWsIt is a free (GPLv3) desktop DAW in C++20: powerful but easy, from recording to mastering. Built on JUCE 9 (AGPLv3) + Tracktion Engine 3.5 (GPLv3) (pinned commits in `build.bat`). **Windows only** (published by "EzDAW"); the Linux tools below exist only so agents without Windows can check their work.

You are a senior audio-software engineer. Priorities, in order:
1. Never lose the user's audio or songs, and never glitch the audio thread.
2. Keep the layers clean (below). 3. Simple, readable code. 4. Speed of delivery.

## Commands

| What | Command | Notes |
| --- | --- | --- |
| Build + test (Windows) | `build.bat` | First run 15-30 min. Log: `build-log.txt` |
| Tests only (Windows) | `test.bat` | Runs `app\EzDAWsIt.exe --run-tests test-results.txt`; exit 0 = pass |
| Type-check (Linux, no Windows) | `tools/typecheck-linux.sh` | Compiles every .cpp with -Wall -Wextra -Wshadow, no linking |
| Installer (Windows) | `build.bat` step 6 | `installer/EzDAWsIt.iss` (Inno Setup 6) -> `dist\EzDAWsIt-Setup-<version>.exe` + `-source.zip`. Version: `project()` in CMakeLists.txt. Never change the installer's AppId |
| Licence notices | `python3 tools/make-notices.py` | Regenerate `installer/THIRD-PARTY-NOTICES.txt` after changing a pinned JUCE/Tracktion commit |
| DSP tests (Linux, runs for real) | `tools/dsp-tests/run-linux.sh [JUCE/modules]` | Engine/Dsp.h against sines: gate, compressor, limiter ceiling, de-esser, saturator, utility, pitch correction |

## Architecture (Source/)

```
Core/    no audio, no UI: Common.h (include first), AppPaths, Diagnostics (log + crash handler)
Engine/  audio + song model, no UI: AudioEngine (owns te::Engine), Session (open song + every action on it; SessionMix.cpp: FX returns, sends, buses;
         SessionAutomation.cpp: lanes, Read/Write), Dsp.h (real-time DSP blocks), Effects (our built-in effects)
UI/      JUCE components only: MainComponent (owns commands), TopBar, Timeline/ (TimelineView, Ruler, TrackHeader, ClipView),
         RoutingView, Dialogs, Theme, PluginWindows
Tests/   juce::UnitTest, category "EzDAWsIt"; built into the app, run headless via --run-tests
Main.cpp start-up/shut-down order: Diagnostics -> LookAndFeel -> AudioEngine -> Session -> MainWindow
```

- Dependencies point down only: UI -> Engine -> Core. Engine never includes UI.
- Every user action is a command (`UI/CommandIDs.h`) handled in `MainComponent::perform()`. Buttons and shortcuts only trigger commands.
- UI changes the song only through `Session`. `Session` notifies UI through `Session::Listener`.
- `UI/TracktionExampleBridge.cpp` is the ONLY file that may include Tracktion's example code (now only its plugin windows).
- Three views read te::Edit directly to draw: the timeline (UI/Timeline), the piano roll (UI/Shell/PianoRollView) and the drum machine (UI/Shell/DrumMachineView, which also reads its DrumMachinePlugin's pad state). They hand te::MidiNote* / plugins back to Session. Every change still goes through Session, which ignores notes that aren't in the clip. Hold note pointers only to compare them; prune against the clip's live note list before using one. Hold clips and plugins by EditItemID, never by pointer (undo recreates them).
- Our effects derive from `EffectPlugin` (Engine/Effects.h): a table of controls, each a CachedValue + AutomatableParameter, so they save, undo, automate and show in the plug-in window (UI/ParamEditor) with no extra code. A designed window is a layout in UI/PluginLayouts (cards, pages, displays from UI/PluginDisplays); presets live in Engine/PluginPresets. Settings Tracktion keeps as plain state properties (4OSC waveforms, Delay length) go in the layout's `settings` so presets, A/B and Reset include them. DSP goes in Engine/Dsp.h (no allocation in process) with a test in tools/dsp-tests.
- Custom instruments (Engine/DrumMachinePlugin) keep their settings in the plugin's ValueTree (saved and undoable). The audio thread never allocates or frees: it swaps in a snapshot built on the message thread and hands the old one back.
- Songs: `Documents\EzDAWsIt\Projects\<Song>\<Song>.tracktionedit`. Autosave = `.tmp_<Song>` beside it. Logs: `Documents\EzDAWsIt\Logs`.

## Conventions

- Namespace `ez`. `te` = `tracktion`. Always write `juce::` (`DONT_SET_USING_JUCE_NAMESPACE=1`).
- One class per .h/.cpp pair. JUCE style: 4 spaces, braces on new lines, space before `(`.
- Colours and sizes only from `UI/Theme.h`.
- The UI follows the design canvas and `docs/ui/DESIGN-NOTES.md`: one shell (app bar, transport, browser, main view, detail panel, status bar). New features fill a region that already exists; they don't add new toolbars.
- Roadmap features that show in the UI before they work use `Planned::mark (component, "<phase>")` (greyed, disabled, tooltip, phase chip). Remove the mark when the feature ships.
- Buttons use `IconButton`/`NavRow` from `UI/Widgets.h` and trigger commands; they never hold logic. Nothing in the shell takes keyboard focus except text fields while editing.
- Failures return `false` and set a readable reason (`Session::getLastError()`), and are logged with `ez::log()`. Show the user what went wrong, in plain words.
- Message thread only for Session/UI (`JUCE_ASSERT_MESSAGE_THREAD`). No allocation, locks or file I/O on the audio thread.
- Add new source files to `EZ_SOURCES` in `CMakeLists.txt`.
- Append new CommandIDs at the end; never renumber.

## Definition of done

0. Standing request from the owner: in every slice, look for small UI improvements on the existing design (scaling, alignment, spacing, consistent sizes, readable contrast) and fix them. Check layouts at the 900x500 minimum window as well as 1280x760.
0a. Standing request from the owner: **maximise the controls at every step.** A feature is not a slice until the user can drive it every way a DAW user expects. Twice now a view has shipped with only the headline controls and the owner had to ask for the rest. Before writing the view:
   - **List the full control set first.** Take it from (1) the design canvas artboard for that screen and DESIGN-NOTES.md, (2) the same view in Ableton, Logic, FL Studio, Reaper and Cubase, and (3) the checklist below. Write that list into the slice's commit message or docs, and mark each line built, or planned with a phase chip. Never leave a gap unmarked.
   - **Every action gets every route:** a toolbar or inspector control, a right-click menu entry, a keyboard shortcut, and a tooltip that names the shortcut. If the action has a mouse gesture (drag, double-click, modifier-click, wheel), build that too.
   - **Mouse checklist (any editor):** click to select, Shift/Ctrl-click to add or toggle, drag a box to select, drag to move, drag an edge to resize, Ctrl/Alt-drag to copy, double-click to create or delete, right-click menu, wheel scrolls, Ctrl+wheel zooms horizontally, Alt+wheel zooms vertically, middle-drag or Space-drag pans, the cursor changes to show what a drag will do, Esc cancels a drag.
   - **Keyboard checklist (any editor):** Delete, Ctrl+A, Ctrl+C/X/V, Ctrl+D duplicate, arrows nudge (Shift = bigger step), Ctrl+Z/Y undo/redo, Esc deselects or returns focus, and the editor's own keys (Q quantise, etc.).
   - **Editing tools:** where the design shows tools (draw, select, erase, split...), build them all, plus the temporary-override modifiers.
   - **View controls:** zoom in/out/fit on both axes, scroll on both axes, follow-playhead, and a way to show only what matters (fold, scale highlight).
   - **Inspector:** every property you can edit by gesture is also a typed value in the inspector, and editing it works on a multi-selection.
   - **Check it by use,** not by reading the code: for each control, say what you click or press and what happens, in the report. If a control is not built, say so in plain words and put it in the Planned list with a phase chip.
   - Only the owner can say "that's enough controls". Default to more.
1. `tools/typecheck-linux.sh` passes, or `build.bat` builds, with zero warnings in our code.
2. New behaviour has a test in `Source/Tests/` that says WHY it matters, and would fail if the behaviour broke.
3. `test.bat` passes (or say clearly that it hasn't been run yet).
4. A fresh-context reviewer has read the diff.
5. Report what was verified and what wasn't. Never say "done" if anything was skipped.

## Boundaries

- Always: read the files you are changing and their callers first. Keep changes focused on the task.
- Ask first: new third-party dependencies, changing the song file format or folder layout, deleting user files, changing pinned JUCE/Tracktion commits.
- Never: write to the user's real Documents\EzDAWsIt in tests (use `AppPaths::setRootFolderOverride`), block or allocate on the audio thread, save while recording, overwrite a song file that failed to load, commit `deps/`, `build/` or `app/`.

## Pitfalls (add one line every time something goes wrong; details in docs/LESSONS.md)
- Shipped a view with only the headline controls (twice). Follow rule 0a: list the full control set first, from the design and from other DAWs.

- Tracktion's `modules/juce` submodule URL is SSH: clone JUCE separately over HTTPS (build.bat does).
- `Components.h` (Tracktion example) `#include`s its own .cpp: include it in one file only (the bridge).
- `EditFileOperations::saveTempVersion(false)` writes on a background thread and returns false: autosave uses `saveTempVersion(true)`.
- `juce::UnitTest` has a private member `category`: in tests write `ez::Tests::category`.
- `juce::File x = {}` is ambiguous: write `juce::File()`.
- Never delete an autosave or backup: rename it aside with a timestamp. Validate a file before replacing or loading it.
- Never replace a `ScopedMessageBox` from inside its own callback: show follow-up messages via `MessageManager::callAsync`.
- Tests use `createTestEngine()` (headless, own settings name). Never construct `AudioEngine` with the real app name in tests.
- Don't trust `Edit::hasChangedSinceSaved()` (asynchronous, and inactive until the first message loop turn): Session compares content fingerprints.
- Tests run inside `initialise()` with no message loop: anything Tracktion does via `callAsync` hasn't happened yet. Each test block must set up its own state.
- Tracktion loads an empty file as a valid blank song: always check `file.getSize() > 0` before trusting a song/autosave.
- Offline renders in tests need the test engine's `TestUIBehaviour` (the default UIBehaviour silently skips the render). `measureStatistics` stops every transport on the engine: tests only.
- Tests must pass on every run, not just the first: set up all state yourselves and clean up after (test settings persist in %APPDATA%\\EzDAWsIt-Tests).
- UI-only state (view flags, zoom) is set with a null UndoManager, never the Edit's.
- Any window holding engine references must be closed before `AudioEngine` is destroyed (`Dialogs::closeAll()`).
- Async callbacks (menus, AlertWindows) capture `EditItemID`, never `te::Clip::Ptr` or raw clip pointers.
- Every Session command begins its own undo step; compound user actions are one step.
- Views never rebuild synchronously from a Session notification; set a flag and let the timer do it.
- Bulk scripted edits: replace later ranges first, then check every header declaration has a definition.
- Tests never assume counts left by earlier tests (tracks, clips): derive them from the current state. A test that expects a refusal also checks nothing changed.
