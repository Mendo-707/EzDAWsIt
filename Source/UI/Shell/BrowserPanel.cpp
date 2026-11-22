/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "BrowserPanel.h"
#include "../../Engine/AudioEngine.h"
#include "../../Engine/Effects.h"
#include "../../Engine/DrumKits.h"
#include "../../Engine/DrumMachinePlugin.h"
#include "../../Engine/PluginPresets.h"
#include "../../Engine/MidiPatterns.h"
#include "../TrackMenus.h"
#include "../HelpGuide.h"

namespace ez
{
    namespace
    {
        constexpr int rowHeight = 25, itemRowHeight = 36, footerHeight = 30, infoHeight = 38, maxResultsPerSection = 200;
        constexpr int starZone = 26, arrowZone = 24;   // where a click stars a row, or opens an effect's presets
        const char* previewKey = "ezPreviewOnClick";
        const char* filesFolderKey = "ezBrowserFolder";
        const char* favouritesKey = "ezBrowserFavourites";
        const char* recentKey = "ezBrowserRecent";
        const char* foldedKey = "ezBrowserCategoriesFolded";
        constexpr int maxRecent = 30;
        const juce::String dragPrefix ("ezfiles:"), patternPrefix ("ezpattern:"), pluginPrefix ("ezplugin:");

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
                { "Amp",          "Guitar and bass amps with cabinets" },
                { "Tuner",        "Tune your guitar or bass" },
                { "Arpeggiator",  "Plays held chords as patterns" },
                // The EzDAW plug-ins that ship with the app
                { "AETHER X1 Heritage", "Guitar amp, pedals and cab" },
                { "StudioVoice Pro",    "Vocal channel strip" },
                { "Aurelia 290",        "Piano" },
                { "HexaFold",           "Synth" },
                { "Voxalis",            "Synth" },
                { "RIOT PRESS MK4",     "Bass synth: crushed, resonant, loud" },
                { "BASSED GOD",         "808 bassline workstation: patterns, slides, distortion" },
                { "HOT LAVA 90",        "Lead synth and melody generator, with chords and arps" },
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

        /** A header that is a sentence ("No plugins yet: ...") is a hint, not a section title. */
        bool isHint (const juce::String& header)
        {
            return (header.contains (": ") && ! header.startsWith ("Built-in: ")) || header.startsWith ("Nothing matches");
        }

        juce::String utf8 (const char* text)   { return juce::String (juce::CharPointer_UTF8 (text)); }
        const juce::String dot = utf8 (" \xc2\xb7 ");

        juce::String categoryName (int c)
        {
            static const char* names[] = { "Favourites", "Recent", "Song templates", "Track presets", "Effect chains", "Sounds",
                                           "Instruments", "Effects", "MIDI patterns", "Drum kits", "Samples & loops", "This song",
                                           "Files", "Learn" };
            return juce::isPositiveAndBelow (c, (int) std::size (names)) ? names[c] : "";
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

    juce::var BrowserDrag::describePattern (const juce::String& name)
    {
        return name.isEmpty() ? juce::var() : juce::var (patternPrefix + name);
    }

    juce::String BrowserDrag::patternIn (const juce::var& description)
    {
        const auto text = description.toString();
        return text.startsWith (patternPrefix) ? text.substring (patternPrefix.length()) : juce::String();
    }

    juce::var BrowserDrag::describePlugin (const Session::PluginChoice& c, bool isInstrument)
    {
        juce::StringArray lines { isInstrument ? "instrument" : "effect", c.name, c.pluginType, c.description.fileOrIdentifier, c.preset };
        return juce::var (pluginPrefix + lines.joinIntoString ("\n"));
    }

    std::optional<std::pair<Session::PluginChoice, bool>> BrowserDrag::pluginIn (const juce::var& description, const Session& session)
    {
        const auto text = description.toString();

        if (! text.startsWith (pluginPrefix))
            return std::nullopt;

        juce::StringArray lines;
        lines.addLines (text.substring (pluginPrefix.length()));

        while (lines.size() < 5)
            lines.add ({});

        const bool instrument = lines[0] == "instrument";
        const auto choices = instrument ? session.getAvailableInstruments() : session.getAvailableEffects();

        for (auto c : choices)
            if (c.pluginType == lines[2] && c.description.fileOrIdentifier == lines[3])
            {
                c.preset = lines[4];
                return std::make_pair (c, instrument);
            }

        return std::nullopt;
    }

    //==============================================================================
    /** A category in the two-column grid: compact, the accent bar on the one you're in. */
    class BrowserPanel::CategoryRow : public NavRow
    {
    public:
        CategoryRow (const juce::String& name, Icon i, Category c) : NavRow (name, i, false), category (c), icon (i) {}
        const Category category;
        bool showsMenuChevron = false;   // folded: this row is the category picker

        void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
        {
            const auto on = getToggleState();
            auto r = getLocalBounds();

            if (on || isMouseOver)
            {
                g.setColour (on ? Theme::selectedRow : Theme::hover.withAlpha (0.7f));
                g.fillRoundedRectangle (r.toFloat(), (float) Theme::cornerRadius);
            }

            if (on)
            {
                g.setColour (Theme::accent);
                g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, (float) r.getCentreY() - 6.0f, 3.0f, 12.0f), 1.5f);
            }

            r.removeFromLeft (9);
            Icons::draw (g, icon, r.removeFromLeft (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f), on ? Theme::accent : Theme::textDim);
            r.removeFromLeft (6);

            if (showsMenuChevron)
                Icons::draw (g, Icon::chevronDown, r.removeFromRight (22).toFloat().withSizeKeepingCentre (11.0f, 11.0f), Theme::textDim);

            g.setColour (on || isMouseOver ? Theme::text : Theme::text.withAlpha (0.88f));
            g.setFont (Theme::font (Theme::baseFontSize - 1.0f, on ? Theme::Weight::semibold : Theme::Weight::regular));
            g.drawFittedText (getName(), r, juce::Justification::centredLeft, 1, 0.85f);
        }

    private:
        Icon icon;
    };

    //==============================================================================
    /** The search box: Up/Down move through the results, Enter uses the selected one. */
    class BrowserPanel::SearchBox : public juce::TextEditor
    {
    public:
        std::function<bool (const juce::KeyPress&)> onKey;

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (onKey && onKey (key))
                return true;

            return juce::TextEditor::keyPressed (key);
        }
    };

    //==============================================================================
    /** Filter chips for Samples & loops: one at a time. */
    class BrowserPanel::ChipRow : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void (SampleFilter)> onChange;
        SampleFilter selected = SampleFilter::all;

        int heightFor (int width)
        {
            layout (width);
            return chips.empty() ? 0 : chips.back().second.getBottom() + 4;
        }

        void resized() override     { layout (getWidth()); }

        void paint (juce::Graphics& g) override
        {
            for (const auto& [filter, area] : chips)
            {
                const bool on = filter == selected;
                g.setColour (on ? Theme::accent.withAlpha (0.25f) : Theme::raised);
                g.fillRoundedRectangle (area.toFloat(), (float) area.getHeight() * 0.5f);
                g.setColour (on ? Theme::accent : Theme::laneSeparator);
                g.drawRoundedRectangle (area.toFloat().reduced (0.5f), (float) area.getHeight() * 0.5f, 1.0f);
                g.setColour (on ? Theme::text : Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize, on ? Theme::Weight::semibold : Theme::Weight::regular));
                g.drawText (filterName (filter), area, juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (const auto& [filter, area] : chips)
                if (area.contains (e.getPosition()))
                {
                    selected = filter;
                    repaint();

                    if (onChange)
                        onChange (filter);
                }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            for (const auto& [filter, area] : chips)
                if (area.contains (e.getPosition()))
                    setTooltip (filter == SampleFilter::all ? juce::String ("Every sound")
                                                            : "Only " + filterName (filter).toLowerCase() + " (from the file and folder names)");
        }

    private:
        std::vector<std::pair<SampleFilter, juce::Rectangle<int>>> chips;

        void layout (int width)
        {
            chips.clear();
            int x = 0, y = 0;
            const int h = 20;
            const juce::Font font = Theme::font (Theme::smallFontSize, Theme::Weight::semibold);

            for (auto f : { SampleFilter::all, SampleFilter::loops, SampleFilter::oneShots, SampleFilter::drums, SampleFilter::bass,
                            SampleFilter::keys, SampleFilter::vocals, SampleFilter::guitar, SampleFilter::fx })
            {
                const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, filterName (f))) + 16;

                if (x > 0 && x + w > width)
                {
                    x = 0;
                    y += h + 4;
                }

                chips.push_back ({ f, { x, y, w, h } });
                x += w + 4;
            }
        }
    };

    //==============================================================================
    BrowserPanel::BrowserPanel (AudioEngine& e, Session& s)
        : audioEngine (e), session (s), library (e.getEngine())
    {
        auto& props = audioEngine.getEngine().getPropertyStorage().getPropertiesFile();

        closeButton.setTooltip ("Hide the browser (B)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        categoriesFolded = props.getBoolValue (foldedKey, false);
        foldButton.setTooltip ("Fold the categories into one row, for more room below (click again to unfold)");
        foldButton.onClick = [this] { setCategoriesFolded (! categoriesFolded); };
        foldButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (foldButton);

        search = std::make_unique<SearchBox>();
        search->setTextToShowWhenEmpty ("Search sounds, instruments, effects, help", Theme::textDim);
        search->setIndents (26, 5);
        search->setTooltip ("Ctrl+F. Searches everything: instruments, sounds, effects and their presets, patterns, kits, samples, "
                            "the open folder and the help. Up/Down: choose; Enter: use it; Shift+Enter: on a new track; "
                            "Ctrl+Enter: hear it; Esc: clear");
        search->onTextChange = [this] { rebuildItems(); };
        search->onEscapeKey = [this]
        {
            search->clear();
            rebuildItems();
            returnFocus();
        };
        search->onKey = [this] (const juce::KeyPress& key)
        {
            if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
            {
                moveSelection (key == juce::KeyPress::upKey ? -1 : 1);
                return true;
            }

            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
            {
                moveSelection (key == juce::KeyPress::pageUpKey ? -8 : 8);
                return true;
            }

            if (key.getKeyCode() == juce::KeyPress::returnKey)
            {
                auto row = list.getSelectedRow();

                if (! juce::isPositiveAndBelow (row, (int) items.size()) || items[(size_t) row].kind == Kind::header)
                {
                    row = -1;

                    if (search->getText().trim().isNotEmpty())   // nothing chosen: the first result
                        for (int i = 0; i < (int) items.size() && row < 0; ++i)
                            if (items[(size_t) i].kind != Kind::header)
                                row = i;
                }

                if (row < 0)
                    return true;

                const auto item = items[(size_t) row];

                if (key.getModifiers().isCommandDown())
                {
                    if (item.isSound())
                        togglePreview (item);

                    return true;
                }

                list.selectRow (row);
                returnFocus();
                activateItem (item, key.getModifiers().isShiftDown());
                return true;
            }

            return false;
        };
        addAndMakeVisible (*search);

        struct Def { const char* name; Icon icon; Category category; };
        const Def defs[] =
        {
            { "Favourites",    Icon::star,     Category::favourites },
            { "Recent",        Icon::clock,    Category::recent },
            { "Templates",     Icon::layers,   Category::templates },
            { "Track presets", Icon::bookmark, Category::trackPresets },
            { "Effect chains", Icon::chain,    Category::chains },
            { "Sounds",        Icon::wave,     Category::sounds },
            { "Instruments",   Icon::piano,    Category::instruments },
            { "Effects",       Icon::plug,     Category::effects },
            { "MIDI patterns", Icon::grid,     Category::patterns },
            { "Drum kits",     Icon::drum,     Category::kits },
            { "Samples",       Icon::note,     Category::samples },
            { "This song",     Icon::folder,   Category::song },
            { "Files",         Icon::file,     Category::files },
            { "Learn",         Icon::book,     Category::learn },
        };

        for (const auto& d : defs)
        {
            auto* row = categoryRows.add (new CategoryRow (d.name, d.icon, d.category));
            row->setTooltip (categoryName ((int) d.category));
            row->onClick = [this, row, category = d.category]
            {
                if (row->showsMenuChevron)   // folded: pick another category from a menu
                {
                    juce::PopupMenu menu;

                    for (auto* r : categoryRows)
                        menu.addItem ((int) r->category + 1, categoryName ((int) r->category), true, r->category == current);

                    menu.addSeparator();
                    menu.addItem (1000, "Unfold the categories");
                    juce::Component::SafePointer<BrowserPanel> safe (this);

                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (row), [safe] (int result)
                    {
                        if (safe == nullptr || result == 0)
                            return;

                        if (result == 1000)
                            safe->setCategoriesFolded (false);
                        else
                            safe->selectCategory ((Category) (result - 1));
                    });
                    return;
                }

                selectCategory (category);
            };
            addAndMakeVisible (row);
        }

        chips = std::make_unique<ChipRow>();
        chips->onChange = [this] (SampleFilter f)
        {
            sampleFilter = f;
            rebuildItems();
        };
        addChildComponent (*chips);

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

        loopButton.setTooltip ("Loop the preview: a loop keeps playing until you click it again (or another sound)");
        loopButton.setClickingTogglesState (true);
        loopButton.setToggleState (audioEngine.isPreviewLooping(), juce::dontSendNotification);
        loopButton.onClick = [this]
        {
            audioEngine.setPreviewLooping (loopButton.getToggleState());

            if (audioEngine.isPreviewing())   // the sound playing starts again, looping (or not)
                audioEngine.startPreview (audioEngine.getPreviewFile());
        };
        loopButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (loopButton);

        previewLevel.setSliderStyle (juce::Slider::LinearHorizontal);
        previewLevel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        previewLevel.setRange (-60.0, 0.0, 0.5);
        previewLevel.setDoubleClickReturnValue (true, -6.0);
        previewLevel.setValue (audioEngine.getPreviewLevel(), juce::dontSendNotification);
        previewLevel.onValueChange = [this]
        {
            audioEngine.setPreviewLevel ((float) previewLevel.getValue());
            previewLevel.setTooltip ("Preview level: " + juce::String (previewLevel.getValue(), 1) + " dB (double-click: -6 dB)");
        };
        previewLevel.setTooltip ("Preview level: " + juce::String (previewLevel.getValue(), 1) + " dB (double-click: -6 dB)");
        previewLevel.setWantsKeyboardFocus (false);
        addAndMakeVisible (previewLevel);

        const auto savedFolder = props.getValue (filesFolderKey);
        filesFolder = juce::File::isAbsolutePath (savedFolder) && juce::File (savedFolder).isDirectory() ? juce::File (savedFolder) : defaultFilesFolder();

        favourites.addLines (props.getValue (favouritesKey));
        favourites.removeEmptyStrings();
        recent.addLines (props.getValue (recentKey));
        recent.removeEmptyStrings();

        thumbnail = std::make_unique<juce::AudioThumbnail> (256, audioEngine.getEngine().getAudioFileFormatManager().readFormatManager, thumbnailCache);

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
        startTimerHz (15);
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
        search->grabKeyboardFocus();
        search->selectAll();
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

    void BrowserPanel::setCategoriesFolded (bool shouldFold)
    {
        categoriesFolded = shouldFold;
        audioEngine.getEngine().getPropertyStorage().getPropertiesFile().setValue (foldedKey, shouldFold);
        resized();
        repaint();
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

        if (search->getText().isNotEmpty())
            search->clear();   // picking a category is leaving the search

        rebuildItems();
        resized();   // the chips come and go, the folded row shows the new name
        repaint();
    }

    void BrowserPanel::setSearchText (const juce::String& text)
    {
        search->setText (text, true);
        rebuildItems();
    }

    juce::StringArray BrowserPanel::getVisibleNames() const
    {
        juce::StringArray names;

        for (const auto& i : items)
            names.add (i.name);

        return names;
    }

    bool BrowserPanel::activateByName (const juce::String& name)
    {
        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].kind != Kind::header && items[(size_t) i].name == name)
            {
                list.selectRow (i);
                activate (i);
                return true;
            }

        return false;
    }

    bool BrowserPanel::toggleFoldByName (const juce::String& name)
    {
        for (int i = 0; i < (int) items.size(); ++i)
            if ((items[(size_t) i].foldable || items[(size_t) i].expandable) && items[(size_t) i].name.upToFirstOccurrenceOf ("  (", false, false) == name)
            {
                toggleFold (i);
                return true;
            }

        return false;
    }

    void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        // Only when it changes what's shown
        const bool searching = search->getText().trim().isNotEmpty();
        const bool keyed = current == Category::favourites || current == Category::recent;
        const bool presets = current == Category::templates || current == Category::trackPresets || current == Category::chains;
        const bool shown = source == &library                       ? (current == Category::samples || searching || keyed)
                         : source == &session.getPresetBroadcaster() ? (presets || searching || keyed)
                                                                     : (current == Category::instruments || current == Category::effects || searching || keyed);

        if (shown)
            rebuildItems();
    }

    //==============================================================================
    void BrowserPanel::rebuildItems()
    {
        // Keep the selected row selected (by what it is) through a rebuild
        juce::String selectedKey;

        if (const auto row = list.getSelectedRow(); juce::isPositiveAndBelow (row, (int) items.size()))
            selectedKey = keyOf (items[(size_t) row]) + "|" + items[(size_t) row].name;

        items.clear();
        const auto query = search->getText().trim();

        if (query.isEmpty())
        {
            addCategoryItems (current, {});

            // Section titles fold: skip what's under a folded one
            std::vector<Item> shown;
            bool skipping = false;

            for (size_t i = 0; i < items.size(); ++i)
            {
                auto item = items[i];

                if (item.kind == Kind::header)
                {
                    const auto key = juce::String ((int) current) + "|" + item.name.upToFirstOccurrenceOf ("  (", false, false);
                    const bool hasContent = i + 1 < items.size() && items[i + 1].kind != Kind::header;
                    item.folded = foldedSections.count (key) > 0;
                    item.foldable = ! isHint (item.name) && (hasContent || item.folded);
                    skipping = item.foldable && item.folded;
                    shown.push_back (item);
                    continue;
                }

                if (! skipping)
                    shown.push_back (item);
            }

            items = std::move (shown);
        }
        else
        {
            // Everything that matches, a section per kind (only those with something in them)
            for (auto c : { Category::templates, Category::trackPresets, Category::chains, Category::sounds, Category::instruments,
                            Category::effects, Category::patterns, Category::kits, Category::samples, Category::song, Category::files,
                            Category::learn })
            {
                const auto before = items.size();
                addCategoryItems (c, query);

                if (items.size() == before)
                    continue;

                auto title = categoryName ((int) c);

                if (c == Category::files)
                    title << " in " << filesFolder.getFileName();

                items.insert (items.begin() + (std::ptrdiff_t) before, Item { Kind::header, title });
            }

            if (items.empty())
                items.push_back ({ Kind::header, "Nothing matches \"" + query + "\"" });
        }

        list.updateContent();
        list.deselectAllRows();

        if (selectedKey.isNotEmpty())
            for (int i = 0; i < (int) items.size(); ++i)
                if (items[(size_t) i].kind != Kind::header && keyOf (items[(size_t) i]) + "|" + items[(size_t) i].name == selectedKey)
                {
                    list.selectRow (i, true);
                    break;
                }

        list.repaint();
        repaint (listTitleArea);
        repaint (infoArea);
    }

    void BrowserPanel::addEffectsWithPresets (const juce::Array<Session::PluginChoice>& choices, const juce::String& category,
                                              const juce::StringArray& words, std::function<void (Item)> add)
    {
        const bool searching = ! words.isEmpty();

        for (const auto& c : choices)
        {
            if (c.category != category)
                continue;

            const auto presets = c.bundled ? std::vector<PluginPresets::Preset>() : PluginPresets::getFactory (c.pluginType);
            const auto hint = describeBuiltIn (c.name);
            Item item { Kind::effect, c.name, hint.isNotEmpty() ? hint : juce::String ("EzDAW plug-in") };
            item.choice = c;
            item.expandable = ! presets.empty() && ! searching;
            item.expanded = item.expandable && expandedEffects.count (c.pluginType + "|" + c.name) > 0;

            if (! presets.empty())
                item.detail << dot << (int) presets.size() << " presets";

            add (item);

            if (presets.empty() || (! searching && ! item.expanded))
                continue;

            for (const auto& p : presets)
            {
                Item child { Kind::effect, p.name, c.name + " preset" };
                child.choice = c;
                child.choice.preset = p.name;
                child.indent = ! searching;
                add (child);
            }
        }
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
            case Category::favourites:
            {
                if (favourites.isEmpty())
                    items.push_back ({ Kind::header, "Nothing starred yet: click the star at the right of any row (or Ctrl+K) to keep it here" });
                else
                    addKeyedItems (favourites, query);

                return;
            }

            case Category::recent:
            {
                if (recent.isEmpty())
                    items.push_back ({ Kind::header, "Nothing used yet: what you add from the browser shows here, newest first" });
                else
                    addKeyedItems (recent, query);

                return;
            }

            case Category::sounds:
            {
                // The built-in synth's sounds by type: a new track with the sound
                juce::String lastCategory;

                for (const auto& p : PluginPresets::getFactory (te::FourOscPlugin::xmlTypeName))
                {
                    if (p.category != lastCategory)
                    {
                        lastCategory = p.category;
                        add ({ Kind::header, p.category });
                    }

                    Item item { Kind::instrument, p.name, p.category + " sound" + dot + "built-in synth" };
                    item.choice = { "Synth (4OSC)", te::FourOscPlugin::xmlTypeName, {} };
                    item.choice.preset = p.name;
                    add (item);
                }

                return;
            }

            case Category::patterns:
            {
                MidiPatterns::Kind lastKind {};
                bool first = true;

                for (const auto& p : MidiPatterns::getAll())
                {
                    if (first || p.kind != lastKind)
                    {
                        add ({ Kind::header, MidiPatterns::kindName (p.kind) });
                        lastKind = p.kind;
                        first = false;
                    }

                    const auto bars = juce::roundToInt (p.lengthBeats / 4.0);
                    Item item { Kind::pattern, p.name, p.description + dot + juce::String (bars) + (bars == 1 ? " bar" : " bars") };
                    item.id = p.name;
                    add (item);
                }

                return;
            }

            case Category::learn:
            {
                for (const auto& section : HelpGuide::sections())
                {
                    add ({ Kind::header, section });

                    for (const auto& t : HelpGuide::topics())
                        if (t.section == section)
                        {
                            Item item { Kind::topic, t.title, t.summary };
                            item.id = t.id;
                            add (item);
                        }
                }

                return;
            }

            case Category::song:
            {
                if (! session.hasSong())
                {
                    if (! searching)
                        items.push_back ({ Kind::header, "No song open: open or start one to see its audio here" });

                    return;
                }

                const auto folder = session.getSongFile().getParentDirectory();
                int total = 0;

                const auto addFolder = [&] (const juce::File& dir, const juce::String& title, bool recursive)
                {
                    auto files = dir.findChildFiles (juce::File::findFiles, recursive, "*");
                    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                    {
                        return a.getLastModificationTime() > b.getLastModificationTime();   // newest first
                    });

                    Item header { Kind::header, title };
                    header.file = dir;
                    bool shownHeader = false;

                    for (const auto& f : files)
                    {
                        if (! f.hasFileExtension (SampleLibrary::audioExtensions) || f.getFileName().startsWith ("."))
                            continue;

                        if (! std::exchange (shownHeader, true))
                            add (header);

                        Item item { Kind::file, f.getFileNameWithoutExtension(),
                                    sizeText (f.getSize()) + dot + f.getLastModificationTime().toString (true, true, false) };
                        item.file = f;
                        add (item);
                        ++total;
                    }
                };

                addFolder (folder, "In the song folder", false);

                for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, false, "*"))
                    if (! sub.getFileName().startsWith ("."))
                        addFolder (sub, sub.getFileName(), true);

                if (total == 0 && ! searching)
                {
                    Item hint { Kind::header, "Nothing yet: recordings, imported files and bounces of this song show here" };
                    hint.file = folder;
                    items.push_back (hint);
                }

                return;
            }

            case Category::kits:
            {
                for (auto& genre : DrumKits::getGenres())
                {
                    add ({ Kind::header, genre });

                    for (int k = 0; k < DrumKits::getNumKits(); ++k)
                        if (genre == DrumKits::getKit (k).genre)
                        {
                            Item item { Kind::kit, DrumKits::getKit (k).name, utf8 ("16 pads \xc2\xb7 ") + juce::String (DrumKits::getKit (k).description) };
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
                bool shownPluginHeader = false;

                if (instruments)
                {
                    add ({ Kind::header, "Built-in" });

                    for (const auto& c : choices)
                    {
                        if (c.pluginType == te::ExternalPlugin::xmlTypeName && ! c.bundled)
                            continue;

                        const auto hint = describeBuiltIn (c.name);
                        Item item { Kind::instrument, c.name, hint.isNotEmpty() ? hint : juce::String ("EzDAW plug-in") };
                        item.choice = c;
                        add (item);
                    }
                }
                else
                {
                    for (const auto& effectCategory : Effects::categories())   // by what they do
                    {
                        const auto before = items.size();
                        addEffectsWithPresets (choices, effectCategory, words, add);

                        if (items.size() != before && ! searching)
                            items.insert (items.begin() + (std::ptrdiff_t) before, Item { Kind::header, effectCategory });
                    }
                }

                for (const auto& c : choices)
                {
                    if (c.pluginType != te::ExternalPlugin::xmlTypeName || c.bundled)
                        continue;   // listed above

                    if (! std::exchange (shownPluginHeader, true))
                        add ({ Kind::header, "VST3 (scanned)" });

                    Item item { instruments ? Kind::instrument : Kind::effect, c.name, c.description.manufacturerName };
                    item.choice = c;
                    add (item);
                }

                if (! shownPluginHeader && ! searching)
                    items.push_back ({ Kind::header, "Your own plug-ins: Setup > Plugins scans them" });

                return;
            }

            case Category::samples:
            {
                if (! searching)
                    items.push_back ({ Kind::addFolder, "Add a folder of samples...", "Your sample packs: they stay where they are" });

                // Each library folder, then the built-in drum sounds
                const auto& entries = library.getEntries();
                const auto filter = searching ? SampleFilter::all : sampleFilter;

                for (const auto& folder : library.getFolders())
                {
                    int inFolder = 0;

                    for (const auto& e : entries)
                        if (e.folder == folder && sampleMatches (filter, e))
                            ++inFolder;

                    Item header { Kind::header, folder == SampleLibrary::getOwnFolder() ? juce::String ("My samples") : folder.getFileName() };
                    header.file = folder;

                    if (! searching)
                        header.name << (library.isScanning() ? juce::String ("  (looking...)") : "  (" + juce::String (inFolder) + ")");

                    add (header);

                    if (! searching && inFolder == 0 && ! library.isScanning() && folder == SampleLibrary::getOwnFolder() && filter == SampleFilter::all)
                        items.push_back ({ Kind::header, "Empty: put sounds in Documents\\EzDAWsIt\\Samples" });

                    for (const auto& e : entries)
                        if (e.folder == folder && sampleMatches (filter, e))
                        {
                            Item item { Kind::sample, e.file.getFileNameWithoutExtension(), e.describe() };
                            item.file = e.file;
                            item.isLoop = e.isLoop;
                            add (item);
                        }
                }

                if (filter == SampleFilter::all || filter == SampleFilter::oneShots || filter == SampleFilter::drums)
                    for (int k = 0; k < DrumKits::getNumKits(); ++k)
                    {
                        add ({ Kind::header, "Built-in: " + juce::String (DrumKits::getKit (k).name) });

                        for (int pad = 0; pad < DrumKits::numPads; ++pad)
                        {
                            Item item { Kind::kitSound, DrumKits::getPadName (pad), juce::String (DrumKits::getKit (k).name) + dot + "one-shot" };
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

    void BrowserPanel::addKeyedItems (const juce::StringArray& keys, const juce::String& query)
    {
        // Everything the keys could name, without the user's folds or filters
        std::vector<Item> listed;
        std::swap (listed, items);
        const auto savedFilter = std::exchange (sampleFilter, SampleFilter::all);
        const auto savedExpanded = expandedEffects;

        for (const auto& c : session.getAvailableEffects())
            expandedEffects.insert (c.pluginType + "|" + c.name);

        for (auto c : { Category::templates, Category::trackPresets, Category::chains, Category::sounds, Category::instruments,
                        Category::effects, Category::patterns, Category::kits, Category::samples, Category::learn })
            addCategoryItems (c, {});

        expandedEffects = savedExpanded;
        sampleFilter = savedFilter;

        std::map<juce::String, Item> byKey;

        for (auto& item : items)
            if (const auto key = keyOf (item); key.isNotEmpty())
                byKey.emplace (key, item);

        items = std::move (listed);

        juce::StringArray words;
        words.addTokens (query, " ", "\"");
        words.removeEmptyStrings();

        for (const auto& key : keys)
        {
            std::optional<Item> item;

            if (auto found = byKey.find (key); found != byKey.end())
            {
                item = found->second;
            }
            else if (key.startsWith ("file|") || key.startsWith ("folder|"))   // anywhere on the computer
            {
                const juce::File f (key.fromFirstOccurrenceOf ("|", false, false));

                if (key.startsWith ("file|") && f.existsAsFile())
                {
                    item = Item { Kind::file, f.getFileNameWithoutExtension(), f.getParentDirectory().getFullPathName() };
                    item->file = f;
                }
                else if (key.startsWith ("folder|") && f.isDirectory())
                {
                    item = Item { Kind::folder, f.getFileName(), f.getFullPathName() };
                    item->file = f;
                }
            }

            if (! item.has_value())
                continue;   // gone (a deleted preset, an uninstalled plug-in): kept in the list in case it comes back

            item->indent = item->expandable = item->expanded = false;

            if (item->kind == Kind::effect && item->choice.preset.isNotEmpty())
                item->name = item->choice.name + ": " + item->choice.preset;   // "Reverb: Hall"

            if (words.isEmpty() || matches (words, item->name, item->detail))
                items.push_back (*item);
        }

        if (items.empty())
            items.push_back ({ Kind::header, words.isEmpty() ? juce::String ("Nothing here is installed any more: star something new")
                                                             : "Nothing matches \"" + query + "\"" });
    }

    //==============================================================================
    juce::String BrowserPanel::keyOf (const Item& item)
    {
        switch (item.kind)
        {
            case Kind::instrument:
            case Kind::effect:
                return juce::String (item.kind == Kind::instrument ? "ins|" : "fx|") + item.choice.pluginType + "|"
                       + item.choice.description.fileOrIdentifier + "|" + item.choice.name + "|" + item.choice.preset;
            case Kind::kit:         return "kit|" + juce::String (DrumKits::getKit (item.kitIndex).name);
            case Kind::kitSound:    return "kitsound|" + juce::String (DrumKits::getKit (item.kitIndex).name) + "|" + juce::String (item.pad);
            case Kind::sample:
            case Kind::file:        return "file|" + item.file.getFullPathName();
            case Kind::folder:      return "folder|" + item.file.getFullPathName();
            case Kind::preset:      return "preset|" + juce::String ((int) item.preset.kind) + "|" + item.preset.name + "|"
                                           + (item.preset.isBuiltIn() ? juce::String() : item.preset.file.getFullPathName());
            case Kind::pattern:     return "pattern|" + item.id;
            case Kind::topic:       return "topic|" + item.id;
            case Kind::header: case Kind::up: case Kind::addFolder: case Kind::savePreset: break;
        }

        return {};
    }

    bool BrowserPanel::isFavourite (const Item& item) const
    {
        const auto key = keyOf (item);
        return key.isNotEmpty() && favourites.contains (key);
    }

    void BrowserPanel::setFavourite (const Item& item, bool shouldBe)
    {
        const auto key = keyOf (item);

        if (key.isEmpty())
            return;

        favourites.removeString (key);

        if (shouldBe)
            favourites.add (key);

        saveLists();

        if (current == Category::favourites)
            rebuildItems();
        else
            list.repaint();
    }

    bool BrowserPanel::toggleFavouriteOfSelection()
    {
        const auto row = list.getSelectedRow();

        if (! juce::isPositiveAndBelow (row, (int) items.size()) || keyOf (items[(size_t) row]).isEmpty())
            return false;

        const auto item = items[(size_t) row];
        setFavourite (item, ! isFavourite (item));
        return true;
    }

    void BrowserPanel::remember (const Item& item)
    {
        const auto key = keyOf (item);

        if (key.isEmpty())
            return;

        recent.removeString (key);
        recent.insert (0, key);

        while (recent.size() > maxRecent)
            recent.remove (recent.size() - 1);

        saveLists();

        if (current == Category::recent)
            rebuildItems();
    }

    void BrowserPanel::saveLists()
    {
        auto& props = audioEngine.getEngine().getPropertyStorage().getPropertiesFile();
        props.setValue (favouritesKey, favourites.joinIntoString ("\n"));
        props.setValue (recentKey, recent.joinIntoString ("\n"));
    }

    //==============================================================================
    juce::String BrowserPanel::filterName (SampleFilter f)
    {
        switch (f)
        {
            case SampleFilter::all:       return "All";
            case SampleFilter::loops:     return "Loops";
            case SampleFilter::oneShots:  return "One-shots";
            case SampleFilter::drums:     return "Drums";
            case SampleFilter::bass:      return "Bass";
            case SampleFilter::keys:      return "Keys & synths";
            case SampleFilter::vocals:    return "Vocals";
            case SampleFilter::guitar:    return "Guitar";
            case SampleFilter::fx:        return "FX";
        }

        return {};
    }

    bool BrowserPanel::sampleMatches (SampleFilter f, const SampleLibrary::Entry& e)
    {
        // Its name and the folders it's in, inside the library folder ("Drums/Kicks/Kick 01.wav")
        const auto text = e.file.getRelativePathFrom (e.folder).toLowerCase();
        const auto any = [&text] (std::initializer_list<const char*> words)
        {
            for (auto* w : words)
                if (text.contains (w))
                    return true;
            return false;
        };

        switch (f)
        {
            case SampleFilter::all:       return true;
            case SampleFilter::loops:     return e.isLoop;
            case SampleFilter::oneShots:  return ! e.isLoop;
            case SampleFilter::drums:     return any ({ "kick", "snare", "hat", "clap", "perc", "drum", "tom", "cymbal", "ride", "crash",
                                                        "break", "rim", "snap", "shaker", "top loop", "808" });
            case SampleFilter::bass:      return any ({ "bass", "808", "sub" });
            case SampleFilter::keys:      return any ({ "piano", "keys", "rhodes", "organ", "synth", "chord", "pad", "pluck", "lead", "arp" });
            case SampleFilter::vocals:    return any ({ "vocal", "vox", "voice", "acapella", "adlib", "choir", "chant" });
            case SampleFilter::guitar:    return any ({ "guitar", "gtr", "strum" });
            case SampleFilter::fx:        return any ({ "fx", "riser", "impact", "sweep", "noise", "foley", "downlifter", "uplifter",
                                                        "transition", "whoosh", "texture" });
        }

        return true;
    }

    //==============================================================================
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
        else
            remember (item);
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

        if (current != Category::files || search->getText().isNotEmpty())
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

    void BrowserPanel::toggleFold (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        const auto& item = items[(size_t) row];

        if (item.expandable)
        {
            const auto key = item.choice.pluginType + "|" + item.choice.name;

            if (! expandedEffects.erase (key))
                expandedEffects.insert (key);
        }
        else if (item.foldable)
        {
            const auto key = juce::String ((int) current) + "|" + item.name.upToFirstOccurrenceOf ("  (", false, false);

            if (! foldedSections.erase (key))
                foldedSections.insert (key);
        }
        else
        {
            return;
        }

        rebuildItems();
    }

    void BrowserPanel::activate (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        // A copy: loading a plugin can run the message loop (licence boxes), which may rebuild the list
        const auto item = items[(size_t) row];
        activateItem (item, false);
    }

    void BrowserPanel::activateItem (const Item& item, bool onNewTrack)
    {
        switch (item.kind)
        {
            case Kind::header:      return;
            case Kind::preset:      usePreset (item.preset); remember (item); return;
            case Kind::savePreset:  savePreset (item.preset.kind); return;
            case Kind::addFolder:   chooseLibraryFolder(); return;
            case Kind::folder:
            case Kind::up:          navigateTo (item.file); return;
            case Kind::sample:
            case Kind::file:
            case Kind::kitSound:    addSound (item, onNewTrack); return;
            case Kind::topic:       if (onOpenHelp) onOpenHelp (item.id); remember (item); return;
            case Kind::instrument:
            case Kind::effect:
            case Kind::kit:
            case Kind::pattern:     break;
        }

        if (! session.hasSong())
        {
            problem ("No song open", "Open a song or start a new one first (Ctrl+N).");
            return;
        }

        const auto track = onNewTrack ? -1 : (getTargetTrack ? getTargetTrack() : -1);
        bool ok = true;

        if (item.kind == Kind::kit)
        {
            // On the highlighted track if it has a Drum Machine, otherwise on a new drum track
            if (auto* drums = session.getDrumMachine (track))
                ok = session.loadDrumKit (*drums, item.kitIndex);
            else
                ok = session.addDrumMachineTrack (item.kitIndex);
        }
        else if (item.kind == Kind::instrument)
        {
            ok = session.addInstrumentTrack (item.choice, item.choice.preset.isEmpty());   // a sound: play it, no window in the way
        }
        else if (item.kind == Kind::pattern)
        {
            ok = session.addMidiPattern (item.id, track, session.getPosition());
        }
        else
        {
            if (! juce::isPositiveAndBelow (track, session.getNumAudioTracks()))
            {
                problem ("Which track?", "Click a track first: the effect goes on the highlighted track.");
                return;
            }

            ok = session.addEffect (track, item.choice, item.choice.preset.isEmpty());
        }

        if (ok)
            remember (item);
        else
            problem ("Could not add " + item.name, session.getLastError());
    }

    void BrowserPanel::moveSelection (int delta)
    {
        if (items.empty())
            return;

        auto row = list.getSelectedRow();

        if (row < 0)
            row = delta > 0 ? -1 : (int) items.size();

        const int step = delta > 0 ? 1 : -1;

        for (int moved = 0, r = row; moved < std::abs (delta);)
        {
            r += step;

            if (! juce::isPositiveAndBelow (r, (int) items.size()))
                break;

            if (items[(size_t) r].kind != Kind::header)
            {
                row = r;
                ++moved;
            }
        }

        if (juce::isPositiveAndBelow (row, (int) items.size()))
        {
            list.selectRow (row);
            list.scrollToEnsureRowIsOnscreen (row);
        }
    }

    void BrowserPanel::selectionChanged()
    {
        const auto row = list.getSelectedRow();
        juce::File file;

        if (juce::isPositiveAndBelow (row, (int) items.size()) && items[(size_t) row].isSound())
            file = soundFile (items[(size_t) row], false);

        if (file != thumbnailFile)
        {
            thumbnailFile = file;

            if (file.existsAsFile())
                thumbnail->setSource (new juce::FileInputSource (file));
            else
                thumbnail->clear();
        }

        repaint (infoArea);
    }

    void BrowserPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        if (search->hasKeyboardFocus (true))
            returnFocus();   // Space, R, M... are shortcuts again, not typed into the search

        if (e.mods.isPopupMenu())
        {
            showMenu (row);
            return;
        }

        const auto item = items[(size_t) row];
        const auto width = list.getVisibleRowWidth();

        if (e.getNumberOfClicks() == 1 && keyOf (item).isNotEmpty() && e.x > width - starZone)
        {
            setFavourite (item, ! isFavourite (item));
            return;
        }

        if (e.getNumberOfClicks() == 1 && (item.foldable || (item.expandable && e.x < arrowZone)))
        {
            toggleFold (row);
            return;
        }

        if (item.isSound() && previewOnClick() && e.getNumberOfClicks() == 1)
            togglePreview (item);
        else if (item.kind == Kind::addFolder)
            chooseLibraryFolder();
        else if (item.kind == Kind::savePreset && e.getNumberOfClicks() == 1)
            savePreset (item.preset.kind);
    }

    void BrowserPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        const auto& item = items[(size_t) row];

        if (e.x > list.getVisibleRowWidth() - starZone && keyOf (item).isNotEmpty())
            return;   // the star: two clicks are two toggles, not "use it"

        if (item.expandable && e.x < arrowZone)
            return;

        activate (row);
    }

    void BrowserPanel::showMenu (int row)
    {
        const auto item = items[(size_t) row];
        list.selectRow (row);
        juce::PopupMenu menu;
        const auto target = getTargetTrack ? getTargetTrack() : -1;
        const bool hasTarget = session.hasSong() && juce::isPositiveAndBelow (target, session.getNumAudioTracks());
        const auto targetName = hasTarget ? "\"" + session.getTrackName (target) + "\"" : juce::String();
        enum { preview = 1, addHere, addNew, reveal, open, addToLibrary, removeFromLibrary, rescan, use, remove, favourite,
               loadOnTrack, togglePresets, foldAll, unfoldAll, clearRecent, padBase = 100 };

        const auto addFavouriteItem = [&]
        {
            if (keyOf (item).isNotEmpty())
            {
                menu.addSeparator();
                menu.addItem (favourite, isFavourite (item) ? juce::String ("Remove from Favourites (Ctrl+K)") : juce::String ("Add to Favourites (Ctrl+K)"));
            }
        };

        if (item.isSound())
        {
            const bool playing = audioEngine.isPreviewing() && audioEngine.getPreviewFile() == soundFile (item);
            menu.addSectionHeader (item.name);
            menu.addItem (preview, playing ? "Stop the preview (Ctrl+Enter)" : "Preview (click, or Ctrl+Enter)");
            menu.addItem (addHere, hasTarget ? "Add to " + targetName + " at the playhead (double-click)"
                                             : juce::String ("Add at the playhead on a new track (double-click)"), session.hasSong());
            menu.addItem (addNew, "Add on a new track (Shift+Enter)", session.hasSong());

            if (hasTarget && session.getDrumMachine (target) != nullptr)
            {
                juce::PopupMenu pads;

                for (int p = 0; p < DrumKits::numPads; ++p)
                    pads.addItem (padBase + p, "Pad " + juce::String (p + 1) + "  (" + DrumKits::getPadName (p) + ")");

                menu.addSubMenu ("Load on a drum pad of " + targetName, pads);
            }

            menu.addSeparator();
            menu.addItem (reveal, "Show in Explorer");
            addFavouriteItem();
        }
        else if (item.kind == Kind::folder || item.kind == Kind::up)
        {
            menu.addSectionHeader (item.file.getFullPathName());
            menu.addItem (open, "Open (double-click)");
            menu.addItem (addToLibrary, "Add to Samples & loops", ! library.getFolders().contains (item.file));
            menu.addItem (reveal, "Show in Explorer");
            addFavouriteItem();
        }
        else if (item.kind == Kind::header)
        {
            if (item.file != juce::File())
            {
                menu.addSectionHeader (item.file.getFullPathName());

                if (library.getFolders().contains (item.file))
                {
                    menu.addItem (rescan, "Look for new sounds");
                    menu.addItem (removeFromLibrary, "Remove from Samples & loops (the files stay)", item.file != SampleLibrary::getOwnFolder());
                }

                if (current == Category::song)
                    menu.addItem (rescan, "Look again");

                menu.addItem (reveal, "Show in Explorer");
            }

            if (item.foldable)
            {
                menu.addSeparator();
                menu.addItem (togglePresets, item.folded ? "Unfold this section (click)" : "Fold this section (click)");
                menu.addItem (foldAll, "Fold every section");
                menu.addItem (unfoldAll, "Unfold every section");
            }

            if (current == Category::recent && ! recent.isEmpty())
                menu.addItem (clearRecent, "Clear Recent");

            if (menu.getNumItems() == 0)
                return;
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
            addFavouriteItem();
        }
        else if (item.kind == Kind::pattern)
        {
            const auto onTrack = hasTarget ? session.trackForPattern (item.id, target) : -1;
            menu.addSectionHeader (item.name);
            menu.addItem (use, onTrack >= 0 ? "Add to " + targetName + " at the playhead (double-click)"
                                            : juce::String ("Add at the playhead on a new track (double-click)"), session.hasSong());
            menu.addItem (addNew, "Add on a new track (Shift+Enter)", session.hasSong());
            addFavouriteItem();
        }
        else if (item.kind == Kind::topic)
        {
            menu.addSectionHeader (item.name);
            menu.addItem (use, "Open in the help guide (double-click)");
            addFavouriteItem();
        }
        else if (item.kind == Kind::instrument || item.kind == Kind::effect || item.kind == Kind::kit)
        {
            const bool isPreset = item.choice.preset.isNotEmpty();
            const bool canLoad = hasTarget && session.hasPluginOfType (target, item.choice.pluginType);
            menu.addSectionHeader (item.kind == Kind::effect && isPreset ? item.choice.name + ": " + item.choice.preset : item.name);

            if (item.kind == Kind::instrument)
            {
                menu.addItem (use, isPreset ? "New track with this sound (double-click)" : "New track with it (double-click)", session.hasSong());

                if (isPreset)
                    menu.addItem (loadOnTrack, canLoad ? "Load on the synth of " + targetName
                                                       : juce::String ("Load on the highlighted track's synth (it has none)"), canLoad);
            }
            else if (item.kind == Kind::effect)
            {
                menu.addItem (use, hasTarget ? "Add to " + targetName + " (double-click)" : juce::String ("Add to the highlighted track (double-click)"), hasTarget);

                if (isPreset)
                    menu.addItem (loadOnTrack, canLoad ? "Load on the " + item.choice.name + " already on " + targetName
                                                       : "Load on a " + item.choice.name + " on the highlighted track (it has none)", canLoad);

                if (item.expandable)
                    menu.addItem (togglePresets, item.expanded ? "Hide its presets (click the arrow)" : "Show its presets (click the arrow)");
            }
            else
            {
                menu.addItem (use, "Load on the highlighted drum track (double-click)", session.hasSong());
            }

            addFavouriteItem();
        }
        else
        {
            return;
        }

        juce::Component::SafePointer<BrowserPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition(), [safe, item, row, target] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& self = *safe;

            switch (result)
            {
                case preview:           self.togglePreview (item); break;
                case addHere:           self.addSound (item, false); break;
                case addNew:            self.activateItem (item, true); break;
                case reveal:            (item.isSound() ? self.soundFile (item) : item.file).revealToUser(); break;
                case open:              self.navigateTo (item.file); break;
                case addToLibrary:      self.library.addFolder (item.file); self.rebuildItems(); break;
                case removeFromLibrary: self.library.removeFolder (item.file); self.rebuildItems(); break;
                case rescan:            self.library.rescan(); self.rebuildItems(); break;
                case favourite:         self.setFavourite (item, ! self.isFavourite (item)); break;
                case clearRecent:       self.recent.clear(); self.saveLists(); self.rebuildItems(); break;

                case togglePresets:
                    // The row may have moved: find it again by what it is
                    for (int i = 0; i < (int) self.items.size(); ++i)
                        if (self.items[(size_t) i].kind == item.kind && self.items[(size_t) i].name == item.name)
                        {
                            self.toggleFold (i);
                            break;
                        }
                    break;

                case foldAll:
                case unfoldAll:
                    for (const auto& i : self.items)
                        if (i.foldable)
                        {
                            const auto key = juce::String ((int) self.current) + "|" + i.name.upToFirstOccurrenceOf ("  (", false, false);

                            if (result == foldAll)
                                self.foldedSections.insert (key);
                            else
                                self.foldedSections.erase (key);
                        }

                    self.rebuildItems();
                    break;

                case loadOnTrack:
                    if (self.session.applyPluginPreset (target, item.choice.pluginType, item.choice.preset))
                        self.remember (item);
                    else
                        self.problem ("Could not load " + item.choice.preset, self.session.getLastError());
                    break;

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
        const juce::String star = keyOf (item).isNotEmpty() ? " The star at the right keeps it in Favourites (Ctrl+K)." : "";

        switch (item.kind)
        {
            case Kind::header:      return item.foldable ? "Click to " + juce::String (item.folded ? "unfold" : "fold") + " this section"
                                                           + (item.file != juce::File() ? " (" + item.file.getFullPathName() + ")" : juce::String())
                                         : item.file != juce::File() ? item.file.getFullPathName() + " (right-click for more)" : juce::String();
            case Kind::kit:         return "Double-click: load " + item.name + " on the highlighted drum track (or a new drum track)." + star;
            case Kind::instrument:  return (item.choice.preset.isNotEmpty() ? "Double-click: a new track playing " + item.name
                                                                            + "; right-click to load it on a synth you have"
                                                                            : "Double-click or drag onto the timeline: a new track with " + item.name) + "." + star;
            case Kind::effect:      return (item.expandable ? juce::String ("The arrow shows its presets. ") : juce::String())
                                         + "Double-click or drag onto a track: add "
                                         + (item.choice.preset.isNotEmpty() ? item.choice.name + " set to " + item.choice.preset : item.name)
                                         + " to the highlighted track." + star;
            case Kind::pattern:     return "Double-click: on the highlighted track at the playhead (a new track if it can't play it), "
                                           "in your song's key. Or drag it onto a track." + star;
            case Kind::topic:       return "Double-click: open " + item.name + " in the help guide." + star;
            case Kind::folder:      return "Double-click: open " + item.file.getFullPathName() + "." + star;
            case Kind::up:          return "Double-click: up to " + item.file.getFullPathName();
            case Kind::addFolder:   return "Add a folder of sounds you already have: it's searched and listed here, never moved or copied";
            case Kind::savePreset:  return item.detail;
            case Kind::preset:      return item.name + ": " + item.detail
                                           + (item.preset.kind == Session::PresetKind::songTemplate ? " (double-click: a new song from it)"
                                            : item.preset.kind == Session::PresetKind::trackPreset  ? " (double-click: a new track)"
                                                                                                    : " (double-click: on the highlighted track)") + "." + star;
            case Kind::sample:
            case Kind::file:
            case Kind::kitSound:    return item.name + ": click to hear it, double-click to add at the playhead, drag onto a track or a drum pad, right-click for more." + star;
        }

        return {};
    }

    juce::var BrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        juce::StringArray paths;

        for (int i = 0; i < rows.size(); ++i)
        {
            const auto row = rows[i];

            if (! juce::isPositiveAndBelow (row, (int) items.size()))
                continue;

            const auto& item = items[(size_t) row];

            // Patterns, instruments and effects drag one at a time
            if (item.kind == Kind::pattern)
                return BrowserDrag::describePattern (item.id);

            if (item.kind == Kind::instrument || item.kind == Kind::effect)
                return BrowserDrag::describePlugin (item.choice, item.kind == Kind::instrument);

            // Never an error box here: it's asked again on every mouse move until a drag starts
            if (item.isSound())
                if (const auto file = soundFile (item, false); file != juce::File())
                    paths.add (file.getFullPathName());
        }

        return BrowserDrag::describe (paths);
    }

    void BrowserPanel::timerCallback()
    {
        // The preview's "playing" mark goes when the sound ends; the waveform's playhead moves
        const auto now = audioEngine.isPreviewing() ? audioEngine.getPreviewFile() : juce::File();

        if (now != lastPreviewing)
        {
            lastPreviewing = now;
            list.repaint();
        }

        const auto progress = audioEngine.getPreviewProgress();

        if (progress != lastProgress)
        {
            lastProgress = progress;
            repaint (infoArea);
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
            if (isHint (item.name))
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

            auto caption = r.withTrimmedLeft (4).withTrimmedBottom (4);

            if (item.foldable)
            {
                const auto arrow = caption.removeFromLeft (12).withTrimmedTop (caption.getHeight() - 14).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
                Icons::draw (g, item.folded ? Icon::chevronRight : Icon::chevronDown, arrow, Theme::textDim, 1.6f);
                caption.removeFromLeft (3);
            }

            drawCaption (g, item.name, caption, juce::Justification::bottomLeft);
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

        // The star (right): filled when it's a favourite, an outline on the selected row
        if (keyOf (item).isNotEmpty())
        {
            const auto starArea = r.removeFromRight (starZone - 6).toFloat().withSizeKeepingCentre (14.0f, 14.0f);

            if (isFavourite (item))
            {
                juce::Path p;
                p.addStar (starArea.getCentre(), 5, 3.0f, 7.0f);
                g.setColour (Theme::accent);
                g.fillPath (p);
            }
            else if (selected)
            {
                Icons::draw (g, Icon::star, starArea, Theme::textDim, 1.4f);
            }
        }

        if (item.indent)
            r.removeFromLeft (arrowZone);

        if (item.expandable)
            Icons::draw (g, item.expanded ? Icon::chevronDown : Icon::chevronRight,
                         r.removeFromLeft (arrowZone - 6).toFloat().withSizeKeepingCentre (10.0f, 10.0f), Theme::textDim, 1.6f);
        else if (current == Category::effects && item.kind == Kind::effect && ! item.indent && search->getText().trim().isEmpty())
            r.removeFromLeft (arrowZone - 6);   // lines up with the effects that have the arrow
        else
            r.removeFromLeft (8);

        Icon icon = Icon::plug;

        switch (item.kind)
        {
            case Kind::kit:                         icon = Icon::drum; break;
            case Kind::instrument:                  icon = item.choice.preset.isNotEmpty() ? Icon::wave : Icon::piano; break;
            case Kind::folder: case Kind::up:       icon = Icon::open; break;
            case Kind::addFolder: case Kind::savePreset: icon = Icon::plus; break;
            case Kind::pattern:                     icon = Icon::grid; break;
            case Kind::topic:                       icon = Icon::book; break;
            case Kind::preset:
                icon = item.preset.kind == Session::PresetKind::songTemplate ? Icon::layers
                     : item.preset.kind == Session::PresetKind::trackPreset  ? Icon::bookmark : Icon::chain;
                break;
            case Kind::sample: case Kind::file:
            case Kind::kitSound:                    icon = playing ? Icon::speaker : item.isLoop ? Icon::loop : Icon::note; break;
            case Kind::effect:                      icon = item.choice.preset.isNotEmpty() ? Icon::sliders : Icon::plug; break;
            case Kind::header:                      break;
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
        if (search->getText().trim().isNotEmpty())
            return "Search";

        return categoryName ((int) current);
    }

    juce::String BrowserPanel::footerHint() const
    {
        if (search->getText().trim().isNotEmpty())
            return "Up/Down: choose, Enter: use, Esc: clear";

        switch (current)
        {
            case Category::favourites:   return "Double-click to use; the star takes it off";
            case Category::recent:       return "Double-click to use again";
            case Category::kits:         return "Double-click to load on a drum track";
            case Category::instruments:  return "Double-click or drag: a new track";
            case Category::sounds:       return "Double-click: a new track with the sound";
            case Category::effects:      return "Double-click or drag onto a track; the arrow: presets";
            case Category::patterns:     return "Double-click or drag: in your key, at the playhead";
            case Category::templates:    return "Double-click to start a new song from it";
            case Category::trackPresets: return "Double-click to add as a new track";
            case Category::chains:       return "Double-click to add to the highlighted track";
            case Category::learn:        return "Double-click to open in the help guide";
            case Category::samples: case Category::song: case Category::files: break;
        }

        return utf8 ("Click: hear it \xc2\xb7 double-click or drag: add");
    }

    void BrowserPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, true));
        g.drawText ("Browser", titleArea, juce::Justification::centredLeft, false);

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (listTitleArea.getY() - 1, 0.0f, (float) getWidth() - 1);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, true));

        // The title, with how many things are listed (not counting section titles)
        int count = 0;

        for (const auto& i : items)
            if (i.kind != Kind::header && i.kind != Kind::savePreset && i.kind != Kind::addFolder && i.kind != Kind::up)
                ++count;

        const auto title = categoryTitle();
        g.drawText (title, listTitleArea, juce::Justification::centredLeft, true);
        const auto titleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));
        g.drawText (count > 0 ? juce::String (count) : juce::String(), listTitleArea.withTrimmedLeft (titleWidth + 8).withTrimmedRight (10),
                    juce::Justification::centredLeft, false);

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth() - 1);

        // The info strip: the selected sound's waveform (and where the preview is), or what a double-click does
        if (! infoArea.isEmpty())
        {
            auto strip = infoArea.reduced (8, 4);
            g.setColour (Theme::raised.withAlpha (0.6f));
            g.fillRoundedRectangle (strip.toFloat(), (float) Theme::smallRadius);

            if (thumbnailFile != juce::File() && thumbnail->getTotalLength() > 0.0)
            {
                const auto wave = strip.reduced (6, 3);
                g.setColour (Theme::accent.withAlpha (0.7f));
                thumbnail->drawChannels (g, wave, 0.0, thumbnail->getTotalLength(), 0.9f);

                if (lastPreviewing == thumbnailFile && lastProgress >= 0.0)
                {
                    const auto x = (float) wave.getX() + (float) lastProgress * (float) wave.getWidth();
                    g.setColour (Theme::text);
                    g.drawLine (x, (float) wave.getY(), x, (float) wave.getBottom(), 1.5f);
                }

                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize - 1.0f));
                g.drawText (juce::String (thumbnail->getTotalLength(), 1) + " s", wave.reduced (2, 0), juce::Justification::bottomRight, false);
            }
            else
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawFittedText (footerHint(), strip.reduced (8, 0), juce::Justification::centredLeft, 2, 0.9f);
            }
        }
    }

    void BrowserPanel::paintOverChildren (juce::Graphics& g)
    {
        // over the search box (its background would hide it)
        Icons::draw (g, Icon::search, searchIconArea.toFloat().withSizeKeepingCentre (13.0f, 13.0f), Theme::textDim);
    }

    void BrowserPanel::resized()
    {
        auto r = getLocalBounds().withTrimmedRight (1);

        auto title = r.removeFromTop (Theme::panelTitleHeight);
        closeButton.setBounds (title.removeFromRight (30).withSizeKeepingCentre (24, 24).translated (-4, 0));
        foldButton.setBounds (title.removeFromRight (26).withSizeKeepingCentre (24, 24));
        titleArea = title.withTrimmedLeft (12);

        search->setBounds (r.removeFromTop (26).reduced (10, 0));
        searchIconArea = search->getBounds().withWidth (24).translated (2, 0);
        r.removeFromTop (8);

        // Two columns of categories; folded (by choice, or when there isn't room): just the one you're in, as a picker
        const int gridRows = (categoryRows.size() + 1) / 2;
        const int gridHeight = gridRows * (rowHeight + 1) + 6;
        const bool folded = categoriesFolded || getHeight() < gridHeight + 330;
        foldButton.setIcon (folded ? Icon::chevronDown : Icon::chevronUp);
        foldButton.setTooltip (folded ? "Show every category (two columns)" : "Fold the categories into one row, for more room below");

        if (folded)
        {
            const auto row = r.removeFromTop (rowHeight + 6).reduced (6, 0).withTrimmedBottom (6);

            for (auto* c : categoryRows)
            {
                c->showsMenuChevron = c->category == current;
                c->setVisible (c->category == current);

                if (c->category == current)
                    c->setBounds (row);

                c->repaint();
            }
        }
        else
        {
            const auto grid = r.removeFromTop (gridHeight).reduced (6, 0);
            const auto colWidth = (grid.getWidth() - 2) / 2;

            for (int i = 0; i < categoryRows.size(); ++i)
            {
                auto* c = categoryRows[i];
                c->showsMenuChevron = false;
                c->setVisible (true);
                c->setBounds (grid.getX() + (i % 2) * (colWidth + 2), grid.getY() + (i / 2) * (rowHeight + 1), colWidth, rowHeight);
                c->repaint();
            }
        }

        auto footer = r.removeFromBottom (footerHeight);
        footerArea = footer;
        footer.reduce (6, 0);
        previewButton.setBounds (footer.removeFromLeft (30).withSizeKeepingCentre (26, 26));
        loopButton.setBounds (footer.removeFromLeft (30).withSizeKeepingCentre (26, 26));
        previewLevel.setBounds (footer.reduced (6, 7));

        // The waveform / hint strip, when there's room for it
        infoArea = r.getHeight() > 200 ? r.removeFromBottom (infoHeight) : juce::Rectangle<int>();

        listTitleArea = r.removeFromTop (30).withTrimmedLeft (12);

        const bool showChips = current == Category::samples && search->getText().trim().isEmpty();
        chips->setVisible (showChips);

        if (showChips)
        {
            const auto h = chips->heightFor (r.getWidth() - 20);
            chips->setBounds (r.removeFromTop (h + 2).reduced (10, 0).withTrimmedBottom (2));
        }

        list.setBounds (r);
    }
}
