/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "BrowserPanel.h"
#include "../../Engine/AudioEngine.h"
#include "../../Engine/Effects.h"
#include "../../Engine/DrumKits.h"
#include "../../Engine/DrumMachinePlugin.h"
#include "../TrackMenus.h"

namespace ez
{
    namespace
    {
        constexpr int rowHeight = 26, itemRowHeight = 36, footerHeight = 30, maxResultsPerSection = 200;
        const char* previewKey = "ezPreviewOnClick";
        const char* filesFolderKey = "ezBrowserFolder";
        const juce::String dragPrefix ("ezfiles:");

        // Plain-language one-liners for the built-in devices
        juce::String describeBuiltIn (const juce::String& name)
        {
            static const std::pair<const char*, const char*> hints[] =
            {
                { "Synth (4OSC)", "Four-oscillator synth: bass, keys, leads" },
                { "Sampler",      "Play any sound across the keys" },
                { "Drum Machine", "16 pads and a step sequencer" },
                { "EQ",           "Shape the tone: cut mud, add clarity" },
                { "Compressor",   "Even out loud and quiet parts" },
                { "Reverb",       "Put the sound in a room" },
                { "Delay",        "Echoes, in time with the song" },
                { "Chorus",       "Thicken and widen" },
                { "Phaser",       "Slow sweeping movement" },
                { "Filter",       "Roll off the highs (low-pass)" },
                { "Gate",         "Silence between phrases: cuts noise and spill" },
                { "De-esser",     "Tames harsh \"s\" and \"t\" sounds" },
                { "Limiter",      "Nothing gets louder than the ceiling" },
                { "Low/high cut", "Removes rumble below, hiss above" },
                { "Saturator",    "Warmth and grit: tape, tube or hard" },
                { "Tune",         "Pulls sung notes to the song's key" },
                { "Tremolo",      "Pulsing level, or a left-right sweep" },
                { "Utility",      "Gain, stereo width, mono, phase flip" },
                { "Vocal Chain",  "A whole vocal sound: six macros, eight presets" },
            };

            for (auto [n, hint] : hints)
                if (name == n)
                    return hint;

            return {};
        }

        /** Every word of the query somewhere in the name or the detail (any order, any case). */
        bool matches (const juce::StringArray& words, const juce::String& name, const juce::String& detail)
        {
            for (const auto& w : words)
                if (! name.containsIgnoreCase (w) && ! detail.containsIgnoreCase (w))
                    return false;

            return true;
        }

        juce::String sizeText (juce::int64 bytes)
        {
            return bytes < 1024 * 1024 ? juce::String (juce::jmax ((juce::int64) 1, bytes / 1024)) + " KB"
                                       : juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
        }

        juce::File defaultFilesFolder()
        {
            const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            return music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        }
    }

    //==============================================================================
    juce::var BrowserDrag::describe (const juce::StringArray& paths)
    {
        return paths.isEmpty() ? juce::var() : juce::var (dragPrefix + paths.joinIntoString ("\n"));
    }

    juce::StringArray BrowserDrag::filesIn (const juce::var& description)
    {
        juce::StringArray paths;
        const auto text = description.toString();

        if (text.startsWith (dragPrefix))
            paths.addLines (text.substring (dragPrefix.length()));

        paths.removeEmptyStrings();
        return paths;
    }

    //==============================================================================
    class BrowserPanel::CategoryRow : public NavRow
    {
    public:
        CategoryRow (const juce::String& name, Icon i, Category c) : NavRow (name, i, true), category (c) {}
        const Category category;
    };

    //==============================================================================
    BrowserPanel::BrowserPanel (AudioEngine& e, Session& s)
        : audioEngine (e), session (s), library (e.getEngine())
    {
        closeButton.setTooltip ("Hide the browser (B)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        search.setTextToShowWhenEmpty ("Search sounds, instruments, effects", Theme::textDim);
        search.setIndents (26, 5);
        search.setTooltip ("Ctrl+F. Finds instruments, effects, drum kits, samples and files in the open folder. "
                           "Enter: use the first one; Esc: clear");
        search.onTextChange = [this] { rebuildItems(); };
        search.onEscapeKey = [this]
        {
            search.clear();
            rebuildItems();
            returnFocus();
        };
        search.onReturnKey = [this]
        {
            if (search.getText().trim().isEmpty())
                return;   // nothing typed: Enter isn't "use the first instrument"

            for (int i = 0; i < (int) items.size(); ++i)
                if (items[(size_t) i].kind != Kind::header)
                {
                    list.selectRow (i);
                    returnFocus();
                    activate (i);
                    return;
                }
        };
        addAndMakeVisible (search);

        struct Def { const char* name; Icon icon; Category category; const char* phase; };
        const Def defs[] =
        {
            { "Song templates",  Icon::layers,   Category::templates,    nullptr },
            { "Track presets",   Icon::bookmark, Category::trackPresets, nullptr },
            { "Effect chains",   Icon::chain,    Category::chains,       nullptr },
            { "Instruments",     Icon::piano,    Category::instruments,  nullptr },
            { "Effects",         Icon::plug,     Category::effects,      nullptr },
            { "Drum kits",       Icon::drum,     Category::kits,         nullptr },
            { "Samples & loops", Icon::note,     Category::samples,      nullptr },
            { "Files",           Icon::file,     Category::files,        nullptr },
        };

        for (const auto& d : defs)
        {
            auto* row = categoryRows.add (new CategoryRow (d.name, d.icon, d.category));
            row->onClick = [this, category = d.category] { selectCategory (category); };

            if (d.phase != nullptr)
                Planned::mark (*row, d.phase);

            addAndMakeVisible (row);
        }

        previewButton.setTooltip ("Preview on click: hear a sound when you click it (click it again to stop)");
        previewButton.setClickingTogglesState (true);
        previewButton.setToggleState (previewOnClick(), juce::dontSendNotification);
        previewButton.onClick = [this]
        {
            audioEngine.getEngine().getPropertyStorage().getPropertiesFile().setValue (previewKey, previewButton.getToggleState());

            if (! previewButton.getToggleState())
                audioEngine.stopPreview();

            repaint();
        };
        previewButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (previewButton);

        const auto savedFolder = audioEngine.getEngine().getPropertyStorage().getPropertiesFile().getValue (filesFolderKey);
        filesFolder = juce::File::isAbsolutePath (savedFolder) && juce::File (savedFolder).isDirectory() ? juce::File (savedFolder) : defaultFilesFolder();

        list.setModel (this);
        list.setRowHeight (itemRowHeight);
        list.setOutlineThickness (0);
        list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
        list.setWantsKeyboardFocus (false);   // keep transport shortcuts working
        addAndMakeVisible (list);

        selectCategory (Category::instruments);
        session.getPluginListBroadcaster().addChangeListener (this);
        library.addChangeListener (this);
        session.getPresetBroadcaster().addChangeListener (this);
        startTimerHz (5);
    }

    BrowserPanel::~BrowserPanel()
    {
        audioEngine.stopPreview();
        library.removeChangeListener (this);
        session.getPresetBroadcaster().removeChangeListener (this);
        session.getPluginListBroadcaster().removeChangeListener (this);
        list.setModel (nullptr);
    }

    void BrowserPanel::returnFocus()
    {
        // Nothing focused would leave the shortcuts dead: hand the keyboard to the first parent that takes it
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (p->getWantsKeyboardFocus())
            {
                p->grabKeyboardFocus();
                return;
            }

        juce::Component::unfocusAllComponents();
    }

    void BrowserPanel::focusSearch()
    {
        search.grabKeyboardFocus();
        search.selectAll();
    }

    bool BrowserPanel::previewOnClick() const
    {
        return audioEngine.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue (previewKey, true);
    }

    void BrowserPanel::problem (const juce::String& title, const juce::String& message)
    {
        if (onProblem)
            onProblem (title, message);
    }

    //==============================================================================
    void BrowserPanel::selectCategory (Category category)
    {
        current = category;

        for (auto* row : categoryRows)
            row->setToggleState (row->category == category, juce::dontSendNotification);

        if (category == Category::samples)
            library.rescanIfStale();   // files may have been added since (right-click a folder to look now)

        if (category == Category::files)
            listedFolder = juce::File();   // read it again on a visit

        if (search.getText().isNotEmpty())
            search.clear();   // picking a category is leaving the search

        rebuildItems();
        repaint();
    }

    void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        // Only when it changes what's shown
        const bool searching = search.getText().trim().isNotEmpty();
        const bool presets = current == Category::templates || current == Category::trackPresets || current == Category::chains;
        const bool shown = source == &library                       ? (current == Category::samples || searching)
                         : source == &session.getPresetBroadcaster() ? (presets || searching)
                                                                     : (current == Category::instruments || current == Category::effects || searching);

        if (shown)
            rebuildItems();
    }

    void BrowserPanel::rebuildItems()
    {
        // Keep the selected row selected (by what it is) through a rebuild
        juce::String selectedName;
        juce::File selectedFile;

        if (const auto row = list.getSelectedRow(); juce::isPositiveAndBelow (row, (int) items.size()))
        {
            selectedName = items[(size_t) row].name;
            selectedFile = items[(size_t) row].file;
        }

        items.clear();
        const auto query = search.getText().trim();

        if (query.isEmpty())
        {
            addCategoryItems (current, {});
        }
        else
        {
            // Everything that matches, a section per kind (only those with something in them)
            for (auto c : { Category::templates, Category::trackPresets, Category::chains, Category::instruments, Category::effects,
                            Category::kits, Category::samples, Category::files })
            {
                const auto before = items.size();
                addCategoryItems (c, query);

                if (items.size() == before)
                    continue;

                static const char* names[] = { "Song templates", "Track presets", "Effect chains", "Instruments", "Effects", "Drum kits", "Samples & loops", "Files" };
                juce::String title = names[(int) c];

                if (c == Category::files)
                    title << " in " << filesFolder.getFileName();

                items.insert (items.begin() + (std::ptrdiff_t) before, Item { Kind::header, title });
            }

            if (items.empty())
                items.push_back ({ Kind::header, "Nothing matches \"" + query + "\"" });
        }

        list.updateContent();
        list.deselectAllRows();

        if (selectedName.isNotEmpty())
            for (int i = 0; i < (int) items.size(); ++i)
                if (items[(size_t) i].kind != Kind::header && items[(size_t) i].name == selectedName && items[(size_t) i].file == selectedFile)
                {
                    list.selectRow (i, true);
                    break;
                }

        list.repaint();
        repaint (listTitleArea);
    }

    void BrowserPanel::addCategoryItems (Category category, const juce::String& query)
    {
        juce::StringArray words;
        words.addTokens (query, " ", "\"");
        words.removeEmptyStrings();
        const bool searching = ! words.isEmpty();
        int found = 0;

        const auto add = [&] (Item item)
        {
            if (! searching)
            {
                items.push_back (std::move (item));
                return;
            }

            if (item.kind != Kind::header && found < maxResultsPerSection && matches (words, item.name, item.detail))
            {
                ++found;
                items.push_back (std::move (item));
            }
        };

        switch (category)
        {
            case Category::kits:
            {
                for (auto& genre : DrumKits::getGenres())
                {
                    add ({ Kind::header, genre });

                    for (int k = 0; k < DrumKits::getNumKits(); ++k)
                        if (genre == DrumKits::getKit (k).genre)
                        {
                            Item item { Kind::kit, DrumKits::getKit (k).name, juce::String (juce::CharPointer_UTF8 ("16 pads \xc2\xb7 ")) + juce::String (DrumKits::getKit (k).description) };
                            item.kitIndex = k;
                            add (item);
                        }
                }
                return;
            }

            case Category::instruments:
            case Category::effects:
            {
                const bool instruments = category == Category::instruments;
                const auto choices = instruments ? session.getAvailableInstruments() : session.getAvailableEffects();
                const auto kind = instruments ? Kind::instrument : Kind::effect;
                bool shownPluginHeader = false;

                const auto addChoice = [&] (const Session::PluginChoice& c, const juce::String& detail)
                {
                    Item item { kind, c.name, detail };
                    item.choice = c;
                    add (item);
                };

                if (instruments)
                    add ({ Kind::header, "Built-in" });
                else
                    for (const auto& effectCategory : Effects::categories())   // built-in effects by what they do
                    {
                        bool shownHeader = false;

                        for (const auto& c : choices)
                            if (c.category == effectCategory)
                            {
                                if (! std::exchange (shownHeader, true))
                                    add ({ Kind::header, effectCategory });

                                addChoice (c, describeBuiltIn (c.name));
                            }
                    }

                for (const auto& c : choices)
                {
                    const bool isPlugin = c.pluginType == te::ExternalPlugin::xmlTypeName;

                    if (! instruments && ! isPlugin)
                        continue;   // listed under its category above

                    if (isPlugin && ! shownPluginHeader)
                    {
                        add ({ Kind::header, "VST3 (scanned)" });
                        shownPluginHeader = true;
                    }

                    addChoice (c, isPlugin ? c.description.manufacturerName : describeBuiltIn (c.name));
                }

                if (! shownPluginHeader)
                    add ({ Kind::header, "No plugins yet: Setup > Plugins to scan" });

                return;
            }

            case Category::samples:
            {
                if (! searching)
                    items.push_back ({ Kind::addFolder, "Add a folder of samples...", "Your sample packs: they stay where they are" });

                // Each library folder, then the built-in drum sounds
                const auto& entries = library.getEntries();

                for (const auto& folder : library.getFolders())
                {
                    int inFolder = 0;

                    for (const auto& e : entries)
                        if (e.folder == folder)
                            ++inFolder;

                    Item header { Kind::header, folder == SampleLibrary::getOwnFolder() ? juce::String ("My samples") : folder.getFileName() };
                    header.file = folder;

                    if (! searching)
                        header.name << (library.isScanning() ? juce::String ("  (looking...)") : "  (" + juce::String (inFolder) + ")");

                    add (header);

                    if (! searching && inFolder == 0 && ! library.isScanning() && folder == SampleLibrary::getOwnFolder())
                        items.push_back ({ Kind::header, "Empty: put sounds in Documents\\EzDAWsIt\\Samples" });

                    for (const auto& e : entries)
                        if (e.folder == folder)
                        {
                            Item item { Kind::sample, e.file.getFileNameWithoutExtension(), e.describe() };
                            item.file = e.file;
                            item.isLoop = e.isLoop;
                            add (item);
                        }
                }

                for (int k = 0; k < DrumKits::getNumKits(); ++k)
                {
                    add ({ Kind::header, "Built-in: " + juce::String (DrumKits::getKit (k).name) });

                    for (int pad = 0; pad < DrumKits::numPads; ++pad)
                    {
                        Item item { Kind::kitSound, DrumKits::getPadName (pad), juce::String (DrumKits::getKit (k).name) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 one-shot")) };
                        item.kitIndex = k;
                        item.pad = pad;
                        add (item);
                    }
                }

                return;
            }

            case Category::files:
            {
                if (! searching)
                {
                    items.push_back ({ Kind::header, "Places" });

                    const std::pair<const char*, juce::File::SpecialLocationType> places[] = {
                        { "Desktop", juce::File::userDesktopDirectory }, { "Documents", juce::File::userDocumentsDirectory },
                        { "Music", juce::File::userMusicDirectory }, { "Home", juce::File::userHomeDirectory } };

                    for (auto [name, where] : places)
                    {
                        const auto dir = juce::File::getSpecialLocation (where);

                        if (dir.isDirectory())
                        {
                            Item item { Kind::folder, name, dir.getFullPathName() };
                            item.file = dir;
                            items.push_back (item);
                        }
                    }

                    if (const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"); downloads.isDirectory())
                    {
                        Item item { Kind::folder, "Downloads", downloads.getFullPathName() };
                        item.file = downloads;
                        items.push_back (item);
                    }

                    juce::Array<juce::File> drives;
                    juce::File::findFileSystemRoots (drives);

                    for (const auto& d : drives)
                    {
                        Item item { Kind::folder, d.getFullPathName(), "Drive" };
                        item.file = d;
                        items.push_back (item);
                    }

                    Item header { Kind::header, filesFolder.getFullPathName() };
                    header.file = filesFolder;
                    items.push_back (header);

                    if (filesFolder.getParentDirectory() != filesFolder)
                    {
                        Item up { Kind::up, ".. (up a folder)", filesFolder.getParentDirectory().getFullPathName() };
                        up.file = filesFolder.getParentDirectory();
                        items.push_back (up);
                    }
                }

                // This folder only (searching doesn't walk the whole computer), read once per visit
                listFolder();

                for (const auto& child : folderListing)
                    if (child.isFolder)
                    {
                        Item item { Kind::folder, child.file.getFileName(), "Folder" };
                        item.file = child.file;
                        add (item);
                    }

                for (const auto& child : folderListing)
                    if (! child.isFolder)
                    {
                        Item item { Kind::file, child.file.getFileName(), sizeText (child.size) };
                        item.file = child.file;
                        add (item);
                    }

                return;
            }

            case Category::templates:
            case Category::trackPresets:
            case Category::chains:
            {
                const auto kind = category == Category::templates    ? Session::PresetKind::songTemplate
                                : category == Category::trackPresets ? Session::PresetKind::trackPreset
                                                                     : Session::PresetKind::effectChain;

                if (! searching)
                {
                    Item save { Kind::savePreset,
                                kind == Session::PresetKind::songTemplate ? "Save this song as a template..."
                              : kind == Session::PresetKind::trackPreset  ? "Save the highlighted track as a preset..."
                                                                          : "Save the highlighted track's effects as a chain...",
                                kind == Session::PresetKind::songTemplate ? "Its tracks, effects, sends, tempo and master: no recordings"
                              : kind == Session::PresetKind::trackPreset  ? "Instrument, effects, colour, level and pan"
                                                                          : "The effects in order, with their settings" };
                    save.preset.kind = kind;
                    items.push_back (save);
                }

                bool shownYours = false;
                add ({ Kind::header, "Built-in" });

                for (const auto& p : session.getPresets (kind))
                {
                    if (! p.isBuiltIn() && ! std::exchange (shownYours, true))
                    {
                        Item header { Kind::header, "Yours" };
                        header.file = Session::getPresetFolder (kind);
                        add (header);
                    }

                    Item item { Kind::preset, p.name, p.description };
                    item.preset = p;
                    item.file = p.file;
                    add (item);
                }

                if (! shownYours && ! searching)
                {
                    Item header { Kind::header, "Yours: none yet (save one above, or right-click a track)" };
                    header.file = Session::getPresetFolder (kind);
                    items.push_back (header);
                }

                return;
            }
        }
    }

    void BrowserPanel::usePreset (const Session::PresetInfo& preset)
    {
        if (preset.kind == Session::PresetKind::songTemplate)
        {
            if (onNewSongFromTemplate)
                onNewSongFromTemplate (preset);

            return;
        }

        if (! session.hasSong())
            return;

        if (preset.kind == Session::PresetKind::trackPreset)
        {
            if (! session.addTrackFromPreset (preset))
                problem ("Could not add " + preset.name, session.getLastError());

            return;
        }

        const auto track = getTargetTrack ? getTargetTrack() : -1;

        if (! session.applyEffectChain (track, preset))
            problem ("Could not add " + preset.name, session.getLastError());
    }

    void BrowserPanel::savePreset (Session::PresetKind kind)
    {
        // Already asking (a double-click is a click, then another)
        if (! session.hasSong() || juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
            return;

        const auto track = getTargetTrack ? getTargetTrack() : -1;

        if (kind != Session::PresetKind::songTemplate && ! juce::isPositiveAndBelow (track, session.getNumAudioTracks()))
        {
            problem ("Which track?", "Click a track first: its " + juce::String (kind == Session::PresetKind::trackPreset ? "setup" : "effects")
                                     + " is what gets saved.");
            return;
        }

        const auto suggestion = kind == Session::PresetKind::songTemplate ? session.getSongName() + " template"
                              : kind == Session::PresetKind::trackPreset  ? session.getTrackName (track)
                                                                          : session.getTrackName (track) + " chain";
        juce::Component::SafePointer<BrowserPanel> safe (this);

        TrackMenus::askForPresetName (session, kind, kind == Session::PresetKind::songTemplate ? "Save song template"
                              : kind == Session::PresetKind::trackPreset  ? "Save track preset" : "Save effect chain",
                                "A name for it:", suggestion, [safe, kind, track] (const juce::String& name)
        {
            if (safe == nullptr)
                return;

            auto& s = safe->session;
            const bool ok = kind == Session::PresetKind::songTemplate ? s.saveSongTemplate (name)
                          : kind == Session::PresetKind::trackPreset  ? s.saveTrackPreset (track, name)
                                                                      : s.saveEffectChain (track, name);

            if (! ok)
                safe->problem ("Could not save it", s.getLastError());   // a save rebuilds the list itself (preset broadcaster)
        });
    }

    //==============================================================================
    void BrowserPanel::listFolder()
    {
        if (listedFolder == filesFolder)
            return;

        listedFolder = filesFolder;
        folderListing.clear();

        for (const auto& entry : juce::RangedDirectoryIterator (filesFolder, false, "*",
                                                                juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            const auto& f = entry.getFile();

            if (entry.isDirectory())
                folderListing.push_back ({ f, true, 0 });
            else if (f.hasFileExtension (SampleLibrary::audioExtensions))
                folderListing.push_back ({ f, false, entry.getFileSize() });
        }

        std::sort (folderListing.begin(), folderListing.end(), [] (const Child& a, const Child& b)
        {
            return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
        });
    }

    juce::File BrowserPanel::soundFile (const Item& item, bool reportProblems)
    {
        if (item.kind != Kind::kitSound)
            return item.file;

        juce::String error;
        auto file = DrumKits::getSampleFile (item.kitIndex, item.pad, error);

        if (file == juce::File() && reportProblems)
            problem ("Could not make the sound", error);

        return file;
    }

    void BrowserPanel::addSound (const Item& item, bool onNewTrack)
    {
        if (! session.hasSong())
            return;

        const auto file = soundFile (item);

        if (file == juce::File())
            return;

        const auto target = getTargetTrack ? getTargetTrack() : -1;
        const auto track = onNewTrack || ! juce::isPositiveAndBelow (target, session.getNumAudioTracks()) ? session.getNumAudioTracks() : target;

        juce::StringArray problems;   // (the track count as the index: a new track)

        if (! session.importAudioFilesAt ({ file }, track, session.getPosition(), problems) || ! problems.isEmpty())
            problem ("Could not add " + file.getFileName(), problems.isEmpty() ? session.getLastError() : problems.joinIntoString ("\n"));
    }

    void BrowserPanel::togglePreview (const Item& item)
    {
        const auto file = soundFile (item);

        if (file == juce::File())
            return;

        if (audioEngine.isPreviewing() && audioEngine.getPreviewFile() == file)
            audioEngine.stopPreview();
        else if (! audioEngine.startPreview (file))
            problem ("Can't play " + file.getFileName(), "EzDAWsIt can't read this file (WAV, AIFF, FLAC, Ogg and MP3 work).");

        timerCallback();
    }

    void BrowserPanel::navigateTo (const juce::File& folder)
    {
        if (! folder.isDirectory())
        {
            problem ("Can't open the folder", folder.getFullPathName() + " isn't there any more.");
            return;
        }

        filesFolder = folder;
        listedFolder = juce::File();
        audioEngine.getEngine().getPropertyStorage().getPropertiesFile().setValue (filesFolderKey, folder.getFullPathName());

        if (current != Category::files || search.getText().isNotEmpty())
            selectCategory (Category::files);
        else
            rebuildItems();
    }

    void BrowserPanel::chooseLibraryFolder()
    {
        if (std::exchange (choosingFolder, true))
            return;   // already asking (a double-click is two clicks)

        chooser = std::make_unique<juce::FileChooser> ("Add a folder of samples (it stays where it is)", filesFolder);
        juce::Component::SafePointer<BrowserPanel> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr)
                                      safe->choosingFolder = false;

                                  if (safe == nullptr || ! fc.getResult().isDirectory())
                                      return;

                                  if (! safe->library.addFolder (fc.getResult()))
                                      safe->problem ("Already there", fc.getResult().getFullPathName() + " is in Samples & loops already.");

                                  safe->rebuildItems();
                              });
    }

    void BrowserPanel::activate (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        // A copy: loading a plugin can run the message loop (licence boxes), which may rebuild the list
        const auto item = items[(size_t) row];

        switch (item.kind)
        {
            case Kind::header:      return;
            case Kind::preset:      usePreset (item.preset); return;
            case Kind::savePreset:  savePreset (item.preset.kind); return;
            case Kind::addFolder:   chooseLibraryFolder(); return;
            case Kind::folder:
            case Kind::up:          navigateTo (item.file); return;
            case Kind::sample:
            case Kind::file:
            case Kind::kitSound:    addSound (item, false); return;
            case Kind::instrument:
            case Kind::effect:
            case Kind::kit:         break;
        }

        if (! session.hasSong())
            return;

        if (item.kind == Kind::kit)
        {
            // On the highlighted track if it has a Drum Machine, otherwise on a new drum track
            const auto track = getTargetTrack ? getTargetTrack() : -1;

            if (auto* drums = session.getDrumMachine (track))
            {
                if (! session.loadDrumKit (*drums, item.kitIndex))
                    problem ("Could not load " + item.name, session.getLastError());
            }
            else if (! session.addDrumMachineTrack (item.kitIndex))
            {
                problem ("Could not add " + item.name, session.getLastError());
            }
        }
        else if (item.kind == Kind::instrument)
        {
            if (! session.addInstrumentTrack (item.choice, true))
                problem ("Could not add " + item.name, session.getLastError());
        }
        else
        {
            const auto track = getTargetTrack ? getTargetTrack() : 0;

            if (! session.addEffect (track, item.choice, true))
                problem ("Could not add " + item.name, session.getLastError());
        }
    }

    void BrowserPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        if (search.hasKeyboardFocus (true))
            returnFocus();   // Space, R, M... are shortcuts again, not typed into the search

        if (e.mods.isPopupMenu())
        {
            showMenu (row);
            return;
        }

        const auto item = items[(size_t) row];

        if (item.isSound() && previewOnClick() && e.getNumberOfClicks() == 1)
            togglePreview (item);
        else if (item.kind == Kind::addFolder)
            chooseLibraryFolder();
        else if (item.kind == Kind::savePreset && e.getNumberOfClicks() == 1)
            savePreset (item.preset.kind);
    }

    void BrowserPanel::showMenu (int row)
    {
        const auto item = items[(size_t) row];
        list.selectRow (row);
        juce::PopupMenu menu;
        const auto target = getTargetTrack ? getTargetTrack() : -1;
        const bool hasTarget = session.hasSong() && juce::isPositiveAndBelow (target, session.getNumAudioTracks());
        enum { preview = 1, addHere, addNew, reveal, open, addToLibrary, removeFromLibrary, rescan, use, remove, padBase = 100 };

        if (item.isSound())
        {
            const bool playing = audioEngine.isPreviewing() && audioEngine.getPreviewFile() == soundFile (item);
            menu.addSectionHeader (item.name);
            menu.addItem (preview, playing ? "Stop the preview" : "Preview (click)");
            menu.addItem (addHere, hasTarget ? "Add to \"" + session.getTrackName (target) + "\" at the playhead (double-click)"
                                             : juce::String ("Add at the playhead on a new track (double-click)"), session.hasSong());
            menu.addItem (addNew, "Add on a new track", session.hasSong());

            if (hasTarget && session.getDrumMachine (target) != nullptr)
            {
                juce::PopupMenu pads;

                for (int p = 0; p < DrumKits::numPads; ++p)
                    pads.addItem (padBase + p, "Pad " + juce::String (p + 1) + "  (" + DrumKits::getPadName (p) + ")");

                menu.addSubMenu ("Load on a drum pad of \"" + session.getTrackName (target) + "\"", pads);
            }

            menu.addSeparator();
            menu.addItem (reveal, "Show in Explorer");
        }
        else if (item.kind == Kind::folder || item.kind == Kind::up)
        {
            menu.addSectionHeader (item.file.getFullPathName());
            menu.addItem (open, "Open (double-click)");
            menu.addItem (addToLibrary, "Add to Samples & loops", ! library.getFolders().contains (item.file));
            menu.addItem (reveal, "Show in Explorer");
        }
        else if (item.kind == Kind::header && item.file != juce::File())
        {
            menu.addSectionHeader (item.file.getFullPathName());

            if (library.getFolders().contains (item.file))
            {
                menu.addItem (rescan, "Look for new sounds");
                menu.addItem (removeFromLibrary, "Remove from Samples & loops (the files stay)", item.file != SampleLibrary::getOwnFolder());
            }

            menu.addItem (reveal, "Show in Explorer");
        }
        else if (item.kind == Kind::preset)
        {
            menu.addSectionHeader (item.name);
            menu.addItem (use, item.preset.kind == Session::PresetKind::songTemplate ? juce::String ("New song from this template (double-click)")
                             : item.preset.kind == Session::PresetKind::trackPreset  ? juce::String ("Add as a new track (double-click)")
                                                                                     : juce::String ("Add to the highlighted track (double-click)"),
                          item.preset.kind == Session::PresetKind::songTemplate || session.hasSong());
            menu.addSeparator();
            menu.addItem (reveal, "Show in Explorer", ! item.preset.isBuiltIn());
            menu.addItem (remove, item.preset.isBuiltIn() ? juce::String ("Delete (built-in: can't be)") : juce::String ("Delete (to the Recycle Bin)"),
                          ! item.preset.isBuiltIn());
        }
        else if (item.kind == Kind::instrument || item.kind == Kind::effect || item.kind == Kind::kit)
        {
            menu.addSectionHeader (item.name);
            menu.addItem (use, item.kind == Kind::instrument ? juce::String ("New track with it (double-click)")
                             : item.kind == Kind::effect     ? juce::String ("Add to the highlighted track (double-click)")
                                                             : juce::String ("Load on the highlighted drum track (double-click)"),
                          session.hasSong());
        }
        else
        {
            return;
        }

        juce::Component::SafePointer<BrowserPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition(), [safe, item, row] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& self = *safe;

            switch (result)
            {
                case preview:           self.togglePreview (item); break;
                case addHere:           self.addSound (item, false); break;
                case addNew:            self.addSound (item, true); break;
                case reveal:            self.soundFile (item).revealToUser(); break;
                case open:              self.navigateTo (item.file); break;
                case addToLibrary:      self.library.addFolder (item.file); self.rebuildItems(); break;
                case removeFromLibrary: self.library.removeFolder (item.file); self.rebuildItems(); break;
                case rescan:            self.library.rescan(); self.rebuildItems(); break;

                case remove:
                    if (! self.session.deletePreset (item.preset))
                        self.problem ("Could not delete it", self.session.getLastError());

                    break;

                case use:
                    // The row may have moved (a plugin scan rebuilt the list): only if it's still the same thing
                    if (juce::isPositiveAndBelow (row, (int) self.items.size()) && self.items[(size_t) row].kind == item.kind
                         && self.items[(size_t) row].name == item.name)
                        self.activate (row);
                    break;

                default:
                    if (result >= padBase && result < padBase + DrumKits::numPads && self.session.hasSong())
                    {
                        const auto padTrack = self.getTargetTrack ? self.getTargetTrack() : -1;
                        const auto file = self.soundFile (item);

                        if (auto* drums = self.session.getDrumMachine (padTrack); drums != nullptr && file != juce::File())
                            if (! self.session.loadDrumPadSample (*drums, result - padBase, file))
                                self.problem ("Could not load the pad", self.session.getLastError());
                    }
                    break;
            }
        });
    }

    juce::String BrowserPanel::getTooltipForRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return {};

        const auto& item = items[(size_t) row];

        switch (item.kind)
        {
            case Kind::header:      return item.file != juce::File() ? item.file.getFullPathName() + " (right-click for more)" : juce::String();
            case Kind::kit:         return "Double-click: load " + item.name + " on the highlighted drum track (or a new drum track)";
            case Kind::instrument:  return "Double-click: new track with " + item.name;
            case Kind::effect:      return "Double-click: add " + item.name + " to the highlighted track";
            case Kind::folder:      return "Double-click: open " + item.file.getFullPathName();
            case Kind::up:          return "Double-click: up to " + item.file.getFullPathName();
            case Kind::addFolder:   return "Pick a folder of sounds to list here (the files aren't moved or copied)";
            case Kind::savePreset:  return item.detail;
            case Kind::preset:
                return item.name + ": " + item.detail + (item.preset.kind == Session::PresetKind::songTemplate ? " (double-click: a new song from it)"
                                                       : item.preset.kind == Session::PresetKind::trackPreset  ? " (double-click: a new track)"
                                                                                                               : " (double-click: on the highlighted track)");
            case Kind::sample:
            case Kind::file:
            case Kind::kitSound:    return item.name + ": click to hear it, double-click to add at the playhead, drag onto a track or a drum pad, right-click for more";
        }

        return {};
    }

    juce::var BrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        juce::StringArray paths;

        for (int i = 0; i < rows.size(); ++i)
        {
            const auto row = rows[i];

            // Never an error box here: it's asked again on every mouse move until a drag starts
            if (juce::isPositiveAndBelow (row, (int) items.size()) && items[(size_t) row].isSound())
                if (const auto file = soundFile (items[(size_t) row], false); file != juce::File())
                    paths.add (file.getFullPathName());
        }

        return BrowserDrag::describe (paths);
    }

    void BrowserPanel::timerCallback()
    {
        // The preview's "playing" mark goes when the sound ends
        const auto now = audioEngine.isPreviewing() ? audioEngine.getPreviewFile() : juce::File();

        if (now != lastPreviewing)
        {
            lastPreviewing = now;
            list.repaint();
        }
    }

    void BrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        const auto& item = items[(size_t) row];
        auto r = juce::Rectangle<int> (width, height).reduced (6, 1);

        if (item.kind == Kind::header)
        {
            // A header that is a sentence ("No plugins yet: ...") is a hint, not a section title
            if ((item.name.contains (": ") && ! item.name.startsWith ("Built-in: ")) || item.name.startsWith ("Nothing matches"))
            {
                auto hint = r.withTrimmedTop (2).toFloat();
                g.setColour (Theme::raised.withAlpha (0.6f));
                g.fillRoundedRectangle (hint, (float) Theme::smallRadius);
                auto text = hint.toNearestInt().reduced (8, 0);
                Icons::draw (g, Icon::help, text.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), Theme::textDim, 1.5f);
                text.removeFromLeft (6);
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawFittedText (item.name, text, juce::Justification::centredLeft, 2, 0.9f);
                return;
            }

            drawCaption (g, item.name, r.withTrimmedLeft (4).withTrimmedBottom (4), juce::Justification::bottomLeft);
            return;
        }

        // A built-in drum sound's file is "<kit>/NN <pad>.wav" (made on first use)
        const bool playing = item.isSound() && lastPreviewing != juce::File()
                              && (item.kind == Kind::kitSound ? lastPreviewing.getParentDirectory().getFileName() == juce::String (DrumKits::getKit (item.kitIndex).name)
                                                                 && lastPreviewing.getFileNameWithoutExtension().startsWith (juce::String (item.pad + 1).paddedLeft ('0', 2) + " ")
                                                              : lastPreviewing == item.file);

        if (selected || playing)
        {
            g.setColour (playing ? Theme::accent.withAlpha (0.25f) : Theme::selectedRow);
            g.fillRoundedRectangle (r.toFloat(), (float) Theme::cornerRadius);
        }

        r.removeFromLeft (8);
        Icon icon = Icon::plug;

        switch (item.kind)
        {
            case Kind::kit:                         icon = Icon::drum; break;
            case Kind::instrument:                  icon = Icon::piano; break;
            case Kind::folder: case Kind::up:       icon = Icon::open; break;
            case Kind::addFolder: case Kind::savePreset: icon = Icon::plus; break;
            case Kind::preset:
                icon = item.preset.kind == Session::PresetKind::songTemplate ? Icon::layers
                     : item.preset.kind == Session::PresetKind::trackPreset  ? Icon::bookmark : Icon::chain;
                break;
            case Kind::sample: case Kind::file:
            case Kind::kitSound:                    icon = playing ? Icon::speaker : item.isLoop ? Icon::loop : Icon::note; break;
            case Kind::effect: case Kind::header:   break;
        }

        // The icon on a small tile, like a file in a library
        {
            const auto tile = r.removeFromLeft (22).toFloat().withSizeKeepingCentre (22.0f, 22.0f);
            g.setColour (playing ? Theme::accent.withAlpha (0.2f) : selected ? Theme::hover : Theme::raised);
            g.fillRoundedRectangle (tile, (float) Theme::smallRadius);
            Icons::draw (g, icon, tile.withSizeKeepingCentre (13.0f, 13.0f), playing ? Theme::accent : selected ? Theme::text : Theme::textDim, 1.6f);
        }

        r.removeFromLeft (8);

        auto top = r.removeFromTop (r.getHeight() / 2 + 1);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, selected ? Theme::Weight::semibold : Theme::Weight::medium));
        g.drawText (item.name, top, juce::Justification::bottomLeft, true);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize - 0.5f));
        g.drawText (item.detail, r.withTrimmedTop (1), juce::Justification::topLeft, true);
    }

    //==============================================================================
    juce::String BrowserPanel::categoryTitle() const
    {
        if (search.getText().trim().isNotEmpty())
            return "Search";

        switch (current)
        {
            case Category::templates:       return "Song templates";
            case Category::trackPresets:    return "Track presets";
            case Category::chains:          return "Effect chains";
            case Category::kits:            return "Drum kits";
            case Category::instruments:     return "Instruments";
            case Category::effects:         return "Effects";
            case Category::samples:         return "Samples & loops";
            case Category::files:           return "Files";
        }

        return {};
    }

    void BrowserPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, true));
        g.drawText ("Browser", titleArea, juce::Justification::centredLeft, false);

        Icons::draw (g, Icon::search, searchIconArea.toFloat().withSizeKeepingCentre (13.0f, 13.0f), Theme::textDim);

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (listTitleArea.getY() - 1, 0.0f, (float) getWidth() - 1);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, true));
        g.drawText (categoryTitle(), listTitleArea, juce::Justification::centredLeft, false);

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth() - 1);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));

        const auto searching = search.getText().trim().isNotEmpty();
        g.drawText (searching                              ? "Double-click to use, Esc to clear"
                    : current == Category::kits            ? "Double-click to load on a drum track"
                    : current == Category::instruments     ? "Double-click to add as a new track"
                    : current == Category::effects         ? "Double-click to add to the highlighted track"
                    : current == Category::templates       ? "Double-click to start a new song from it"
                    : current == Category::trackPresets    ? "Double-click to add as a new track"
                    : current == Category::chains          ? "Double-click to add to the highlighted track"
                                                           : juce::String (juce::CharPointer_UTF8 ("Click: hear it \xc2\xb7 double-click or drag: add")),
                    footerArea.withTrimmedRight (34), juce::Justification::centredLeft, true);
    }

    void BrowserPanel::paintOverChildren (juce::Graphics& g)
    {
        // Chips sit inside the rows (on the right) rather than as corner badges: the rows are full width
        for (auto* row : categoryRows)
            if (Planned::isPlanned (*row))
                Planned::drawChip (g, row->getProperties()["ezPlannedPhase"].toString(),
                                   { row->getRight() - 26, row->getY() + (row->getHeight() - 14) / 2 });
    }

    void BrowserPanel::resized()
    {
        auto r = getLocalBounds().withTrimmedRight (1);

        auto title = r.removeFromTop (Theme::panelTitleHeight);
        closeButton.setBounds (title.removeFromRight (30).withSizeKeepingCentre (24, 24).translated (-4, 0));
        titleArea = title.withTrimmedLeft (12);

        search.setBounds (r.removeFromTop (26).reduced (10, 0));
        searchIconArea = search.getBounds().withWidth (24).translated (2, 0);
        r.removeFromTop (8);

        auto categories = r.removeFromTop ((int) categoryRows.size() * (rowHeight + 1) + 8).reduced (6, 0);

        for (auto* row : categoryRows)
        {
            row->setBounds (categories.removeFromTop (rowHeight));
            categories.removeFromTop (1);
        }

        auto footer = r.removeFromBottom (footerHeight);
        footerArea = footer.withTrimmedLeft (12);
        previewButton.setBounds (footer.removeFromRight (34).withSizeKeepingCentre (26, 26));
        listTitleArea = r.removeFromTop (30).withTrimmedLeft (12);
        list.setBounds (r);
    }
}
