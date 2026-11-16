/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ExportView.h"
#include "TrackMenus.h"
#include "Theme.h"

namespace ez
{
    namespace
    {
        using What = Session::ExportWhat;
        using Range = Session::ExportRange;
        using Format = Session::ExportFormat;
        using State = Session::ExportStatus::State;

        constexpr int titleHeight = 0, footerHeight = 56, presetsWidth = 230, gap = 18;

        void drawHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::captionFontSize + 0.5f, true));
            g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft, false);
        }

        juce::String dbText (float db)     { return juce::String (db, 1); }
    }

    //==============================================================================
    /** A choice with a title and a line under it (a preset, Full mix / Stems, Whole song / Loop). */
    class ExportView::Choice : public juce::Button
    {
    public:
        Choice (const juce::String& title, const juce::String& sub, int radioGroup)
            : juce::Button (title), subtitle (sub)
        {
            setClickingTogglesState (true);
            setRadioGroupId (radioGroup);
            setWantsKeyboardFocus (false);
        }

        void setSubtitle (const juce::String& s)    { if (s != subtitle) { subtitle = s; repaint(); } }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            const auto alpha = isEnabled() ? 1.0f : 0.4f;

            g.setColour ((on ? Theme::selectedRow : over ? Theme::raised : Theme::panel).brighter (down ? 0.05f : 0.0f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour ((on ? Theme::accent : Theme::border).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, 5.0f, on ? 1.5f : 1.0f);

            auto area = getLocalBounds().reduced (10, 5);
            const bool twoLines = subtitle.isNotEmpty() && area.getHeight() >= 30;
            g.setColour (Theme::text.withMultipliedAlpha (alpha));
            g.setFont (Theme::font (Theme::baseFontSize, true));
            g.drawText (getButtonText(), twoLines ? area.removeFromTop (area.getHeight() / 2) : area, juce::Justification::centredLeft, true);

            if (twoLines)
            {
                g.setColour (Theme::textDim.withMultipliedAlpha (alpha));
                g.setFont (Theme::font (Theme::smallFontSize));
                g.drawText (subtitle, area, juce::Justification::centredLeft, true);
            }
        }

    private:
        juce::String subtitle;
    };

    //==============================================================================
    /** The right-hand column: every setting. Scrolls when the window is short. */
    class ExportView::Body : public juce::Component
    {
    public:
        explicit Body (ExportView& o) : owner (o)
        {
            const std::pair<const char*, const char*> whats[] = {
                { "Full mix", "one stereo file of the song" }, { "Stems", "one file per track with clips" },
                { "Bus stems", "one file per bus or FX return" }, { "MIDI", "notes only, one file per MIDI track" } };

            for (int i = 0; i < 4; ++i)
            {
                auto* c = what.add (new Choice (whats[i].first, whats[i].second, 1));
                c->onClick = [this, c, i] { if (c->getToggleState()) { owner.settings.what = (What) i; owner.changedByHand(); } };
                addAndMakeVisible (c);
            }

            const char* ranges[] = { "Whole song", "Loop", "Selection" };

            for (int i = 0; i < 3; ++i)
            {
                auto* c = range.add (new Choice (ranges[i], {}, 2));
                c->onClick = [this, c, i] { if (c->getToggleState()) { owner.settings.range = (Range) i; owner.changedByHand(); } };
                addAndMakeVisible (c);
            }

            range[0]->setTooltip ("From the start to the end of the last clip (reverb and delay tails ring out)");
            range[1]->setTooltip ("Just the loop (drag in the ruler to set it); ends exactly at the loop end");
            range[2]->setTooltip ("From the first to the last selected clip");

            const char* formats[] = { "WAV", "FLAC", "OGG", "MP3" };
            const char* formatTips[] = { "WAV: uncompressed, plays everywhere", "FLAC: lossless and about half the size",
                                         "Ogg Vorbis: small, good quality (256 kbps)", "MP3: small, plays everywhere (320 kbps)" };

            for (int i = 0; i < 4; ++i)
            {
                auto* b = format.add (new juce::TextButton (formats[i]));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (3);
                b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 3 ? juce::Button::ConnectedOnRight : 0));
                b->setColour (juce::TextButton::buttonOnColourId, Theme::selectedRow);
                b->setTooltip (formatTips[i]);
                b->setWantsKeyboardFocus (false);
                b->onClick = [this, b, i] { if (b->getToggleState()) { owner.settings.format = (Format) i; owner.changedByHand(); } };
                addAndMakeVisible (b);
            }

            sampleRate.addItem ("44.1 kHz", 44100);
            sampleRate.addItem ("48 kHz", 48000);
            sampleRate.addItem ("88.2 kHz", 88200);
            sampleRate.addItem ("96 kHz", 96000);
            sampleRate.setTooltip ("Sample rate: 44.1 kHz for CDs, 48 kHz for video and most streaming");
            sampleRate.onChange = [this] { owner.settings.sampleRate = (double) sampleRate.getSelectedId(); owner.changedByHand(); };

            bitDepth.addItem ("16-bit", 16);
            bitDepth.addItem ("24-bit", 24);
            bitDepth.addItem ("32-bit float", 32);
            bitDepth.setTooltip ("24-bit for a master or for stems; 16-bit for CDs (with dither)");
            bitDepth.onChange = [this] { owner.settings.bitDepth = bitDepth.getSelectedId(); owner.changedByHand(); };

            dither.setTooltip ("Dither: a little noise that hides the steps of 16-bit audio in quiet parts");
            dither.onClick = [this] { owner.settings.dither = dither.getToggleState(); owner.changedByHand(); };

            matchLoudness.setTooltip ("Turns the whole mix up or down to the loudness target. The true peak stays under -1 dB, "
                                      "so a very quiet or very dynamic mix can land under the target");
            matchLoudness.onClick = [this] { owner.settings.matchLoudness = matchLoudness.getToggleState(); owner.changedByHand(); };

            for (int i = 0; i < Session::getLoudnessTargets().size(); ++i)
            {
                const auto& t = Session::getLoudnessTargets().getReference (i);
                target.addItem (t.name + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + dbText (t.lufs) + juce::String (juce::CharPointer_UTF8 (" LUFS \xc2\xb7 ")) + dbText (t.ceilingDb) + " dBTP", i + 1);
            }

            target.setTooltip ("How loud the export should be (integrated loudness, like the streaming services measure it)");
            target.onChange = [this]
            {
                if (const auto i = target.getSelectedId() - 1; juce::isPositiveAndBelow (i, Session::getLoudnessTargets().size()))
                {
                    owner.settings.targetLufs = Session::getLoudnessTargets().getReference (i).lufs;
                    owner.settings.ceilingDb = Session::getLoudnessTargets().getReference (i).ceilingDb;
                }

                owner.changedByHand();
            };

            masterChain.onClick = [this] { owner.settings.useMasterChain = masterChain.getToggleState(); owner.changedByHand(); };

            name.setTooltip ("The file name (stems add \" - <track name>\")");
            name.setColour (juce::TextEditor::backgroundColourId, Theme::display);
            name.onTextChange = [this]
            {
                owner.settings.fileName = name.getText();
                owner.nameIsAutomatic = false;
                owner.changedByHand (false);
            };
            name.onReturnKey = [this] { owner.startExport(); };

            folder.setColour (juce::Label::backgroundColourId, Theme::display);
            folder.setColour (juce::Label::outlineColourId, Theme::border);
            folder.setMinimumHorizontalScale (1.0f);
            browse.setTooltip ("Pick the folder to save into");
            browse.onClick = [this] { owner.chooseFolder(); };
            openFolder.setTooltip ("Open the folder in Explorer");
            openFolder.onClick = [this]
            {
                if (owner.settings.folder.isDirectory())
                    owner.settings.folder.startAsProcess();
            };

            for (auto* c : std::initializer_list<juce::Component*> { &sampleRate, &bitDepth, &dither, &matchLoudness, &target,
                                                                     &masterChain, &name, &extension, &folder, &browse, &openFolder })
                addAndMakeVisible (c);

            extension.setColour (juce::Label::textColourId, Theme::textDim);

            // Enter exports wherever the focus was: only the name field takes the keyboard
            for (auto* c : std::initializer_list<juce::Component*> { &sampleRate, &bitDepth, &target, &dither, &matchLoudness, &masterChain, &browse, &openFolder })
                c->setWantsKeyboardFocus (false);
        }

        void refresh()
        {
            auto& s = owner.settings;
            const bool idle = ! owner.isRunning();
            const bool audio = s.what != What::midi;
            const bool fullMix = s.what == What::fullMix;

            for (int i = 0; i < what.size(); ++i)
            {
                what[i]->setToggleState ((int) s.what == i, juce::dontSendNotification);
                what[i]->setEnabled (idle);
            }

            for (int i = 0; i < range.size(); ++i)
            {
                const auto description = owner.session.describeExportRange ((Range) i);
                range[i]->setSubtitle (description.isNotEmpty() ? description : i == 1 ? juce::String ("no loop set") : i == 2 ? juce::String ("nothing selected") : juce::String ("the song is empty"));
                range[i]->setToggleState ((int) s.range == i, juce::dontSendNotification);
                range[i]->setEnabled (idle && description.isNotEmpty());
            }

            for (int i = 0; i < format.size(); ++i)
            {
                const auto available = owner.session.isExportFormatAvailable ((Format) i);
                format[i]->setToggleState ((int) s.format == i, juce::dontSendNotification);
                format[i]->setEnabled (idle && audio && available);

                if (i == (int) Format::mp3 && ! available)
                    format[i]->setTooltip ("MP3 needs lame.exe (free) in the same folder as EzDAWsIt.exe; OGG is just as small");
            }

            const bool lossy = s.format == Format::ogg || s.format == Format::mp3;
            const auto rates = owner.session.getExportSampleRates (s.format);

            for (auto r : { 44100, 48000, 88200, 96000 })
                sampleRate.setItemEnabled (r, ! audio || rates.contains (r));

            sampleRate.setSelectedId ((int) s.sampleRate, juce::dontSendNotification);
            sampleRate.setEnabled (idle && audio);
            bitDepth.setSelectedId (lossy ? 16 : (s.format == Format::flac ? juce::jmin (24, s.bitDepth) : s.bitDepth), juce::dontSendNotification);
            bitDepth.setItemEnabled (32, s.format != Format::flac);
            bitDepth.setEnabled (idle && audio && ! lossy);
            bitDepth.setTooltip (lossy ? "MP3 and Ogg choose their own resolution" : s.format == Format::flac ? "FLAC: 16 or 24-bit"
                                       : "24-bit for a master or for stems; 16-bit for CDs (with dither)");
            dither.setToggleState (s.dither, juce::dontSendNotification);
            dither.setEnabled (idle && audio && ! lossy && bitDepth.getSelectedId() == 16);

            matchLoudness.setToggleState (s.matchLoudness && fullMix, juce::dontSendNotification);
            matchLoudness.setEnabled (idle && fullMix);
            matchLoudness.setTooltip (fullMix ? "Turns the whole mix up or down to the loudness target. The true peak stays under -1 dB, "
                                                "so a very quiet or very dynamic mix can land under the target"
                                              : "Loudness matching is for the full mix: stems keep their levels so they add up to the mix");
            int targetId = 0;

            for (int i = 0; i < Session::getLoudnessTargets().size(); ++i)
                if (const auto& t = Session::getLoudnessTargets().getReference (i);
                    std::abs (t.lufs - s.targetLufs) < 0.05f && std::abs (t.ceilingDb - s.ceilingDb) < 0.05f
                     && (targetId == 0 || i == owner.session.getMasterTarget()))   // Spotify and YouTube match alike: the song's choice wins
                    targetId = i + 1;

            target.setSelectedId (targetId, juce::dontSendNotification);
            target.setEnabled (idle && fullMix && s.matchLoudness);

            const bool chainOn = owner.session.isMasterChainOn();
            masterChain.setToggleState (fullMix && chainOn && s.useMasterChain, juce::dontSendNotification);
            masterChain.setEnabled (idle && fullMix && chainOn);
            masterChain.setTooltip (! fullMix ? "Stems and MIDI skip the master"
                                  : chainOn ? "The mastering chain (Master view) is in the export; untick to export without it"
                                            : "The mastering chain is off (switch it on in the Master view)");

            if (! name.hasKeyboardFocus (true) && name.getText() != s.fileName)
                name.setText (s.fileName, juce::dontSendNotification);

            name.setEnabled (idle);
            extension.setText ((s.what == What::fullMix ? juce::String() : juce::String (" - <track>")) + Session::getExportExtension (s),
                               juce::dontSendNotification);
            folder.setText (s.folder.getFullPathName(), juce::dontSendNotification);
            folder.setTooltip (s.folder.getFullPathName());
            browse.setEnabled (idle);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& [text, area] : headings)
                drawHeading (g, text, area);

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize));

            for (auto& [text, area] : labels)
                g.drawText (text, area, juce::Justification::centredLeft, false);

            g.setColour (Theme::text);
            g.drawFittedText (loudnessNote(), noteArea, juce::Justification::topLeft, 2);
        }

        void paintOverChildren (juce::Graphics& g) override     { Planned::paintChips (g, *this); }

        int layout (int width)
        {
            headings.clear();
            labels.clear();
            auto r = juce::Rectangle<int> (0, 0, width, 10000).reduced (gap, 14);
            const auto rowH = 26;

            const auto section = [&] (const juce::String& title)
            {
                headings.push_back ({ title, r.removeFromTop (16) });
                r.removeFromTop (6);
            };

            const auto label = [&] (juce::Rectangle<int>& row, const juce::String& text, int w)
            {
                labels.push_back ({ text, row.removeFromLeft (w) });
            };

            section ("What to export");
            {
                auto grid = r.removeFromTop (2 * 46 + 8);
                const auto cellW = (grid.getWidth() - 8) / 2;

                for (int i = 0; i < 4; ++i)
                    what[i]->setBounds (grid.getX() + (i % 2) * (cellW + 8), grid.getY() + (i / 2) * 54, cellW, 46);
            }

            r.removeFromTop (gap);
            section ("Range");
            {
                auto row = r.removeFromTop (46);
                const auto cellW = (row.getWidth() - 16) / 3;

                for (auto* c : range)
                {
                    c->setBounds (row.removeFromLeft (cellW));
                    row.removeFromLeft (8);
                }
            }

            r.removeFromTop (gap);
            section ("Format");
            {
                auto row = r.removeFromTop (rowH);

                for (auto* b : format)
                    b->setBounds (row.removeFromLeft (58));

                r.removeFromTop (8);
                row = r.removeFromTop (rowH);
                label (row, "Sample rate", 72);
                sampleRate.setBounds (row.removeFromLeft (100));
                row.removeFromLeft (14);
                label (row, "Bit depth", 60);
                bitDepth.setBounds (row.removeFromLeft (118));
                row.removeFromLeft (14);

                if (row.getWidth() < 130)   // narrow window: dither on a line of its own
                {
                    r.removeFromTop (8);
                    row = r.removeFromTop (rowH).withTrimmedLeft (72);
                }

                dither.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), 150)));
            }

            r.removeFromTop (gap);
            section ("Loudness");
            {
                auto row = r.removeFromTop (rowH);
                matchLoudness.setBounds (row.removeFromLeft (190));
                target.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), 290)));
                r.removeFromTop (6);
                masterChain.setBounds (r.removeFromTop (rowH).withWidth (juce::jmin (r.getWidth(), 260)));
                r.removeFromTop (4);
                noteArea = r.removeFromTop (34);
            }

            r.removeFromTop (gap - 6);
            section ("Save to");
            {
                auto row = r.removeFromTop (rowH);
                label (row, "File name", 64);
                extension.setBounds (row.removeFromRight (juce::jmin (150, row.getWidth() / 3)));
                name.setBounds (row);
                r.removeFromTop (8);
                row = r.removeFromTop (rowH);
                label (row, "Folder", 64);
                openFolder.setBounds (row.removeFromRight (openFolder.getIdealWidth (rowH)));
                row.removeFromRight (6);
                browse.setBounds (row.removeFromRight (browse.getIdealWidth (rowH)));
                row.removeFromRight (6);
                folder.setBounds (row);
            }

            return r.getY() + 14;
        }

    private:
        ExportView& owner;
        juce::OwnedArray<Choice> what, range;
        juce::OwnedArray<juce::TextButton> format;
        juce::ComboBox sampleRate, bitDepth, target;
        juce::ToggleButton dither { "Dither (16-bit)" }, matchLoudness { "Match loudness target" },
                           masterChain { "Use the mastering chain" };
        juce::TextEditor name;
        juce::Label extension, folder;
        IconButton browse { "Browse", Icon::open, "Browse" }, openFolder { "Open folder", Icon::file, "Open folder" };
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings, labels;
        juce::Rectangle<int> noteArea;

        juce::String loudnessNote() const
        {
            const auto& s = owner.settings;

            if (s.what == What::midi)
                return "MIDI files carry the notes only: no sound, effects or loudness.";

            if (s.what != What::fullMix)
                return "Stems skip the master fader and effects and keep their own levels, so together they add up to the mix.";

            const auto chain = owner.session.isMasterChainOn() && s.useMasterChain ? juce::String ("the mastering chain and the master fader")
                                                                                   : juce::String ("the master fader");
            return s.matchLoudness ? "The mix is turned up or down to " + dbText (s.targetLufs) + " LUFS; the true peak stays under "
                                         + dbText (s.ceilingDb) + " dB. With " + chain + "."
                                   : "Exported at the level you hear, with " + chain + ".";
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Body)
    };

    //==============================================================================
    ExportView::ExportView (Session& s) : session (s)
    {
        settings = session.getDefaultExportSettings();
        settings.what = What::fullMix;

        rebuildPresets();

        savePresetButton.setTooltip ("Save these settings as a preset of your own (they're kept for every song)");
        savePresetButton.onClick = [this] { saveAsPreset(); };
        addAndMakeVisible (savePresetButton);

        body = std::make_unique<Body> (*this);
        bodyViewport.setViewedComponent (body.get(), false);
        bodyViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (bodyViewport);

        progressBar.setColour (juce::ProgressBar::foregroundColourId, Theme::accent);
        progressBar.setColour (juce::ProgressBar::backgroundColourId, Theme::display);
        addChildComponent (progressBar);

        showButton.setTooltip ("Show the exported file in Explorer");
        showButton.onClick = [this]
        {
            const auto& files = session.getExportStatus().files;

            if (! files.isEmpty())
                files.getFirst().revealToUser();
        };
        addChildComponent (showButton);

        cancelButton.setTooltip ("Close (Esc)");
        cancelButton.onClick = [this] { cancelOrClose(); };
        exportButton.setTooltip ("Export (Enter)");
        exportButton.onClick = [this] { startExport(); };

        for (auto* b : { &cancelButton, &exportButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        session.clearExportStatus();   // a finished export from last time
        applyPreset (0);

        // The song's own loudness target and dither (Master view) win over the first preset's
        const auto songDefaults = session.getDefaultExportSettings();

        if (std::abs (songDefaults.targetLufs - settings.targetLufs) > 0.05f || std::abs (songDefaults.ceilingDb - settings.ceilingDb) > 0.05f)
        {
            settings.targetLufs = songDefaults.targetLufs;
            settings.ceilingDb = songDefaults.ceilingDb;
            presetIndex = -1;
        }

        settings.dither = songDefaults.dither;
        refreshControls();
        setWantsKeyboardFocus (true);
        setSize (preferredWidth, preferredHeight);
        startTimerHz (20);
    }

    ExportView::~ExportView()
    {
        // Closed while running: it stops (the Session finishes the bookkeeping on its own timer)
        if (session.isExporting())
            session.cancelExport();
    }

    bool ExportView::isRunning()
    {
        return session.isExporting();
    }

    //==============================================================================
    void ExportView::rebuildPresets()
    {
        presetRows.clear();
        allPresets = Session::getExportPresets();
        numBuiltIn = allPresets.size();
        allPresets.addArray (Session::getUserExportPresets());

        for (int i = 0; i < allPresets.size(); ++i)
        {
            const auto& preset = allPresets.getReference (i);
            auto* row = presetRows.add (new Choice (preset.name, preset.description, 10));
            row->setTooltip (preset.name + ": " + preset.description
                             + (i < 9 ? " (" + juce::String (i + 1) + ")" : juce::String())
                             + (i >= numBuiltIn ? "  Right-click to delete." : ""));
            row->onClick = [this, row, i]
            {
                if (rightClicked)
                {
                    refreshControls();   // the right-clicked row goes back to how it was
                    return;
                }

                if (row->getToggleState())
                    applyPreset (i);   // (JUCE also "clicks" the one switched off)
            };
            row->addMouseListener (this, false);
            addAndMakeVisible (row);
        }

        if (body != nullptr)   // (the constructor builds the list before the rest)
            resized();
    }

    void ExportView::saveAsPreset()
    {
        if (isRunning())
            return;

        juce::Component::SafePointer<ExportView> safe (this);
        TrackMenus::askForName ("Save export preset", "A name for these export settings:", Session::describeExportSettings (settings),
                                [safe] (const juce::String& name)
                                {
                                    if (safe == nullptr)
                                        return;

                                    if (! safe->session.saveExportPreset (name, safe->settings))
                                    {
                                        safe->note = safe->session.getLastError();
                                    }
                                    else
                                    {
                                        safe->rebuildPresets();

                                        for (int i = safe->numBuiltIn; i < safe->allPresets.size(); ++i)
                                            if (safe->allPresets.getReference (i).name == juce::File::createLegalFileName (name.trim()))
                                                safe->presetIndex = i;

                                        safe->note = "Saved: it's in the list for every song";
                                    }

                                    safe->refreshControls();
                                    safe->repaint();
                                });
    }

    void ExportView::mouseDown (const juce::MouseEvent& e)
    {
        rightClicked = e.mods.isPopupMenu();

        if (! e.mods.isPopupMenu() || isRunning())
            return;

        for (int i = numBuiltIn; i < presetRows.size(); ++i)
        {
            if (e.eventComponent != presetRows[i])
                continue;

            juce::PopupMenu menu;
            menu.addItem (1, "Delete \"" + allPresets.getReference (i).name + "\"");
            juce::Component::SafePointer<ExportView> safe (this);
            const auto name = allPresets.getReference (i).name;

            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, name] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                if (! safe->session.deleteExportPreset (name))
                    safe->note = safe->session.getLastError();

                safe->presetIndex = -1;
                safe->rebuildPresets();
                safe->refreshControls();
                safe->repaint();
            });
        }
    }

    void ExportView::applyPreset (int index)
    {
        const auto& presets = allPresets;

        if (! juce::isPositiveAndBelow (index, presets.size()) || isRunning())
            return;

        const auto folder = settings.folder;
        const auto name = settings.fileName;
        settings = presets.getReference (index).settings;
        settings.folder = folder;
        settings.fileName = name;
        const bool swapMp3 = settings.format == Format::mp3 && ! session.isExportFormatAvailable (Format::mp3);

        if (swapMp3)
            settings.format = Format::ogg;

        changedByHand (false);   // clears the note, so the swap is told after it
        presetIndex = index;

        if (swapMp3)
            note = "MP3 isn't set up (it needs lame.exe next to EzDAWsIt.exe): using Ogg, just as small";

        refreshControls();
    }

    void ExportView::changedByHand (bool deselectPreset)
    {
        if (deselectPreset)
            presetIndex = -1;

        if (! isRunning())
        {
            note = {};
            session.clearExportStatus();   // the footer goes back to the estimate
        }

        // A sample rate the format can't write (MP3 at 96 kHz) comes down to one it can
        if (const auto rates = session.getExportSampleRates (settings.format); ! rates.isEmpty() && ! rates.contains ((int) settings.sampleRate))
            settings.sampleRate = rates.contains (48000) ? 48000.0 : (double) rates.getLast();

        // A range that can't be exported now (no loop, nothing selected) falls back to the whole song
        if (session.describeExportRange (settings.range).isEmpty())
            settings.range = Range::wholeSong;

        if (nameIsAutomatic)
        {
            const auto song = session.getDefaultExportSettings().fileName.upToLastOccurrenceOf (" - Master", false, false);
            settings.fileName = settings.what == What::fullMix ? song + " - Master" : song;
        }

        refreshControls();
    }

    void ExportView::refreshControls()
    {
        for (int i = 0; i < presetRows.size(); ++i)
        {
            presetRows[i]->setToggleState (i == presetIndex, juce::dontSendNotification);
            presetRows[i]->setEnabled (! isRunning());
        }

        body->refresh();

        const auto status = session.getExportStatus();
        const bool running = isRunning();
        const bool finished = status.state == State::done || status.state == State::failed || status.state == State::cancelled;

        progressBar.setVisible (running);
        showButton.setVisible (! running && status.state == State::done && ! status.files.isEmpty());
        exportButton.setEnabled (! running);
        exportButton.setButtonText (finished ? "Export again" : "Export");
        cancelButton.setButtonText (running ? "Cancel" : "Close");
        cancelButton.setTooltip (running ? "Stop the export (Esc)" : "Close (Esc)");
        resized();
        repaint();
    }

    juce::String ExportView::footerMessage()
    {
        const auto& status = session.getExportStatus();

        switch (status.state)
        {
            case State::running:
                return "Exporting " + status.current + (status.filesTotal > 1 ? " (" + juce::String (status.filesDone + 1) + " of " + juce::String (status.filesTotal) + ")" : juce::String())
                       + ". Esc stops it.";

            case State::measuring:
                return "Measuring the loudness...";

            case State::done:
            {
                auto text = juce::String (status.files.size() == 1 ? "Exported " + status.files.getFirst().getFileName()
                                                                    : "Exported " + juce::String (status.files.size()) + " files");

                if (status.loudnessValid)
                {
                    text << ": " << dbText (status.integratedLufs) << " LUFS, true peak " << dbText (status.truePeakDb) << " dB";

                    if (settings.matchLoudness && status.integratedLufs < settings.targetLufs - 0.7f)
                        text << " (held under the target to keep the peak below " << dbText (settings.ceilingDb) << " dB)";
                }

                return status.message.isNotEmpty() ? text + ". " + status.message : text;
            }

            case State::failed:
            case State::cancelled:
                return status.message;

            case State::idle:
                break;
        }

        if (note.isNotEmpty())
            return note;

        return session.estimateExport (settings);
    }

    //==============================================================================
    void ExportView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel);

        // Presets column (the window's title bar says "Export song")
        drawHeading (g, "Export presets", { 12, titleHeight + 12, presetsWidth - 24, 16 });
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (presetsWidth, (float) titleHeight, (float) (getHeight() - footerHeight));

        // Footer
        const auto footer = getLocalBounds().removeFromBottom (footerHeight);
        g.setColour (Theme::bar);
        g.fillRect (footer);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (footer.getY(), 0.0f, (float) getWidth());

        const auto& status = session.getExportStatus();
        g.setColour (status.state == State::failed ? Theme::danger : Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize));
        g.drawFittedText (footerMessage(), footerText, juce::Justification::centredLeft, 2);
    }

    void ExportView::paintOverChildren (juce::Graphics&)
    {
    }

    void ExportView::resized()
    {
        auto r = getLocalBounds();
        r.removeFromTop (titleHeight);
        auto footer = r.removeFromBottom (footerHeight).reduced (18, 0);

        // Presets
        auto presets = r.removeFromLeft (presetsWidth).reduced (12);
        presets.removeFromTop (24);
        savePresetButton.setBounds (presets.removeFromBottom (28).withWidth (savePresetButton.getIdealWidth (28)));
        presets.removeFromBottom (8);

        const auto rowHeight = juce::jlimit (30, 50, presets.getHeight() / juce::jmax (1, presetRows.size()) - 4);   // one line each when short

        for (auto* row : presetRows)
        {
            row->setBounds (presets.removeFromTop (rowHeight));
            presets.removeFromTop (4);
        }

        // Settings
        bodyViewport.setBounds (r);
        auto width = r.getWidth();
        auto height = body->layout (width);

        if (height > r.getHeight())   // it will scroll: leave room for the bar
        {
            width -= bodyViewport.getScrollBarThickness();
            height = body->layout (width);
        }

        body->setSize (width, height);

        // Footer: text (or progress) on the left, buttons on the right
        const auto buttonH = 30;
        exportButton.setBounds (footer.removeFromRight (exportButton.getIdealWidth (buttonH)).withSizeKeepingCentre (exportButton.getIdealWidth (buttonH), buttonH));
        footer.removeFromRight (8);
        cancelButton.setBounds (footer.removeFromRight (juce::jmax (80, cancelButton.getIdealWidth (buttonH))).withSizeKeepingCentre (juce::jmax (80, cancelButton.getIdealWidth (buttonH)), buttonH));
        footer.removeFromRight (8);

        if (showButton.isVisible())
        {
            showButton.setBounds (footer.removeFromRight (showButton.getIdealWidth (buttonH)).withSizeKeepingCentre (showButton.getIdealWidth (buttonH), buttonH));
            footer.removeFromRight (12);
        }

        if (progressBar.isVisible())
        {
            progressBar.setBounds (footer.removeFromLeft (juce::jmin (220, footer.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (220, footer.getWidth() / 3), 12));
            footer.removeFromLeft (12);
        }

        footerText = footer;
    }

    bool ExportView::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            cancelOrClose();
            return true;
        }

        if (key == juce::KeyPress::returnKey)
        {
            startExport();
            return true;
        }

        if (const auto c = key.getTextCharacter(); c >= '1' && c <= '9' && key.getModifiers().isAnyModifierKeyDown() == false)
        {
            applyPreset (c - '1');
            return true;
        }

        return false;
    }

    //==============================================================================
    void ExportView::startExport()
    {
        if (isRunning())
            return;

        settings.fileName = settings.fileName.trim();

        if (settings.fileName.isEmpty())
        {
            note = "Type a file name first";
            refreshControls();
            return;
        }

        juce::StringArray existing;

        for (auto& f : session.getExportFiles (settings))
            if (f.exists())
                existing.add (f.getFileName());

        if (existing.isEmpty())
        {
            startExportNow();
            return;
        }

        const auto question = existing.size() == 1 ? existing[0] + " is already there. Replace it?"
                                                   : juce::String (existing.size()) + " of the files are already there (" + existing[0] + ", ...). Replace them?";
        juce::Component::SafePointer<ExportView> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                               .withTitle ("Replace files?").withMessage (question)
                                                               .withButton ("Replace", 1).withButton ("Cancel", 0)
                                                               .withAssociatedComponent (this),
                                      [safe] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->startExportNow();
                                      });
    }

    void ExportView::startExportNow()
    {
        session.clearExportStatus();
        note = {};

        if (! session.startExport (settings))
            note = session.getLastError();

        refreshControls();
    }

    void ExportView::cancelOrClose()
    {
        if (isRunning())
        {
            session.cancelExport();
            refreshControls();
            return;
        }

        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    }

    void ExportView::chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export to which folder?", settings.folder.isDirectory() ? settings.folder : juce::File());
        juce::Component::SafePointer<ExportView> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;

                                  safe->settings.folder = fc.getResult();
                                  safe->changedByHand (false);
                              });
    }

    void ExportView::timerCallback()
    {
        session.updateExport();
        const auto& status = session.getExportStatus();
        progress = status.state == State::running ? (double) status.progress : status.state == State::measuring ? -1.0 : 0.0;

        if (status.state != lastState)
        {
            lastState = status.state;
            refreshControls();
        }
        else if (status.state == State::running || status.state == State::measuring)
        {
            repaint (footerText);
        }
    }
}
