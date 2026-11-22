/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "AppPaths.h"

namespace ez::AppPaths
{
    namespace
    {
        juce::File getLastSongNote()    { return getRootFolder().getChildFile ("last-song.txt"); }

        juce::File& rootOverride()
        {
            static juce::File folder;
            return folder;
        }
    }

    namespace
    {
        juce::File& bundledOverride()
        {
            static juce::File folder;
            return folder;
        }
    }

    juce::File getBundledPluginsFolder()
    {
        if (bundledOverride() != juce::File())
            return bundledOverride();

        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("Plugins");
    }

    void setBundledPluginsFolderOverride (const juce::File& folder)
    {
        bundledOverride() = folder;
    }

    void setRootFolderOverride (const juce::File& folder)
    {
        rootOverride() = folder;
    }

    juce::File getRootFolder()
    {
        if (rootOverride() != juce::File())
            return rootOverride();

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("EzDAWsIt");
    }

    juce::File getProjectsFolder()  { return getRootFolder().getChildFile ("Projects"); }
    juce::File getLogsFolder()      { return getRootFolder().getChildFile ("Logs"); }

    bool createFolders()
    {
        bool ok = true;

        for (auto& folder : { getRootFolder(), getProjectsFolder(), getLogsFolder() })
            ok = folder.createDirectory().wasOk() && ok;

        return ok;
    }

    juce::File createSongFile (const juce::String& songName)
    {
        auto name = juce::File::createLegalFileName (songName.trim());

        if (name.isEmpty())
            name = "New Song";

        auto folder = getProjectsFolder().getChildFile (name);

        if (folder.exists())
            folder = getProjectsFolder().getNonexistentChildFile (name, {}, false);

        if (! folder.createDirectory().wasOk())
            return {};

        return folder.getChildFile (folder.getFileName() + songFileExtension);
    }

    juce::File getLastSongFile()
    {
        auto note = getLastSongNote();

        if (! note.existsAsFile())
            return {};

        auto path = note.loadFileAsString().trim();
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    void rememberLastSongFile (const juce::File& songFile)
    {
        getLastSongNote().replaceWithText (songFile.getFullPathName());

        // The recent list: this one first, no repeats, ten at most
        juce::StringArray recent;
        recent.add (songFile.getFullPathName());

        for (const auto& f : getRecentSongs())
            if (f != songFile && recent.size() < 10)
                recent.add (f.getFullPathName());

        getLastSongNote().getSiblingFile ("recent-songs.txt").replaceWithText (recent.joinIntoString ("\n"));
    }

    juce::Array<juce::File> getRecentSongs()
    {
        juce::Array<juce::File> songs;
        const auto note = getLastSongNote().getSiblingFile ("recent-songs.txt");

        if (! note.existsAsFile())
            return songs;

        for (const auto& line : juce::StringArray::fromLines (note.loadFileAsString()))
            if (const auto path = line.trim(); juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
                songs.addIfNotAlreadyThere (juce::File (path));

        return songs;
    }

    juce::File findSongInCommandLine (const juce::String& commandLine)
    {
        const auto isSong = [] (const juce::String& path)
        {
            return juce::File::isAbsolutePath (path)
                && path.endsWithIgnoreCase (songFileExtension)
                && juce::File (path).existsAsFile();
        };

        // The whole line first: Windows can pass an unquoted path that contains spaces
        if (const auto whole = commandLine.trim().unquoted().trim(); isSong (whole))
            return juce::File (whole);

        for (const auto& token : juce::StringArray::fromTokens (commandLine, true))
            if (const auto path = token.trim().unquoted(); isSong (path))
                return juce::File (path);

        return {};
    }
}
