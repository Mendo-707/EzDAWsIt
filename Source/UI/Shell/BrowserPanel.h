/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The left-hand browser: categories on top (two columns; fold them away with the chevron), their
    contents below, a preview strip and controls at the bottom.

      Favourites         anything starred (the star on a row, right-click, or Ctrl+K)
      Recent             the last things you used, newest first
      Templates          double-click: a new song from it
      Track presets      double-click: a new track set up for it
      Effect chains      double-click: on the highlighted track
      Sounds             the built-in synth's sounds by type: double-click for a new track with it
      Instruments        double-click: a new instrument track (the EzDAW plug-ins sit with the built-ins)
      Effects            by what they do; the arrow shows an effect's presets
      MIDI patterns      chords, basslines, melodies and drum beats: in your song's key, at the playhead
      Drum kits          double-click: on the highlighted drum track (or a new one)
      Samples & loops    your folders and the built-in drum sounds; chips filter loops, one-shots, drums...
      This song          the audio in the song's folder: recordings, imports, bounces
      Files              your computer: places, then folders and sound files
      Learn              tutorials and guides (opens the help guide)

    Search (Ctrl+F) looks everywhere; Up/Down move through the results, Enter uses one, Ctrl+Enter
    hears a sound, Shift+Enter puts it on a new track, Esc clears. Click a section title to fold it.
    Sounds: click to preview (switch it off in the footer), double-click to put on the highlighted
    track at the playhead, drag onto a track or a drum pad. Patterns, instruments and effects drag
    onto the timeline too. Right-click anything for everything else.
*/

#pragma once

#include "../../Engine/Session.h"
#include "../../Engine/SampleLibrary.h"
#include "../Widgets.h"

namespace ez
{
    class AudioEngine;

    class BrowserPanel : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::ChangeListener,
                         private juce::Timer
    {
    public:
        BrowserPanel (AudioEngine&, Session&);
        ~BrowserPanel() override;

        /** Which track things go on (the timeline's highlighted track). */
        std::function<int()> getTargetTrack;
        std::function<void()> onClose;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;
        std::function<void (const Session::PresetInfo&)> onNewSongFromTemplate;   // asks for the song's name
        std::function<void (const juce::String& topicId)> onOpenHelp;             // the Learn category

        /** Ctrl+F: the cursor in the search box. */
        void focusSearch();

        /** Ctrl+K: star or unstar the selected item. False if nothing that can be starred is selected. */
        bool toggleFavouriteOfSelection();

        SampleLibrary& getSampleLibrary() noexcept      { return library; }

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        // For the tests and the screenshots
        enum class Category { favourites, recent, templates, trackPresets, chains, sounds, instruments, effects,
                              patterns, kits, samples, song, files, learn };
        void selectCategory (Category);
        juce::StringArray getVisibleNames() const;     // every row's name, headers included
        void setSearchText (const juce::String&);
        bool activateByName (const juce::String& name);   // double-clicks the first row of that name
        bool toggleFoldByName (const juce::String& headerOrEffect);

    private:
        enum class Kind { header, instrument, effect, kit, kitSound, sample, file, folder, up, addFolder, preset, savePreset, pattern, topic };
        enum class SampleFilter { all, loops, oneShots, drums, bass, keys, vocals, guitar, fx };

        struct Item
        {
            Item (Kind k = Kind::header, const juce::String& n = {}, const juce::String& d = {}) : kind (k), name (n), detail (d) {}

            Kind kind = Kind::header;
            juce::String name, detail;
            Session::PluginChoice choice;
            int kitIndex = -1, pad = -1;
            juce::File file;            // a sound or folder (a header: the library folder it heads)
            bool isLoop = false;
            Session::PresetInfo preset; // a preset, or (savePreset) which kind to save
            juce::String id;            // a pattern's or help topic's name/id
            bool indent = false;        // an effect's preset, under the effect
            bool expandable = false;    // an effect with presets (the arrow)
            bool expanded = false;
            bool foldable = false;      // a section title that folds
            bool folded = false;

            bool isSound() const noexcept   { return kind == Kind::sample || kind == Kind::file || kind == Kind::kitSound; }
        };

        class CategoryRow;
        class SearchBox;
        class ChipRow;

        AudioEngine& audioEngine;
        Session& session;
        SampleLibrary library;
        Category current = Category::instruments;
        juce::File filesFolder;
        juce::OwnedArray<CategoryRow> categoryRows;
        IconButton closeButton { "Hide browser", Icon::close, {}, IconButton::Style::ghost };
        IconButton foldButton { "Fold the categories", Icon::chevronUp, {}, IconButton::Style::ghost };
        IconButton previewButton { "Preview on click", Icon::speaker, {}, IconButton::Style::ghost };
        IconButton loopButton { "Loop the preview", Icon::loop, {}, IconButton::Style::ghost };
        juce::Slider previewLevel;
        std::unique_ptr<SearchBox> search;
        std::unique_ptr<ChipRow> chips;
        juce::ListBox list;
        std::vector<Item> items;
        juce::Rectangle<int> titleArea, listTitleArea, footerArea, infoArea, searchIconArea, foldedCategoryArea;
        std::unique_ptr<juce::FileChooser> chooser;
        bool choosingFolder = false, categoriesFolded = false;
        struct Child { juce::File file; bool isFolder; juce::int64 size; };
        std::vector<Child> folderListing;
        juce::File listedFolder;
        juce::File lastPreviewing;
        double lastProgress = -1.0;
        SampleFilter sampleFilter = SampleFilter::all;
        std::set<juce::String> expandedEffects, foldedSections;
        juce::StringArray favourites, recent;   // item keys, newest first for recent

        // The selected sound's waveform
        juce::AudioThumbnailCache thumbnailCache { 8 };
        std::unique_ptr<juce::AudioThumbnail> thumbnail;
        juce::File thumbnailFile;

        void rebuildItems();
        void addCategoryItems (Category, const juce::String& query);
        void addKeyedItems (const juce::StringArray& keys, const juce::String& query);   // favourites, recent
        void addEffectsWithPresets (const juce::Array<Session::PluginChoice>&, const juce::String& category, const juce::StringArray& words,
                                    std::function<void (Item)> add);
        void activate (int row);
        void activateItem (const Item&, bool onNewTrack);
        void showMenu (int row);
        void navigateTo (const juce::File& folder);
        void chooseLibraryFolder();
        juce::File soundFile (const Item&, bool reportProblems = true);   // makes a built-in drum sound if it isn't on disk yet
        void returnFocus();                      // the keyboard back to the app (Space, the shortcuts)
        void listFolder();                       // reads the Files folder (once per visit, not per keystroke)
        void addSound (const Item&, bool onNewTrack);
        void usePreset (const Session::PresetInfo&);
        void savePreset (Session::PresetKind);
        void togglePreview (const Item&);
        bool previewOnClick() const;
        juce::String categoryTitle() const;
        juce::String footerHint() const;
        void problem (const juce::String& title, const juce::String& message);
        void moveSelection (int delta);          // from the search box's arrow keys
        void selectionChanged();                 // the waveform follows the selected sound
        void setCategoriesFolded (bool);
        void toggleFold (int row);

        // Favourites and recent: an item as a string that finds it again
        static juce::String keyOf (const Item&);
        bool isFavourite (const Item&) const;
        void setFavourite (const Item&, bool);
        void remember (const Item&);             // to Recent
        void saveLists();

        static bool sampleMatches (SampleFilter, const SampleLibrary::Entry&);
        static juce::String filterName (SampleFilter);

        // ListBoxModel
        int getNumRows() override                                   { return (int) items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override                    { activate (row); }
        void selectedRowsChanged (int) override                     { selectionChanged(); }
        juce::String getTooltipForRow (int row) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;   // a plugin scan, a sample scan
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
    };

    /** Dragging out of the browser. Sounds: "ezfiles:" then one path per line. Patterns: "ezpattern:<name>".
        Instruments and effects: "ezplugin:" then kind, name, type, file or id, preset on separate lines. */
    namespace BrowserDrag
    {
        juce::var describe (const juce::StringArray& paths);
        juce::StringArray filesIn (const juce::var& description);   // empty if it isn't ours

        juce::var describePattern (const juce::String& name);
        juce::String patternIn (const juce::var& description);      // empty if it isn't a pattern

        juce::var describePlugin (const Session::PluginChoice&, bool isInstrument);
        /** The instrument or effect a drag carries, found again among the session's choices. */
        std::optional<std::pair<Session::PluginChoice, bool>> pluginIn (const juce::var& description, const Session&);
    }
}
