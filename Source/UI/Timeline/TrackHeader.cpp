/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "TrackHeader.h"
#include "../Theme.h"
#include "../TrackMenus.h"

namespace ez
{
    TrackHeader::TrackHeader (Session& s, TimelineViewState& v, int index)
        : session (s), view (v), trackIndex (index), meter (s, index, false)
    {
        addAndMakeVisible (meter);
        name.setEditable (false, true);   // double-click to rename
        name.setFont (Theme::font (Theme::baseFontSize, true));
        name.setBorderSize ({ 0, 0, 0, 0 });
        name.setMinimumHorizontalScale (1.0f);   // long names end in "..." rather than squashing
        name.setTooltip ("Double-click to rename. Right-click for more.");
        name.addMouseListener (this, false);   // right-click on the name opens the track menu too
        name.onEditorShow = [this] { resized(); };   // the editor gets the whole row, not the old name's width
        name.onEditorHide = [this] { resized(); };
        name.onTextChange = [this]
        {
            if (! session.renameTrack (trackIndex, name.getText()))
                refresh();
        };
        addAndMakeVisible (name);

        muteButton.setTooltip ("Mute (M on the highlighted track)");
        soloButton.setTooltip ("Solo: hear only soloed tracks (S on the highlighted track)");
        armButton.setTooltip ("Arm for recording (Shift+R). Pick the input in Routing");
        fxButton.setTooltip ("Instrument and effects on this track (right-click the track for more)");

        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9a441));
        soloButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        armButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        // The design's on colours: amber mute and blue solo with dark text, red arm with white
        muteButton.setColour (juce::TextButton::textColourOnId, Theme::ink);
        soloButton.setColour (juce::TextButton::textColourOnId, Theme::ink);
        armButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        muteButton.onClick = [this] { session.setTrackMuted (trackIndex, ! session.isTrackMuted (trackIndex)); refresh(); };
        soloButton.onClick = [this] { session.setTrackSoloed (trackIndex, ! session.isTrackSoloed (trackIndex)); refresh(); };
        armButton.onClick  = [this]
        {
            if (! session.setTrackArmed (trackIndex, ! session.isTrackArmed (trackIndex)))
                showProblem (session.getLastError());

            refresh();
        };
        fxButton.onClick = [this] { showFxMenu(); };
        autoButton.setTooltip ("Automation lanes: show how volume, pan or any control moves over the song (A)");
        autoButton.setColour (juce::TextButton::buttonOnColourId, Theme::activeFill);
        autoButton.onClick = [this] { session.toggleAutomationLanes (trackIndex); };

        for (auto* b : { &muteButton, &soloButton, &armButton, &fxButton, &autoButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        volume.setRange (Session::minVolumeDb, Session::maxVolumeDb, 0.1);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTooltip ("Volume (double-click for 0 dB)");
        volume.textFromValueFunction = [] (double db) { return juce::String (db, 1) + " dB"; };
        volume.setPopupDisplayEnabled (true, true, nullptr);
        // One drag = one undo step; a wheel step or typed value is its own step
        // A drag is also a gesture: with Write on and the song playing, it records into the volume lane
        volume.onDragStart = [this] { session.beginUndoStep(); volumeDragging = true; session.beginParameterGesture (trackIndex, session.getVolumeParam (trackIndex)); };
        volume.onDragEnd   = [this] { volumeDragging = false; session.endParameterGesture (trackIndex, session.getVolumeParam (trackIndex)); refresh(); };
        volume.onValueChange = [this]
        {
            if (! volumeDragging)
                session.beginUndoStep();

            if (! session.setTrackVolumeDb (trackIndex, (float) volume.getValue()) && ! volumeDragging)
                refresh();   // automated: the lane wins (the status bar says why)

            repaint (dbArea);
        };

        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Pan (double-click for centre)");
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.onDragStart = [this] { session.beginUndoStep(); panDragging = true; session.beginParameterGesture (trackIndex, session.getPanParam (trackIndex)); };
        pan.onDragEnd   = [this] { panDragging = false; session.endParameterGesture (trackIndex, session.getPanParam (trackIndex)); refresh(); };
        pan.onValueChange = [this]
        {
            if (! panDragging)
                session.beginUndoStep();

            if (! session.setTrackPan (trackIndex, (float) pan.getValue()) && ! panDragging)
                refresh();

            repaint (panLabelArea);
        };

        for (auto* slider : { &volume, &pan })
        {
            slider->setWantsKeyboardFocus (false);
            addAndMakeVisible (slider);
        }

        refresh();
    }

    TrackHeader::~TrackHeader()
    {
        if (volumeDragging)  session.endParameterGesture (trackIndex, session.getVolumeParam (trackIndex));
        if (panDragging)     session.endParameterGesture (trackIndex, session.getPanParam (trackIndex));
    }

    void TrackHeader::refresh()
    {
        if (! name.isBeingEdited())
            name.setText (session.getTrackName (trackIndex), juce::dontSendNotification);

        bool needsLayout = ! name.isBeingEdited() && name.getText() != laidOutName;   // the device text starts after the name

        if (const auto newKind = session.getTrackKind (trackIndex); newKind != kind)
        {
            kind = newKind;
            armButton.setEnabled (kind == Session::TrackKind::normal);
            armButton.setTooltip (kind == Session::TrackKind::normal ? "Arm for recording (Shift+R). Pick the input in Routing" : "Buses and FX returns don't record");
            name.setTooltip (kind == Session::TrackKind::bus ? "Bus: tracks play into it (their Output). Double-click to rename, right-click for more."
                           : kind == Session::TrackKind::fxReturn ? "FX return: plays what tracks send to it (Sends, in the Mix). Double-click to rename, right-click for more."
                           : "Double-click to rename. Right-click for more.");
            resized();
            repaint();
        }
        muteButton.setToggleState (session.isTrackMuted (trackIndex), juce::dontSendNotification);
        soloButton.setToggleState (session.isTrackSoloed (trackIndex), juce::dontSendNotification);
        armButton.setToggleState (session.isTrackArmed (trackIndex), juce::dontSendNotification);
        autoButton.setToggleState (! session.getShownAutomationLanes (trackIndex).empty(), juce::dontSendNotification);

        // An automated control follows its lane: say so where you'd grab it
        const auto volumeLocked = session.isParamLockedByAutomation (trackIndex, session.getVolumeParam (trackIndex));
        const auto panLocked = session.isParamLockedByAutomation (trackIndex, session.getPanParam (trackIndex));
        volume.setTooltip (volumeLocked ? "Volume follows its automation lane (edit the lane, or turn on Write and move it while playing)" : "Volume (double-click for 0 dB)");
        pan.setTooltip (panLocked ? "Pan follows its automation lane" : "Pan (double-click for centre)");
        if (volumeLocked)
            volume.setColour (juce::Slider::thumbColourId, Theme::accent);
        else
            volume.removeColour (juce::Slider::thumbColourId);

        if (! volume.isMouseButtonDown())
            volume.setValue (session.getTrackVolumeDb (trackIndex), juce::dontSendNotification);

        if (! pan.isMouseButtonDown())
            pan.setValue (session.getTrackPan (trackIndex), juce::dontSendNotification);

        const auto effects = session.getEffectNames (trackIndex).size();
        const auto fxText = effects > 0 ? "FX " + juce::String (effects) : juce::String ("FX");
        needsLayout = needsLayout || fxText.length() != fxButton.getButtonText().length();   // "FX 2" is wider than "FX"
        fxButton.setButtonText (fxText);

        // What plays on it: the instrument, or the input it records from
        auto newIcon = Icon::mic;
        juce::String newDevice;

        if (kind == Session::TrackKind::bus)            newIcon = Icon::layers;
        else if (kind == Session::TrackKind::fxReturn)  newIcon = Icon::chain;
        else if (const auto instrument = session.getInstrumentInfo (trackIndex))
        {
            newIcon = Icon::piano;
            newDevice = instrument->name;
        }
        else
        {
            // The input list asks the device manager: only look it up when the input changes
            const auto input = session.getTrackInput (trackIndex);

            if (input != lastInputID || ! inputNameKnown)
            {
                lastInputID = input;
                lastInputName = {};
                inputNameKnown = true;

                if (input.isNotEmpty())
                    for (const auto& choice : session.getTrackInputChoices())
                        if (choice.id == input)
                            lastInputName = choice.name;
            }

            newDevice = lastInputName;
        }

        if (newIcon != typeIcon || newDevice != deviceText)
        {
            typeIcon = newIcon;
            deviceText = newDevice;
            needsLayout = true;
        }

        if (needsLayout)
        {
            resized();
            repaint();
        }

        repaint (dbArea.getUnion (panLabelArea));

        const bool isFocused = view.focusedTrack == trackIndex;
        const auto colour = session.getTrackColour (trackIndex);

        if (isFocused != wasFocused || colour != lastColour)
        {
            wasFocused = isFocused;
            lastColour = colour;
            repaint();
        }
    }

    void TrackHeader::paint (juce::Graphics& g)
    {
        g.fillAll (view.focusedTrack == trackIndex ? juce::Colour (0xff2c313b) : Theme::panel);

        // The colour bar: the track's colour, or a quiet grey when it has none
        g.setColour (lastColour.isTransparent() ? Theme::border : lastColour);
        g.fillRect (0, 0, 4, getHeight());

        Icons::draw (g, typeIcon, iconArea.toFloat(), Theme::textDim, 1.6f);

        if (! deviceArea.isEmpty() && deviceText.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize));
            g.drawText (deviceText, deviceArea, juce::Justification::centredLeft, true);
        }

        if (! kindChip.isEmpty())
        {
            const auto chip = kindChip.toFloat().withSizeKeepingCentre ((float) kindChip.getWidth() - 2.0f, 16.0f);
            g.setColour (kind == Session::TrackKind::bus ? Theme::selection.withAlpha (0.35f) : Theme::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.drawText (kind == Session::TrackKind::bus ? "BUS" : "FX", chip, juce::Justification::centred, false);
        }

        // Volume and pan, read out next to their controls
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::smallFontSize));

        if (! dbArea.isEmpty())
        {
            const auto db = volume.getValue();
            g.drawText (db <= Session::minVolumeDb + 0.05 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB",
                        dbArea, juce::Justification::centredLeft, false);
        }

        if (! panLabelArea.isEmpty())
        {
            const auto amount = juce::roundToInt (pan.getValue() * 100.0);
            g.drawText (amount == 0 ? juce::String ("C") : (amount < 0 ? "L" : "R") + juce::String (std::abs (amount)),
                        panLabelArea, juce::Justification::centredLeft, false);
        }

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void TrackHeader::resized()
    {
        auto r = getLocalBounds().withTrimmedLeft (4).withTrimmedBottom (1);   // colour bar, separator
        r = r.withTrimmedLeft (8).withTrimmedRight (6).reduced (0, 5);

        meter.setBounds (r.removeFromRight (9).reduced (0, 1));
        r.removeFromRight (8);

        const bool tall = getHeight() >= 80;
        const int rowHeight = getHeight() >= 56 ? 20 : 17;
        const int rows = tall ? 3 : 2;
        const int gap = juce::jlimit (2, 7, (r.getHeight() - rows * rowHeight) / juce::jmax (1, rows - 1));
        r = r.withSizeKeepingCentre (r.getWidth(), rows * rowHeight + (rows - 1) * gap);

        // Row 1: icon, name, device ... kind chip, FX
        auto top = r.removeFromTop (rowHeight);
        r.removeFromTop (gap);

        iconArea = top.removeFromLeft (15).withSizeKeepingCentre (14, 14);
        top.removeFromLeft (6);
        fxButton.setBounds (top.removeFromRight (fxButton.getButtonText().length() > 2 ? 42 : 30));
        top.removeFromRight (4);
        kindChip = kind != Session::TrackKind::normal ? top.removeFromRight (kind == Session::TrackKind::bus ? 32 : 24) : juce::Rectangle<int>();

        const auto nameWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (name.getFont(), name.getText())) + 4;
        const auto room = top.getWidth();
        // Renaming gets the whole row; otherwise the name is as wide as it needs and the device follows
        name.setBounds (top.removeFromLeft (deviceText.isEmpty() || name.isBeingEdited() ? room : juce::jmin (nameWidth, room * 2 / 3)));
        laidOutName = name.getText();
        top.removeFromLeft (6);
        deviceArea = top;

        // Volume row parts (the last row): slider, dB, pan knob, pan label
        const auto placeVolume = [this] (juce::Rectangle<int> row)
        {
            // Read-outs only when the fader keeps a usable length (dragging shows the value anyway)
            constexpr int minFader = 64;
            const bool roomForDb = row.getWidth() - 28 - 2 - (row.getHeight() + 2) - 6 - 52 >= minFader;
            const bool roomForPanLabel = row.getWidth() - (row.getHeight() + 2) - 6 - 30 >= minFader;

            panLabelArea = roomForPanLabel ? row.removeFromRight (28) : juce::Rectangle<int>();
            row.removeFromRight (roomForPanLabel ? 2 : 0);
            pan.setBounds (row.removeFromRight (row.getHeight() + 2).expanded (0, 1));
            row.removeFromRight (6);
            dbArea = roomForDb ? row.removeFromRight (48) : juce::Rectangle<int>();
            row.removeFromRight (roomForDb ? 4 : 0);
            volume.setBounds (row);
        };

        const auto placeButtons = [this] (juce::Rectangle<int>& row, int width)
        {
            for (auto* b : { &muteButton, &soloButton, &armButton })
            {
                b->setBounds (row.removeFromLeft (width));
                row.removeFromLeft (3);
            }
        };

        if (tall)
        {
            // Row 2: M S R ... A     Row 3: volume, dB, pan
            auto buttons = r.removeFromTop (rowHeight);
            r.removeFromTop (gap);
            placeButtons (buttons, 24);
            autoButton.setBounds (buttons.removeFromRight (24));
            placeVolume (r.removeFromTop (rowHeight));
        }
        else
        {
            // Row 2: M S R  volume dB pan  A
            auto row = r.removeFromTop (rowHeight);
            placeButtons (row, 20);
            row.removeFromLeft (3);
            autoButton.setBounds (row.removeFromRight (20));
            row.removeFromRight (6);
            placeVolume (row);
        }
    }

    void TrackHeader::mouseDown (const juce::MouseEvent& e)
    {
        view.focusedTrack = trackIndex;

        if (e.mods.isPopupMenu())
            showTrackMenu();

        if (auto* parent = getParentComponent())
            parent->repaint();   // move the focus highlight
    }

    void TrackHeader::showTrackMenu()
    {
        TrackMenus::Callbacks callbacks;
        juce::Component::SafePointer<TrackHeader> safe (this);
        auto& viewState = view;   // outlives the headers (the timeline rebuilds them when tracks change)
        callbacks.rename = [safe] { if (safe != nullptr) safe->name.showEditor(); };
        callbacks.moved = [&viewState] (int newIndex) { viewState.focusedTrack = newIndex; };
        callbacks.showTrack = [&viewState] (int t) { viewState.focusedTrack = t; };
        callbacks.problem = [safe] (const juce::String& message) { if (safe != nullptr) safe->showProblem (message); };
        TrackMenus::showTrackMenu (session, trackIndex, callbacks);
    }

    void TrackHeader::showFxMenu()
    {
        juce::PopupMenu menu;
        const auto effectNames = session.getEffectNames (trackIndex);
        const auto choices = session.getAvailableEffects();

        constexpr int openInstrumentId = 1, openBase = 100, removeBase = 200, addBase = 1000;

        menu.addItem (openInstrumentId, "Open instrument");

        if (! effectNames.isEmpty())
        {
            menu.addSectionHeader ("Effects on this track");

            for (int i = 0; i < effectNames.size(); ++i)
                menu.addItem (openBase + i, "Open " + effectNames[i]);

            juce::PopupMenu removeMenu;

            for (int i = 0; i < effectNames.size(); ++i)
                removeMenu.addItem (removeBase + i, effectNames[i]);

            menu.addSubMenu ("Remove effect", removeMenu);
        }

        menu.addSectionHeader ("Add effect");
        TrackMenus::addEffectChoices (menu, choices, addBase - 1);   // IDs addBase + index

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fxButton),
                            [safeThis = juce::Component::SafePointer<TrackHeader> (this), choices] (int result)
                            {
                                if (safeThis == nullptr || result <= 0)
                                    return;

                                auto& s = safeThis->session;
                                const auto t = safeThis->trackIndex;
                                bool ok = true;

                                if (result == openInstrumentId)                         ok = s.openInstrumentWindow (t);
                                else if (result >= addBase)                             ok = s.addEffect (t, choices[result - addBase], true);
                                else if (result >= removeBase)                          ok = s.removeEffect (t, result - removeBase);
                                else if (result >= openBase)                            ok = s.openEffectWindow (t, result - openBase);

                                if (! ok)
                                    safeThis->showProblem (s.getLastError());

                                safeThis->refresh();
                            });
    }

    void TrackHeader::showProblem (const juce::String& message)
    {
        auto* callout = new juce::Label ({}, message);
        callout->setSize (320, 60);
        callout->setJustificationType (juce::Justification::centred);
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (callout), fxButton.getScreenBounds(), nullptr);
    }
}
