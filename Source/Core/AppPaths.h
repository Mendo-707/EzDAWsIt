/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Where EzDAWsIt keeps things on disk:
        Documents\EzDAWsIt\Projects\<Song>\<Song>.tracktionedit
        Documents\EzDAWsIt\Logs\
*/

#pragma once

#include "Common.h"

namespace ez::AppPaths
{
    constexpr const char* songFileExtension = ".tracktionedit";

    juce::File getRootFolder();

    /** Tests only: redirect every app folder somewhere disposable. Pass {} to reset. */
    void setRootFolderOverride (const juce::File&);
    juce::File getProjectsFolder();
    juce::File getLogsFolder();

    /** Creates all app folders if missing. Returns false if any could not be created. */
    bool createFolders();

    /** Makes a new, empty, uniquely named folder for a song and returns its song file path. */
    juce::File createSongFile (const juce::String& songName);

    /** The song that was open when the app last closed (may no longer exist). */
    juce::File getLastSongFile();
    void rememberLastSongFile (const juce::File&);   // also puts it first in the recent songs

    /** Songs opened lately, newest first (up to 10, only ones that still exist). */
    juce::Array<juce::File> getRecentSongs();

    /** The song named on a command line (double-clicking a song in Explorer, or "Open with"),
        or {} if there is none that exists. Quoted and unquoted paths both work. */
    juce::File findSongInCommandLine (const juce::String& commandLine);
}
