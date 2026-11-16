# EzDAWsIt

A powerful but easy DAW: record, arrange, mix and master in one app.

Windows 10/11 (64-bit). Published by EzDAW. Version 1.0.0.

## Install

Run `EzDAWsIt-Setup-<version>.exe`. It installs EzDAWsIt for everyone on the PC (or only you, without
administrator rights), adds Start menu and desktop shortcuts, and can open songs (`.tracktionedit`)
with a double-click. Uninstalling never deletes your songs.

## Build (Windows)

1. Double-click `build.bat`.
2. Wait for "SUCCESS" (the first build takes 15-30 minutes). It builds the app, then runs the self-tests.
3. EzDAWsIt starts. The app lives in `app\EzDAWsIt.exe`, and the installer in `dist\` (with the matching
   source package). The installer needs Inno Setup 6, which build.bat installs with winget if it is missing.

Needs Visual Studio 2022 with "Desktop development with C++", Git, and an internet connection for the first build.
Run the self-tests again any time with `test.bat` (results in `test-results.txt`).

## Basics

| Action | Button | Shortcut |
| --- | --- | --- |
| Play / stop | Play | Space |
| Record on armed tracks | Record | R |
| Back to start | \|< | Home |
| Metronome | Click | C |
| Save (also autosaves every minute) | Save | Ctrl+S |
| New song / open song | New Song / Open | Ctrl+N / Ctrl+O |
| Add a track / import audio | + Track / Import Audio | Ctrl+T / Ctrl+I |
| Undo / redo | Undo / Redo | Ctrl+Z / Ctrl+Y |

- **Setup > Audio device**: pick your audio interface. ASIO drivers give the lowest latency.
- Track 1 is armed on your first input. On any track: **I** picks the input, **A** arms it, **M** mutes, **S** solos.
- Songs live in `Documents\EzDAWsIt\Projects`. If the app ever closes unexpectedly, it offers to restore your unsaved work next time.
- Logs (useful when reporting a problem) are in `Documents\EzDAWsIt\Logs`.

## For developers and AI agents

Read `AGENTS.md` (architecture, commands, rules). Mistakes and their fixes are logged in `docs/LESSONS.md`.

## Licence

EzDAWsIt is free software under the GNU General Public License v3 (`LICENSE.txt`).
It is built on [JUCE](https://juce.com) (used under AGPLv3) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (GPLv3),
with Steinberg's ASIO (GPLv3 option) and VST3 (MIT) SDKs. Every bundled component and its licence is listed in
`installer/THIRD-PARTY-NOTICES.txt`, which the installer puts next to the app (Help > About EzDAWsIt > Licences).
