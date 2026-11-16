/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The sounds the browser's "Samples & loops" lists: every audio file in Documents\EzDAWsIt\Samples
    and in the folders you add (remembered in the app settings, not the song). Scanned on a
    background thread (subfolders too, with each file's length); the change message says when the
    list is ready. Message thread for everything public.

    A file counts as a loop when its name says so ("loop", "bpm") or it's 1-32 bars long at a
    tempo its name gives; anything else is a one-shot.
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    class SampleLibrary : public juce::ChangeBroadcaster
    {
    public:
        explicit SampleLibrary (te::Engine&);
        ~SampleLibrary() override;

        struct Entry
        {
            juce::File file;
            juce::File folder;          // the library folder it was found in
            double seconds = 0.0;       // 0 if it couldn't be read
            bool isLoop = false;
            juce::int64 size = 0;
            juce::Time modified;        // with size: a file that hasn't changed isn't read again

            /** "Drums/Kicks · 0.4 s" */
            juce::String describe() const;
        };

        static constexpr int maxFilesPerFolder = 5000, maxFiles = 20000;
        static const char* const audioExtensions;   // "wav;aif;aiff;flac;ogg;mp3;..."

        /** Documents\EzDAWsIt\Samples (made if missing). Always in the list, can't be removed. */
        static juce::File getOwnFolder();

        juce::Array<juce::File> getFolders() const;            // own folder first
        bool addFolder (const juce::File&);                     // false if it isn't a folder or is there already
        bool removeFolder (const juce::File&);                  // not the own folder

        /** Scans again in the background (the old list stays until the new one is ready). Files already
            known (same size and date) aren't opened again. */
        void rescan();

        /** Rescans only if the last scan is more than a minute old (opening the category). */
        void rescanIfStale();
        bool isScanning() const noexcept                        { return scanner != nullptr; }

        const std::vector<Entry>& getEntries() const noexcept   { return entries; }

        /** For tests: blocks until a running scan is done (and its results are in). */
        void waitForScan();

    private:
        class Scanner;

        te::Engine& engine;
        std::vector<Entry> entries;
        std::unique_ptr<Scanner> scanner;
        bool rescanWanted = false;
        juce::Time lastScan;

        std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);   // the polling callbacks check it

        void saveFolders (const juce::Array<juce::File>&);
        void pollScan (Scanner*);
        void scanFinished();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLibrary)
    };
}
