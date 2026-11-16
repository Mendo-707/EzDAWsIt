/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "AppBar.h"
#include "../UiMode.h"
#include "../../Core/AppPaths.h"
#include "../CommandIDs.h"
#include "../../Engine/AudioEngine.h"
#include "../InterfaceScale.h"

namespace ez
{
    namespace
    {
        constexpr int logoSize = 22;
        constexpr int outputBoxWidth = 210, outputBoxMinWidth = 150;
    }

    AppBar::AppBar (AudioEngine& e, Session& s, juce::ApplicationCommandManager& commands)
        : audioEngine (e), session (s), commandManager (commands)
    {
        newButton.setCommandToTrigger (&commands, CommandIDs::newSong, true);
        openButton.setCommandToTrigger (&commands, CommandIDs::openSong, true);
        saveButton.setCommandToTrigger (&commands, CommandIDs::saveSong, true);
        setupButton.setCommandToTrigger (&commands, CommandIDs::audioSetup, true);
        exportButton.setCommandToTrigger (&commands, CommandIDs::exportSong, true);
        tunerButton.setCommandToTrigger (&commands, CommandIDs::showTuner, true);

        for (auto* b : { &newButton, &openButton, &saveButton, &setupButton, &sizeButton })
            addAndMakeVisible (b);

        sizeButton.onClick = [this] { showSizeMenu(); };

        // View switcher: one segmented control (Tab: next view, Shift+Tab: back)
        arrangeButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        mixButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        masterButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        arrangeButton.setCommandToTrigger (&commands, CommandIDs::showArrangeView, true);
        mixButton.setCommandToTrigger (&commands, CommandIDs::showMixView, true);
        arrangeButton.setClickingTogglesState (false);
        mixButton.setClickingTogglesState (false);
        masterButton.setCommandToTrigger (&commands, CommandIDs::showMasterView, true);
        masterButton.setClickingTogglesState (false);

        easyButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        proButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        easyButton.setClickingTogglesState (false);
        proButton.setClickingTogglesState (false);
        easyButton.onClick = [this] { UiMode::setPro (false); refreshViewMode(); };
        proButton.onClick = [this] { UiMode::setPro (true); refreshViewMode(); };
        refreshViewMode();
        easyButton.setTooltip ("Easy view: what most songs need (Ctrl+Shift+P switches)");
        proButton.setTooltip ("Pro view: adds routing, inserts, punch and MIDI sync to the screen (Ctrl+Shift+P switches)");
        helpButton.setTooltip ("Help: the tour, keyboard shortcuts (F1), the log folder, About");
        helpButton.onClick = [this, &commands]
        {
            juce::PopupMenu menu;
            menu.addCommandItem (&commands, CommandIDs::startTour);
            menu.addCommandItem (&commands, CommandIDs::showShortcuts);
            menu.addSeparator();
            menu.addCommandItem (&commands, CommandIDs::openLogFolder);
            menu.addCommandItem (&commands, CommandIDs::showAbout);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (helpButton));
        };

        for (auto* b : { &arrangeButton, &mixButton, &masterButton, &exportButton, &tunerButton, &easyButton, &proButton, &helpButton })
            addAndMakeVisible (b);

        outputBox.setTooltip ("Where you hear EzDAWsIt: pick headphones or speakers");
        outputBox.setWantsKeyboardFocus (false);
        outputBox.onChange = [this]
        {
            const auto index = outputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, outputIDs.size()) && outputIDs[index] != audioEngine.getMainOutputID()
                && ! session.setMainOutput (outputIDs[index]))
            {
                lastOutputSignature = {};   // show the real output again
                refreshOutputs();
            }
        };
        addAndMakeVisible (outputBox);

        session.addListener (this);
        refreshOutputs();
        startTimer (1000);
    }

    AppBar::~AppBar()
    {
        session.removeListener (this);
    }

    void AppBar::refreshOutputs()
    {
        // Switching devices mid-take would break the recording
        outputBox.setEnabled (! session.isRecording());
        outputBox.setTooltip (session.isRecording() ? "Stop recording to change the output"
                                                    : "Where you hear EzDAWsIt: pick headphones or speakers");

        const auto outputs = audioEngine.getOutputs();
        const auto mainID = audioEngine.getMainOutputID();
        juce::String signature (mainID);

        for (const auto& o : outputs)
            signature << "|" << o.id << "=" << o.name;

        if (signature == lastOutputSignature || outputBox.isPopupActive())
            return;

        lastOutputSignature = signature;
        outputBox.clear (juce::dontSendNotification);
        outputIDs.clear();

        for (int i = 0; i < outputs.size(); ++i)
        {
            outputBox.addItem (outputs.getReference (i).name, i + 1);
            outputIDs.add (outputs.getReference (i).id);
        }

        outputBox.setSelectedItemIndex (outputIDs.indexOf (mainID), juce::dontSendNotification);
        outputBox.setTextWhenNothingSelected ("No output");
    }

    //==============================================================================
    void AppBar::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::bar);
        g.setColour (Theme::hairline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // Logo: a rounded square with a small waveform, then the name
        if (! logoArea.isEmpty())
        {
            const auto mark = logoArea.withWidth (logoSize).withSizeKeepingCentre (logoSize, logoSize).toFloat();
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (mark, 5.0f);

            juce::Path wave;
            const float xs[] = { 0.25f, 0.4f, 0.55f, 0.7f }, hs[] = { 0.2f, 0.55f, 0.35f, 0.15f };

            for (int i = 0; i < 4; ++i)
            {
                const auto x = mark.getX() + mark.getWidth() * xs[i];
                wave.startNewSubPath (x, mark.getCentreY() - mark.getHeight() * hs[i] * 0.5f);
                wave.lineTo (x, mark.getCentreY() + mark.getHeight() * hs[i] * 0.5f);
            }

            g.setColour (Theme::ink);
            g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::titleFontSize - 0.5f, true));
            g.drawText ("EzDAWsIt", logoArea.withTrimmedLeft (logoSize + 8), juce::Justification::centredLeft, false);
        }

        // Song name
        if (! songNameArea.isEmpty())
        {
            auto nameArea = songNameArea;

            // Saved or not, in small letters after the name
            if (session.hasSong() && nameArea.getWidth() > 120)
            {
                const auto nameFont = Theme::font (Theme::titleFontSize - 0.5f, true);
                const auto nameWidth = juce::jmin ((int) juce::GlyphArrangement::getStringWidth (nameFont, session.getSongName()) + 10,
                                                   nameArea.getWidth() - 60);
                auto stateArea = nameArea.withTrimmedLeft (nameWidth).withWidth (60);
                nameArea = nameArea.withWidth (nameWidth);
                g.setFont (Theme::font (Theme::captionFontSize, true));
                g.setColour (lastUnsaved ? Theme::warning : Theme::textDim);
                g.drawText (lastUnsaved ? "EDITED" : "SAVED", stateArea, juce::Justification::centredLeft, false);
            }

            g.setFont (Theme::font (Theme::titleFontSize - 0.5f, session.hasSong()));
            g.setColour (session.hasSong() ? Theme::text : Theme::textDim);
            g.drawText (session.hasSong() ? session.getSongName() : juce::String ("No song open"),
                        nameArea, juce::Justification::centredLeft, true);
        }

        if (! outputIconArea.isEmpty())
            Icons::draw (g, Icon::headphones, outputIconArea.toFloat().withSizeKeepingCentre (15.0f, 15.0f), Theme::textDim);

        // Thin separators between the groups
        g.setColour (Theme::border);

        for (auto* b : { &newButton, &sizeButton })
            if (b->isVisible())
                g.fillRect (b->getX() - 8, getHeight() / 2 - 10, 1, 20);
    }

    void AppBar::showSizeMenu()
    {
        auto& engine = audioEngine.getEngine();
        const auto setting = InterfaceScale::getSetting (engine);

        juce::PopupMenu menu;
        menu.addSectionHeader ("Interface size: now " + InterfaceScale::describe (InterfaceScale::current()));
        menu.addCommandItem (&commandManager, CommandIDs::interfaceBigger);
        menu.addCommandItem (&commandManager, CommandIDs::interfaceSmaller);
        menu.addSeparator();

        juce::PopupMenu::Item automatic (InterfaceScale::describe (0.0f));
        automatic.setTicked (setting <= 0.0f);
        automatic.setAction ([&engine] { InterfaceScale::apply (engine, 0.0f); });
        automatic.shortcutKeyDescription = "Ctrl+0";
        menu.addItem (automatic);

        for (auto step : InterfaceScale::steps())
            menu.addItem (InterfaceScale::describe (step) + (step > InterfaceScale::largest() + 0.001f ? " (too big for this screen)" : ""),
                          step <= InterfaceScale::largest() + 0.001f, std::abs (setting - step) < 0.001f,
                          [&engine, step] { InterfaceScale::apply (engine, step); });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sizeButton));
    }

    void AppBar::paintOverChildren (juce::Graphics& g)
    {
        Planned::paintChips (g, *this);
    }

    void AppBar::resized()
    {
        auto r = getLocalBounds().reduced (10, 0);
        const auto h = Theme::buttonHeight;
        const auto fit = [h] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), h); };

        // Fixed parts first: what must always be visible
        const auto fileButtons = { &newButton, &openButton, &saveButton };
        const auto optional = { &helpButton, &proButton, &easyButton, &tunerButton, &exportButton };   // hidden first when narrow

        for (auto* b : optional)
            b->setVisible (true);

        for (auto* b : fileButtons)
            b->setShowText (true);

        const auto viewWidth = arrangeButton.getIdealWidth (h) + mixButton.getIdealWidth (h) + masterButton.getIdealWidth (h);
        const auto widthOf = [h] (std::initializer_list<IconButton*> list)
        {
            int total = 0;

            for (auto* b : list)
                if (b->isVisible())
                    total += b->getIdealWidth (h) + 2;

            return total;
        };

        auto needed = [&]
        {
            return 112 + 17 + widthOf (fileButtons) + 17 + 120 /* song name */ + viewWidth + 20
                 + outputBoxWidth + 10 + widthOf ({ &setupButton, &sizeButton }) + 10 + widthOf (optional) + 30;
        };

        for (auto* b : optional)
            if (needed() > r.getWidth())
                b->setVisible (false);

        easyButton.setVisible (proButton.isVisible());   // the pair hides together

        if (needed() > r.getWidth())
            for (auto* b : fileButtons)
                b->setShowText (false);   // icons only (tooltips still say what they do)

        // Left: logo, file buttons, song name
        logoArea = r.removeFromLeft (112);
        r.removeFromLeft (17);

        for (auto* b : fileButtons)
        {
            b->setBounds (fit (r.removeFromLeft (b->getIdealWidth (h))));
            r.removeFromLeft (2);
        }

        r.removeFromLeft (15);

        // Right, from the edge inwards
        for (auto* b : { &helpButton, &proButton, &easyButton, &tunerButton, &exportButton })
        {
            if (! b->isVisible())
                continue;

            b->setBounds (fit (r.removeFromRight (b->getIdealWidth (h))));
            r.removeFromRight (b == &proButton ? 0 : 10);
        }

        setupButton.setBounds (fit (r.removeFromRight (setupButton.getIdealWidth (h))));
        r.removeFromRight (2);
        sizeButton.setBounds (fit (r.removeFromRight (sizeButton.getIdealWidth (h))));
        r.removeFromRight (17);

        const auto outputWidth = juce::jlimit (outputBoxMinWidth, outputBoxWidth, r.getWidth() - viewWidth - 140);
        outputBox.setBounds (fit (r.removeFromRight (outputWidth)));
        outputIconArea = r.removeFromRight (22);
        r.removeFromRight (12);

        // Centre: the view switcher, centred in the whole bar when there's room
        const auto highest = r.getRight() - viewWidth / 2;                            // never past the output box
        const auto lowest = juce::jmin (highest, r.getX() + 120 + viewWidth / 2);     // song name room, if there is any
        const auto centreX = juce::jlimit (lowest, highest, getWidth() / 2);
        auto views = juce::Rectangle<int> (centreX - viewWidth / 2, 0, viewWidth, getHeight());

        for (auto* b : { &arrangeButton, &mixButton, &masterButton })
            b->setBounds (fit (views.removeFromLeft (b->getIdealWidth (h))));

        songNameArea = r.withRight (arrangeButton.getX() - 16);
    }

    void AppBar::refreshViewMode()
    {
        easyButton.setToggleState (! UiMode::isPro(), juce::dontSendNotification);
        proButton.setToggleState (UiMode::isPro(), juce::dontSendNotification);
    }

    void AppBar::timerCallback()
    {
        refreshOutputs();

        if (const auto unsaved = session.hasSong() && session.hasUnsavedChanges(); unsaved != lastUnsaved)
        {
            lastUnsaved = unsaved;
            repaint (songNameArea);
        }
    }

    void AppBar::mouseDown (const juce::MouseEvent& e)
    {
        if (songNameArea.contains (e.getPosition()))
            showSongMenu();
    }

    void AppBar::showSongMenu()
    {
        enum { saveAsId = 1, revealId, browseId, recentBase = 100 };
        juce::PopupMenu menu, recentMenu;
        const auto recent = AppPaths::getRecentSongs();

        for (int i = 0; i < recent.size(); ++i)
            recentMenu.addItem (recentBase + i, recent[i].getFileNameWithoutExtension(), recent[i] != session.getSongFile(),
                                recent[i] == session.getSongFile());

        if (session.hasSong())
        {
            menu.addSectionHeader (session.getSongName() + (lastUnsaved ? " (edited since saved)" : " (saved)"));
            menu.addCommandItem (&commandManager, CommandIDs::saveSong);
            menu.addCommandItem (&commandManager, CommandIDs::saveSongAs);
            menu.addItem (revealId, "Show the song's folder");
            menu.addSeparator();
        }

        menu.addCommandItem (&commandManager, CommandIDs::newSong);
        menu.addItem (browseId, "Open a song...\tCtrl+O");
        menu.addSubMenu ("Recent songs", recentMenu, ! recent.isEmpty());

        juce::Component::SafePointer<AppBar> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (songNameArea)),
                            [safe, recent] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;

                                if (result == revealId)
                                    safe->session.getSongFile().revealToUser();
                                else if (result == browseId)
                                    safe->commandManager.invokeDirectly (CommandIDs::openSong, true);
                                else if (juce::isPositiveAndBelow (result - recentBase, recent.size()) && safe->onOpenSong != nullptr)
                                    safe->onOpenSong (recent[result - recentBase]);
                            });
    }
}
