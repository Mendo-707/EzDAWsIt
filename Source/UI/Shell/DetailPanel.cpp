/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "DetailPanel.h"
#include "DevicesView.h"
#include "PianoRollView.h"
#include "DrumMachineView.h"

namespace ez
{
    namespace
    {
        constexpr int minBodyHeight = 150, maxBodyHeight = 420;
    }

    DetailPanel::DetailPanel (Session& s) : session (s)
    {
        devices = std::make_unique<DevicesView> (session);
        addChildComponent (*devices);
        pianoRoll = std::make_unique<PianoRollView> (session);
        addChildComponent (*pianoRoll);
        drums = std::make_unique<DrumMachineView> (session);
        addChildComponent (*drums);

        stepsView.setTooltip ("Edit this drum clip as steps");
        notesView.setTooltip ("Edit this drum clip in the piano roll");
        stepsView.onClick = [this] { drumNotes = false; updateChildren(); };
        notesView.onClick = [this] { drumNotes = true; updateChildren(); };
        addChildComponent (stepsView);
        addChildComponent (notesView);

        expandButton.setTooltip ("Open or close the panel");
        expandButton.onClick = [this] { setOpen (! open); };
        devicesTab.setTooltip ("The highlighted track's instrument and effects (D)");
        devicesTab.onClick = [this] { toggleTab (Tab::devices); };
        clipEditorTab.setTooltip ("Edit the notes of the selected MIDI clip (E)");
        clipEditorTab.onClick = [this] { toggleTab (Tab::clipEditor); };

        for (auto* b : { &expandButton, &devicesTab, &clipEditorTab })
            addAndMakeVisible (b);

        session.addListener (this);
        startTimerHz (5);
    }

    DetailPanel::~DetailPanel()
    {
        session.removeListener (this);
    }

    void DetailPanel::setFocusedTrackSource (std::function<int()> source)
    {
        getFocusedTrack = source;
        devices->getFocusedTrack = source;
        drums->getFocusedTrack = std::move (source);
    }

    void DetailPanel::setProblemHandler (std::function<void (const juce::String&, const juce::String&)> handler)
    {
        devices->onProblem = handler;
        pianoRoll->onProblem = handler;
        drums->onProblem = std::move (handler);
    }

    void DetailPanel::setOpen (bool shouldBeOpen)
    {
        if (open == shouldBeOpen)
            return;

        open = shouldBeOpen;
        updateChildren();
        expandButton.setIcon (open ? Icon::chevronDown : Icon::chevronUp);
        repaint();

        if (onHeightChanged)
            onHeightChanged();
    }

    void DetailPanel::toggleTab (Tab t)
    {
        if (open && tab == t)
        {
            setOpen (false);
            return;
        }

        showTab (t);
        setOpen (true);
    }

    void DetailPanel::showTab (Tab t)
    {
        if (tab == t)
            return;

        tab = t;
        updateChildren();
        repaint (headerArea);
    }

    bool DetailPanel::shouldShowDrums() const
    {
        return drums->hasSomethingToShow() && ! (drumNotes && drums->hasClip());
    }

    void DetailPanel::updateChildren()
    {
        showingDrums = shouldShowDrums();
        lastDrumClip = drums->hasSomethingToShow() && drums->hasClip();
        const bool clipEditorOpen = open && tab == Tab::clipEditor;
        devices->setVisible (open && tab == Tab::devices);
        pianoRoll->setVisible (clipEditorOpen && ! showingDrums);
        drums->setVisible (clipEditorOpen && showingDrums);

        const bool drumClip = clipEditorOpen && drums->hasSomethingToShow() && drums->hasClip();
        stepsView.setVisible (drumClip);
        notesView.setVisible (drumClip);
        stepsView.setToggleState (drumClip && ! drumNotes, juce::dontSendNotification);
        notesView.setToggleState (drumClip && drumNotes, juce::dontSendNotification);
        resized();   // the Steps / Notes buttons come and go
        devicesTab.setToggleState (open && tab == Tab::devices, juce::dontSendNotification);
        clipEditorTab.setToggleState (open && tab == Tab::clipEditor, juce::dontSendNotification);
    }

    bool DetailPanel::pianoRollHasEditFocus() const
    {
        return open && tab == Tab::clipEditor && pianoRoll->isVisible() && pianoRoll->hasEditFocus();
    }

    EditorTarget* DetailPanel::getFocusedEditor() const
    {
        if (! open || tab != Tab::clipEditor)
            return nullptr;

        if (pianoRoll->isVisible() && pianoRoll->hasEditFocus())
            return pianoRoll.get();

        if (drums->isVisible() && drums->hasEditFocus())
            return drums.get();

        return nullptr;
    }

    int DetailPanel::getPreferredHeight() const
    {
        return Theme::detailHeaderHeight + (open ? bodyHeight : 0);
    }

    void DetailPanel::fitDefaultHeightTo (int mainAreaHeight)
    {
        if (! bodySizedByUser)
            bodyHeight = juce::jlimit (230, maxBodyHeight, juce::roundToInt (mainAreaHeight * 0.38));
    }

    void DetailPanel::timerCallback()
    {
        // Selecting a different MIDI clip while the panel is open brings up its notes.
        // Only on a change, so you can still switch back to Devices with the clip selected.
        const auto selectedClips = session.getSelectedClips();
        auto* midiClip = selectedClips.size() == 1 ? dynamic_cast<te::MidiClip*> (selectedClips.getFirst()) : nullptr;
        const auto midiID = midiClip != nullptr ? midiClip->itemID : te::EditItemID();

        if (midiID != lastSelectedMidiClip)
        {
            lastSelectedMidiClip = midiID;

            if (open && midiID.isValid())
                showTab (Tab::clipEditor);
        }

        // A drum track or clip shows the drum machine, anything else the piano roll
        const auto drumClipNow = drums->hasSomethingToShow() && drums->hasClip();

        if (shouldShowDrums() != showingDrums || drumClipNow != lastDrumClip)
            updateChildren();

        // The highlighted track changes on click, with no notification: check a few times a second
        const auto index = getFocusedTrack ? getFocusedTrack() : -1;
        const auto valid = session.hasSong() && juce::isPositiveAndBelow (index, session.getNumAudioTracks());
        const auto name = valid ? session.getTrackName (index) : juce::String();
        const auto colour = valid ? session.getTrackColour (index) : juce::Colour();

        if (name != trackName || colour != trackColour)
        {
            trackName = name;
            trackColour = colour;
            repaint (headerArea);
        }
    }

    //==============================================================================
    // Dragging the header changes the height (only while open)
    void DetailPanel::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (open && headerArea.contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor
                                                                      : juce::MouseCursor::NormalCursor);
    }

    void DetailPanel::mouseDown (const juce::MouseEvent&)
    {
        // From the height on screen (a small window shows less than bodyHeight): no dead zone at the start of a drag
        bodyHeightAtDragStart = open ? juce::jmin (bodyHeight, getHeight() - Theme::detailHeaderHeight) : bodyHeight;
    }

    void DetailPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (! open)
            return;

        resizeBodyTo (bodyHeightAtDragStart - e.getDistanceFromDragStartY());
    }

    void DetailPanel::resizeBodyTo (int newBodyHeight)
    {
        const auto newHeight = juce::jlimit (minBodyHeight, maxBodyHeight, newBodyHeight);
        bodySizedByUser = true;   // the user's size stays, whatever the window does

        if (newHeight != bodyHeight)
        {
            bodyHeight = newHeight;

            if (onHeightChanged)
                onHeightChanged();
        }
    }

    void DetailPanel::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (headerArea.contains (e.getPosition()))
            setOpen (! open);
    }

    //==============================================================================
    void DetailPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        // The header: the same surface as the transport, so the editors below read as part of the shell
        g.setGradientFill (juce::ColourGradient::vertical (Theme::panel.brighter (0.03f), (float) headerArea.getY(), Theme::panel, (float) headerArea.getBottom()));
        g.fillRect (headerArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (headerArea.getY(), 0.0f, (float) getWidth());
        g.setColour (Theme::hairline);
        g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

        if (open)
        {
            // A grip in the middle of the header says "drag me"
            const auto cx = (float) headerArea.getCentreX();
            g.setColour (Theme::border);

            for (int i = 0; i < 2; ++i)
                g.fillRoundedRectangle (cx - 14.0f, (float) headerArea.getY() + 3.0f + (float) i * 3.0f, 28.0f, 1.5f, 0.75f);
        }

        if (trackName.isNotEmpty())
        {
            auto r = contextArea;
            const auto dot = r.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat();
            g.setColour (trackColour.isTransparent() ? Theme::textDim : trackColour);
            g.fillRoundedRectangle (dot, 2.0f);
            r.removeFromLeft (6);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize - 0.5f, true));
            g.drawText (trackName, r, juce::Justification::centredLeft, true);
        }

        if (! open)
        {
            g.setColour (Theme::textDim);
            // The longest hint that fits whole (a cut-off hint reads as a bug)
            const auto font = Theme::font (Theme::smallFontSize);
            g.setFont (font);

            for (auto* hint : { "D: devices   E: clip editor   Double-click a MIDI clip to edit it", "D: devices   E: clip editor" })
            {
                if (juce::GlyphArrangement::getStringWidthInt (font, hint) <= hintArea.getWidth())
                {
                    g.drawText (hint, hintArea, juce::Justification::centredRight, false);
                    break;
                }
            }
        }
    }

    void DetailPanel::paintOverChildren (juce::Graphics& g)
    {
        Planned::paintChips (g, *this);
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (Theme::detailHeaderHeight);
        devices->setBounds (r);
        pianoRoll->setBounds (r);
        drums->setBounds (r);

        auto h = headerArea.reduced (8, 0);
        expandButton.setBounds (h.removeFromLeft (24).withSizeKeepingCentre (24, 22));
        h.removeFromLeft (4);
        contextArea = h.removeFromLeft (150);

        for (auto* button : { &devicesTab, &clipEditorTab })
        {
            const auto w = button->getIdealWidth (24);
            button->setBounds (h.removeFromLeft (w).withSizeKeepingCentre (w, 24));
            h.removeFromLeft (6);
        }

        if (stepsView.isVisible())
        {
            h.removeFromLeft (6);

            for (auto* button : { &stepsView, &notesView })
            {
                const auto w = button->getIdealWidth (22);
                button->setBounds (h.removeFromLeft (w).withSizeKeepingCentre (w, 22));
                h.removeFromLeft (2);
            }
        }

        hintArea = h.withTrimmedLeft (12);
    }
}
