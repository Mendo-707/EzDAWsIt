/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Every user action is a command. Buttons, shortcuts, menus and (later)
    MIDI controllers all trigger the same commands, so behaviour lives in
    one place: MainComponent::perform().

    Append new IDs at the end; never renumber (saved key mappings use them).
*/

#pragma once

#include "../Core/Common.h"

namespace ez::CommandIDs
{
    enum : juce::CommandID
    {
        newSong = 0x1000,
        openSong,
        saveSong,
        audioSetup,
        pluginList,

        togglePlay,
        toggleRecord,
        returnToStart,
        toggleClick,
        toggleCountIn,

        addTrack,
        importAudio,
        undo,
        redo,

        // Phase 2
        addInstrumentTrack,
        routing,
        deleteSelection,
        toggleLoop,
        toggleSnap,
        zoomIn,
        zoomOut,

        // Phase 2B-2: editing
        copy,
        cut,
        paste,
        duplicate,
        selectAll,
        splitClips,
        deselectAll,
        nudgeLeft,
        nudgeRight,
        goToEnd,

        // Shell
        toggleBrowser,
        toggleDetailPanel,
        toggleClipEditor,

        // P3
        resetMeters,
        showArrangeView,
        showMixView,
        toggleMixView,

        // P3: sends and buses
        addBus,
        addFxReturn,
        routeToNewBus,
        toggleSends,

        // P3: automation
        toggleAutomationLanes,
        toggleAutomationWrite,
        toggleAutomationRead,

        // Interface size
        interfaceBigger,
        interfaceSmaller,
        interfaceAutomatic,

        // The highlighted track (Arrange and Mix)
        muteTrack,
        soloTrack,
        armTrack,

        exportSong,
        focusSearch,

        // Master
        showMasterView,
        toggleMasterChain,
        toggleReference,

        // Guitar & bass
        showTuner,

        // Recording
        togglePunch,
        punchInHere,
        punchOutHere,
        nextTake,
        previousTake,

        // MIDI
        midiLearn,
        toggleMidiClock,

        // View and help
        toggleProView,
        showShortcuts,
        startTour,
        showAbout,
        openLogFolder,

        // Audio clips and tempo
        tapTempo,
        fadeClips,
        normaliseClips,
        reverseClips,
        followTempoClips,
        transposeClipsUp,
        transposeClipsDown,
        zoomToFit,
        loopSelection,

        // The highlighted track
        renameTrack,
        duplicateTrack,
        deleteTrack,
        moveTrackUp,
        moveTrackDown,
        previousTrack,
        nextTrack,
        toggleFollow,

        // Songs
        saveSongAs,
        songMenu
    };
}
