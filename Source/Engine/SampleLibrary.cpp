/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "SampleLibrary.h"
#include "../Core/AppPaths.h"

namespace ez
{
    namespace
    {
        const char* foldersKey = "ezSampleFolders";

        bool nameSaysLoop (const juce::String& name)
        {
            const auto lower = name.toLowerCase();
            return lower.contains ("loop") || lower.contains ("bpm");
        }
    }

    const char* const SampleLibrary::audioExtensions = "wav;aif;aiff;flac;ogg;mp3;m4a;wma;caf";

    juce::String SampleLibrary::Entry::describe() const
    {
        auto where = file.getParentDirectory().getRelativePathFrom (folder).replaceCharacter ('\\', '/');

        if (where == ".")
            where = {};

        const auto length = seconds <= 0.0 ? juce::String()
                          : seconds < 10.0 ? juce::String (seconds, 1) + " s"
                                           : juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2);
        juce::StringArray parts;

        if (isLoop)
            parts.add ("Loop");

        if (length.isNotEmpty())
            parts.add (length);

        if (where.isNotEmpty())
            parts.add (where);

        return parts.joinIntoString (juce::CharPointer_UTF8 (" \xc2\xb7 "));   // a middle dot whatever the compiler's source encoding
    }

    //==============================================================================
    /** Walks the folders and reads each file's length (header only). */
    class SampleLibrary::Scanner : public juce::Thread
    {
    public:
        Scanner (juce::AudioFormatManager& f, juce::Array<juce::File> folderList, std::vector<Entry> known)
            : juce::Thread ("EzDAWsIt sample scan"), formats (f), folders (std::move (folderList))
        {
            for (auto& e : known)
                previous[e.file.getFullPathName()] = e;
        }

        ~Scanner() override     { stopThread (4000); }

        std::vector<Entry> results;   // read after `done`
        std::atomic<bool> done { false };

        void run() override
        {
            int total = 0, looked = 0;

            for (const auto& folder : folders)
            {
                int count = 0;

                // No following links in circles; hidden folders (and whatever is in them) left out
                for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles,
                                                                        juce::File::FollowSymlinks::noCycles))
                {
                    if (threadShouldExit() || ++looked > maxFiles * 20)   // a whole drive: stop somewhere
                        return finish();

                    const auto& file = entry.getFile();

                    if (! file.hasFileExtension (audioExtensions) || file.getFileName().startsWithChar ('.')
                         || entry.isHidden() || file.getRelativePathFrom (folder).contains ("/.") || file.getRelativePathFrom (folder).contains ("\\."))
                        continue;

                    Entry e;
                    e.file = file;
                    e.folder = folder;
                    e.size = entry.getFileSize();
                    e.modified = entry.getModificationTime();

                    if (auto known = previous.find (file.getFullPathName()); known != previous.end()
                         && known->second.size == e.size && known->second.modified == e.modified)
                    {
                        e.seconds = known->second.seconds;   // unchanged: no need to open it (MP3s are read through)
                    }
                    else if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)))
                    {
                        if (reader->sampleRate > 0)
                            e.seconds = (double) reader->lengthInSamples / reader->sampleRate;
                    }

                    e.isLoop = nameSaysLoop (file.getFileNameWithoutExtension()) || nameSaysLoop (file.getParentDirectory().getFileName());
                    results.push_back (e);

                    if (++count >= maxFilesPerFolder || ++total >= maxFiles)
                        break;
                }

                if (total >= maxFiles)
                    break;
            }

            finish();
        }

    private:
        juce::AudioFormatManager& formats;
        const juce::Array<juce::File> folders;
        std::map<juce::String, Entry> previous;

        void finish()
        {
            std::sort (results.begin(), results.end(), [] (const Entry& a, const Entry& b)
            {
                return a.file.getFullPathName().compareNatural (b.file.getFullPathName()) < 0;
            });

            done = true;
        }
    };

    //==============================================================================
    SampleLibrary::SampleLibrary (te::Engine& e) : engine (e)
    {
        rescan();
    }

    SampleLibrary::~SampleLibrary()
    {
        scanner.reset();
    }

    juce::File SampleLibrary::getOwnFolder()
    {
        auto folder = AppPaths::getRootFolder().getChildFile ("Samples");
        folder.createDirectory();
        return folder;
    }

    juce::Array<juce::File> SampleLibrary::getFolders() const
    {
        juce::Array<juce::File> folders { getOwnFolder() };
        juce::StringArray saved;
        saved.addLines (engine.getPropertyStorage().getPropertiesFile().getValue (foldersKey));

        for (const auto& path : saved)
            if (juce::File::isAbsolutePath (path.trim()))
                folders.addIfNotAlreadyThere (juce::File (path.trim()));

        return folders;
    }

    void SampleLibrary::saveFolders (const juce::Array<juce::File>& folders)
    {
        juce::StringArray paths;

        for (const auto& f : folders)
            if (f != getOwnFolder())
                paths.add (f.getFullPathName());

        auto& props = engine.getPropertyStorage().getPropertiesFile();
        props.setValue (foldersKey, paths.joinIntoString ("\n"));
        props.saveIfNeeded();
    }

    bool SampleLibrary::addFolder (const juce::File& folder)
    {
        auto folders = getFolders();

        if (! folder.isDirectory() || folders.contains (folder))
            return false;

        folders.add (folder);
        saveFolders (folders);
        rescan();
        return true;
    }

    bool SampleLibrary::removeFolder (const juce::File& folder)
    {
        auto folders = getFolders();

        if (folder == getOwnFolder() || ! folders.contains (folder))
            return false;

        folders.removeFirstMatchingValue (folder);
        saveFolders (folders);
        rescan();
        return true;
    }

    void SampleLibrary::rescan()
    {
        if (scanner != nullptr)
        {
            rescanWanted = true;   // after this one: the folders changed while it ran
            return;
        }

        rescanWanted = false;
        scanner = std::make_unique<Scanner> (engine.getAudioFileFormatManager().readFormatManager, getFolders(), entries);
        scanner->startThread (juce::Thread::Priority::low);

        // Poll from the message thread: the results are swapped in there, never across threads
        auto* polling = scanner.get();
        juce::Timer::callAfterDelay (50, [this, polling, alive = std::weak_ptr<bool> (aliveFlag)]
        {
            if (! alive.expired())
                pollScan (polling);
        });
    }

    void SampleLibrary::pollScan (Scanner* which)
    {
        if (scanner.get() != which)
            return;

        if (! scanner->done)
        {
            juce::Timer::callAfterDelay (100, [this, which, alive = std::weak_ptr<bool> (aliveFlag)]
            {
                if (! alive.expired())
                    pollScan (which);
            });
            return;
        }

        scanFinished();
    }

    void SampleLibrary::scanFinished()
    {
        entries = std::move (scanner->results);
        scanner.reset();
        lastScan = juce::Time::getCurrentTime();
        sendChangeMessage();

        if (rescanWanted)
            rescan();
    }

    void SampleLibrary::rescanIfStale()
    {
        if (scanner == nullptr && juce::Time::getCurrentTime() - lastScan > juce::RelativeTime::minutes (1))
            rescan();
    }

    void SampleLibrary::waitForScan()
    {
        for (int i = 0; i < 2000 && scanner != nullptr; ++i)
        {
            if (scanner->done)
                scanFinished();
            else
                juce::Thread::sleep (5);
        }
    }
}
