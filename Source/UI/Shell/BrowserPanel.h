/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The left-hand browser: categories on top, their contents below.

      Search (Ctrl+F)    finds instruments, effects, drum kits, samples and files in the open folder
      Instruments        double-click: a new instrument track
      Effects            double-click: on the highlighted track
      Drum kits          double-click: on the highlighted drum track (or a new drum track)
      Samples & loops    your folders (Documents\EzDAWsIt\Samples + any you add) and the built-in drum sounds
      Files              your computer: places, then folders and sound files

    Sounds: click to preview (switch it off in the footer), double-click to put on the highlighted
    track at the playhead, drag onto a track or a drum pad, right-click for everything else.
    Templates, track presets and effect chains are planned and shown greyed.
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

        /** Ctrl+F: the cursor in the search box. */
        void focusSearch();

        SampleLibrary& getSampleLibrary() noexcept      { return library; }

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        enum class Category { templates, trackPresets, chains, instruments, effects, kits, samples, files };
        enum class Kind { header, instrument, effect, kit, kitSound, sample, file, folder, up, addFolder, preset, savePreset };

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

            bool isSound() const noexcept   { return kind == Kind::sample || kind == Kind::file || kind == Kind::kitSound; }
        };

        class CategoryRow;

        AudioEngine& audioEngine;
        Session& session;
        SampleLibrary library;
        Category current = Category::instruments;
        juce::File filesFolder;
        juce::OwnedArray<CategoryRow> categoryRows;
        IconButton closeButton { "Hide browser", Icon::close, {}, IconButton::Style::ghost };
        IconButton previewButton { "Preview on click", Icon::speaker, {}, IconButton::Style::ghost };
        juce::TextEditor search;
        juce::ListBox list;
        std::vector<Item> items;
        juce::Rectangle<int> titleArea, listTitleArea, footerArea, searchIconArea;
        std::unique_ptr<juce::FileChooser> chooser;
        bool choosingFolder = false;
        struct Child { juce::File file; bool isFolder; juce::int64 size; };
        std::vector<Child> folderListing;
        juce::File listedFolder;
        juce::File lastPreviewing;

        void selectCategory (Category);
        void rebuildItems();
        void addCategoryItems (Category, const juce::String& query);
        void activate (int row);
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
        void problem (const juce::String& title, const juce::String& message);

        // ListBoxModel
        int getNumRows() override                                   { return (int) items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override     { activate (row); }
        void returnKeyPressed (int row) override                    { activate (row); }
        juce::String getTooltipForRow (int row) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;   // a plugin scan, a sample scan
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
    };

    /** Dragging sounds out of the browser: the description is "ezfiles:" then one path per line. */
    namespace BrowserDrag
    {
        juce::var describe (const juce::StringArray& paths);
        juce::StringArray filesIn (const juce::var& description);   // empty if it isn't ours
    }
}
