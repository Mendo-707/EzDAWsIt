/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "TransportBar.h"
#include "../UiMode.h"
#include "../CommandIDs.h"
#include "../Timeline/TempoLane.h"

namespace ez
{
    //==============================================================================
    /** Bar.beat.sixteenth over the time, LCD style. */
    class TransportBar::PositionDisplay : public juce::Component,
                                          public juce::SettableTooltipClient
    {
    public:
        explicit PositionDisplay (Session& s) : session (s)
        {
            setTooltip ("Playhead position: bar.beat.sixteenth and minutes:seconds");
            setOpaque (true);   // repaints 30 times a second while playing: don't repaint the bar behind it
        }

        /** Repaints only when the text changed (called at 30 Hz). */
        void refresh()
        {
            auto newBars = session.getBarsBeatsText(), newTime = session.getTimeText();

            if (newBars != bars || newTime != time)
            {
                bars = std::move (newBars);
                time = std::move (newTime);
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);   // the corners outside the rounded box (opaque component)
            drawLcd (g, getLocalBounds());
            auto r = getLocalBounds().reduced (10, 2);
            const auto used = drawLcdField (g, r, "BAR", bars.isEmpty() ? "-" : bars, Theme::positionFontSize);
            r.removeFromLeft (used + 12);
            drawLcdField (g, r, "TIME", time.isEmpty() ? "-" : time, Theme::baseFontSize + 1.5f);
        }

    private:
        Session& session;
        juce::String bars, time;
    };

    //==============================================================================
    /** Tempo readout you can drag, scroll or type into. One drag = one undo step. */
    class TransportBar::TempoDisplay : public juce::Component,
                                       public juce::SettableTooltipClient
    {
    public:
        explicit TempoDisplay (Session& s) : session (s)
        {
            setTooltip ("Tempo at the playhead: drag up or down (Shift for fine), scroll, or double-click to type. "
                        "Right-click: double or half time. K taps the tempo in.");
            setOpaque (true);
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

            editor.setJustification (juce::Justification::centredLeft);
            editor.setInputRestrictions (6, "0123456789.");
            editor.setFont (Theme::font (Theme::baseFontSize + 3.0f, true));
            editor.setColour (juce::TextEditor::textColourId, Theme::displayText);
            editor.onReturnKey = [this] { commitTyping(); };
            editor.onEscapeKey = [this] { editor.setVisible (false); };
            editor.onFocusLost = [this] { commitTyping(); };
            addChildComponent (editor);
        }

        void refresh()
        {
            const auto bpm = session.hasSong() ? session.getBpmAt (session.getPosition()) : 0.0;

            if (bpm != shownBpm)
            {
                shownBpm = bpm;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);
            drawLcd (g, getLocalBounds());

            if (editor.isVisible())
                return;

            auto r = getLocalBounds().reduced (10, 0);
            g.setFont (Theme::font (Theme::baseFontSize + 3.5f, true));
            g.setColour (Theme::displayText);
            const auto value = session.hasSong() ? juce::String (shownBpm, 2) : juce::String ("-");
            const auto valueWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), value);
            g.drawText (value, r.removeFromLeft (valueWidth + 2), juce::Justification::centredLeft, false);
            r.removeFromLeft (6);
            g.setFont (Theme::font (Theme::captionFontSize, true));
            g.setColour (Theme::displayLabel);
            g.drawText ("BPM", r, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            editor.setBounds (getLocalBounds().reduced (4, 4));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! session.hasSong())
                return;

            if (e.mods.isPopupMenu())
            {
                // Double or half time (trap at 70 or 140), and how to tap it in
                const auto markers = session.getTempoMarkers();
                const auto index = juce::jlimit (0, (int) markers.size() - 1, session.getEdit()->tempoSequence.indexOfTempoAt (session.getPosition()));
                const auto bpm = markers[(size_t) index].bpm;
                juce::PopupMenu menu;
                menu.addItem (1, "Double time (" + juce::String (bpm * 2.0, 1) + " BPM)", bpm * 2.0 <= 300.0);
                menu.addItem (2, "Half time (" + juce::String (bpm / 2.0, 1) + " BPM)", bpm / 2.0 >= 30.0);
                juce::PopupMenu::Item tap ("Tap tempo: press K in time with the music");
                tap.itemID = 3;
                tap.shortcutKeyDescription = "K";
                menu.addItem (tap);

                juce::Component::SafePointer<TempoDisplay> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, index, bpm] (int result)
                {
                    if (safe == nullptr || result <= 0)
                        return;

                    if (result == 3)
                        safe->session.tapTempo();
                    else if (! safe->session.setTempoChangeBpm (index, result == 1 ? bpm * 2.0 : bpm / 2.0, false))
                        safe->session.showHint (safe->session.getLastError());

                    safe->refresh();
                });

                dragTempo = -1;
                return;
            }

            // The marker in effect at the playhead: start from its own value (inside a glide the display shows the
            // glide's current tempo, but the marker is what changes), and keep editing it even if the playhead moves on
            const auto markers = session.getTempoMarkers();
            dragTempo = juce::jlimit (0, (int) markers.size() - 1, session.getEdit()->tempoSequence.indexOfTempoAt (session.getPosition()));
            bpmAtMouseDown = markers[(size_t) dragTempo].bpm;
            dragStarted = false;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! session.hasSong() || dragTempo < 0)
                return;

            const auto perPixel = e.mods.isShiftDown() ? 0.02 : 0.25;
            const auto newBpm = bpmAtMouseDown - e.getDistanceFromDragStartY() * perPixel;
            const bool ok = session.setTempoChangeBpm (dragTempo, e.mods.isShiftDown() ? newBpm : std::round (newBpm), dragStarted);   // one undo step per drag
            dragStarted = dragStarted || ok;   // a refused step (recording) must not let the rest join an older step
            refresh();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (! session.hasSong() || wheel.deltaY == 0.0f)
                return;

            const auto markers = session.getTempoMarkers();
            const auto index = juce::jlimit (0, (int) markers.size() - 1, session.getEdit()->tempoSequence.indexOfTempoAt (session.getPosition()));
            session.setTempoChangeBpm (index, markers[(size_t) index].bpm + (wheel.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? 0.1 : 1.0));
            refresh();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (! session.hasSong())
                return;

            editor.setText (juce::String (session.getBpmAt (session.getPosition()), 2), false);
            editor.setVisible (true);
            editor.grabKeyboardFocus();
            editor.selectAll();
            repaint();
        }

    private:
        Session& session;
        juce::TextEditor editor;
        double shownBpm = -1.0, bpmAtMouseDown = 120.0;
        bool dragStarted = false;
        int dragTempo = 0;

        void commitTyping()
        {
            if (! editor.isVisible())
                return;

            editor.setVisible (false);   // first: committing can run again from onFocusLost
            const auto typed = editor.getText().getDoubleValue();

            if (typed > 0.0 && session.hasSong())
                session.setBpmAt (session.getPosition(), typed);

            refresh();
            repaint();
        }
    };

    //==============================================================================
    /** A small LCD showing a value at the playhead (time signature, key). Click for a menu, scroll to step. */
    class TransportBar::LcdText : public juce::Component,
                                  public juce::SettableTooltipClient
    {
    public:
        LcdText (std::function<juce::String()> v, juce::String c) : getValue (std::move (v)), caption (std::move (c))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            value = getValue();
        }

        std::function<void()> onClick;
        std::function<void (int direction)> onWheel;

        /** True if the text changed width (the bar must lay out again). */
        bool refresh()
        {
            const auto newValue = getValue();

            if (newValue == value)
                return false;

            const auto oldWidth = getIdealWidth();
            value = newValue;
            repaint();
            return getIdealWidth() != oldWidth;
        }

        int getIdealWidth() const
        {
            return 20 + juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::baseFontSize + 1.0f, true), value)
                      + (caption.isEmpty() ? 0 : 6 + juce::GlyphArrangement::getStringWidthInt (Theme::font (Theme::captionFontSize, true), caption));
        }

        void paint (juce::Graphics& g) override
        {
            drawLcd (g, getLocalBounds());
            auto r = getLocalBounds().reduced (10, 0);
            g.setFont (Theme::font (Theme::baseFontSize + 1.0f, true));
            g.setColour (isMouseOver() ? Theme::displayText.brighter (0.3f) : Theme::displayText);
            const auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), value);
            g.drawText (value, r.removeFromLeft (w + 2), juce::Justification::centredLeft, false);

            if (caption.isNotEmpty())
            {
                r.removeFromLeft (4);
                g.setFont (Theme::font (Theme::captionFontSize, true));
                g.setColour (Theme::displayLabel);
                g.drawText (caption, r, juce::Justification::centredLeft, false);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick)
                onClick();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            if (onWheel && wheel.deltaY != 0.0f)
                onWheel (wheel.deltaY > 0 ? 1 : -1);
        }

    private:
        std::function<juce::String()> getValue;
        juce::String value, caption;
    };

    //==============================================================================
    TransportBar::TransportBar (Session& s, juce::ApplicationCommandManager& commands)
        : session (s)
    {
        const std::pair<IconButton*, juce::CommandID> commandButtons[] =
        {
            { &returnButton, CommandIDs::returnToStart }, { &playButton, CommandIDs::togglePlay },
            { &recordButton, CommandIDs::toggleRecord },  { &loopButton, CommandIDs::toggleLoop },
            { &clickButton, CommandIDs::toggleClick },    { &countInButton, CommandIDs::toggleCountIn },
            { &lowLatencyButton, CommandIDs::toggleLowLatency },
            { &snapButton, CommandIDs::toggleSnap },      { &undoButton, CommandIDs::undo },
            { &redoButton, CommandIDs::redo },
            { &punchInButton, CommandIDs::punchInHere }, { &punchOutButton, CommandIDs::punchOutHere },
            { &syncButton, CommandIDs::toggleMidiClock },
            { &gridButton, CommandIDs::gridMenu },
            { &selectToolButton, CommandIDs::selectTool }, { &splitToolButton, CommandIDs::splitTool },
            { &eraseToolButton, CommandIDs::eraseTool },
        };

        for (auto [button, id] : commandButtons)
        {
            button->setCommandToTrigger (&commands, id, true);   // tooltip, shortcut and on/off state come from the command
            addAndMakeVisible (button);
        }

        recordButton.setIconColour (Theme::recordIcon);

        position = std::make_unique<PositionDisplay> (session);
        tempo = std::make_unique<TempoDisplay> (session);
        signature = std::make_unique<LcdText> ([this]
        {
            if (! session.hasSong())
                return juce::String ("-");

            const auto [n, d] = session.getTimeSigAt (session.getPosition());
            return juce::String (n) + "/" + juce::String (d);
        }, "");

        key = std::make_unique<LcdText> ([this]
        {
            if (! session.hasSong())
                return juce::String ("-");

            const auto k = session.getKeyAt (session.getPosition());
            return k.root < 0 ? juce::String ("No key") : Session::describeKey (k.root, k.minor);
        }, "");

        signature->setTooltip ("Time signature at the playhead: click to change it, scroll to add or remove a beat per bar. "
                               "Add changes later in the song in the lane above the ruler.");
        key->setTooltip ("Key at the playhead: click to choose, scroll for the next or previous key. "
                         "The piano roll lights up its notes. Add key changes in the lane above the ruler.");

        signature->onClick = [this]
        {
            if (! session.hasSong())
                return;

            const auto [n, d] = session.getTimeSigAt (session.getPosition());
            juce::PopupMenu menu;
            TempoLane::addTimeSigItems (menu, 1, n, d);
            juce::Component::SafePointer<TransportBar> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (signature.get()), [safe] (int result)
            {
                int num = 4, den = 4;

                if (safe != nullptr && TempoLane::timeSigForId (result, 1, num, den))
                    safe->session.setTimeSigAt (safe->session.getPosition(), num, den);
            });
        };

        signature->onWheel = [this] (int direction)
        {
            if (! session.hasSong())
                return;

            const auto [n, d] = session.getTimeSigAt (session.getPosition());
            session.setTimeSigAt (session.getPosition(), juce::jlimit (1, 32, n + direction), d);
        };

        key->onClick = [this]
        {
            if (! session.hasSong())
                return;

            const auto k = session.getKeyAt (session.getPosition());
            juce::PopupMenu menu;
            TempoLane::addKeyItems (menu, 1, k.root, k.minor);
            juce::Component::SafePointer<TransportBar> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (key.get()), [safe] (int result)
            {
                int root = -1;
                bool minor = false;

                if (safe != nullptr && TempoLane::keyForId (result, 1, root, minor))
                    safe->session.setKeyAt (safe->session.getPosition(), root, minor);
            });
        };

        key->onWheel = [this] (int direction)
        {
            if (! session.hasSong())
                return;

            const auto k = session.getKeyAt (session.getPosition());
            session.setKeyAt (session.getPosition(), k.root < 0 ? 0 : (k.root + direction + 12) % 12, k.minor);
        };
        meter = std::make_unique<LevelMeter> (session, -1, true);
        meter->onResetAll = [&commands] { commands.invokeDirectly (CommandIDs::resetMeters, true); };
        addAndMakeVisible (*position);
        addAndMakeVisible (*tempo);

        punchInButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        punchOutButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        for (auto* c : std::initializer_list<juce::Component*> { signature.get(), key.get(), meter.get() })
            addAndMakeVisible (c);

        session.addListener (this);
        refreshTransportButtons();
        startTimerHz (30);
    }

    TransportBar::~TransportBar()
    {
        session.removeListener (this);
    }

    void TransportBar::refreshTransportButtons()
    {
        const auto playing = session.isPlaying(), recording = session.isRecording();

        if (playing == wasPlaying && recording == wasRecording)
            return;

        wasPlaying = playing;
        wasRecording = recording;
        playButton.setIcon (playing ? Icon::stop : Icon::play);
        recordButton.setFillOverride (recording ? std::optional<juce::Colour> (Theme::record) : std::nullopt);
        recordButton.setIconColour (recording ? juce::Colours::white : Theme::recordIcon);
    }

    void TransportBar::timerCallback()
    {
        if (gridText != nullptr)
            if (const auto text = gridText(); text != gridButton.getButtonText())
            {
                gridButton.setButtonText (text);
                resized();
            }

        refreshTransportButtons();   // the transport can stop by itself (end of a count-in, a device change)
        position->refresh();
        tempo->refresh();

        // Time signature and key at the playhead: their width can change ("-" to "G minor")
        const bool sigWidth = signature->refresh();
        const bool keyWidth = key->refresh();

        if (sigWidth || keyWidth)
        {
            resized();
            repaint();   // the separators move too
        }
    }

    //==============================================================================
    void TransportBar::paint (juce::Graphics& g)
    {
        // Slightly lifted above the work area: a touch lighter at the top, a hairline below
        g.setGradientFill (juce::ColourGradient::vertical (Theme::panel.brighter (0.035f), 0.0f, Theme::panel, (float) getHeight()));
        g.fillAll();
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        g.setColour (Theme::hairline.brighter (0.1f));

        for (auto x : separatorXs)
            g.fillRect (x, getHeight() / 2 - 11, 1, 22);
    }

    void TransportBar::paintOverChildren (juce::Graphics& g)
    {
        Planned::paintChips (g, *this);
    }

    void TransportBar::resized()
    {
        const auto h = Theme::transportButtonHeight;
        auto r = getLocalBounds().reduced (10, 0);
        const auto fit = [h] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), h); };
        const auto lcdFit = [] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), 32); };

        // Decide what fits, least important first: sync, punch, key, signature (both also in the tempo lane),
        // then the button labels, then a narrower main meter, and the main meter last of all
        std::vector<juce::Component*> optional { &syncButton, &punchInButton, &selectToolButton, key.get(), signature.get() };
        int meterWidth = 200;
        meter->setVisible (true);
        const auto labelled = { &clickButton, &countInButton, &snapButton };

        for (auto* c : optional)    c->setVisible (true);
        for (auto* b : labelled)    b->setShowText (true);

        if (! UiMode::isPro())   // Easy view: punch and MIDI sync keep their shortcuts (P, Ctrl+Shift+Y)
        {
            syncButton.setVisible (false);
            punchInButton.setVisible (false);
        }


        // Exactly what the layout below uses, for what's visible now
        const auto needed = [&]
        {
            int total = 4 * (Theme::iconButtonWidth + 4) + (Theme::groupGap - 4) + Theme::positionDisplayWidth + 8 + Theme::bpmDisplayWidth + 8;

            for (auto* lcd : { signature.get(), key.get() })
                if (lcd->isVisible())
                    total += lcd->getIdealWidth() + 8;

            total += Theme::groupGap + clickButton.getIdealWidth (h) + 6 + countInButton.getIdealWidth (h) + 6 + Theme::iconButtonWidth + 6;

            if (punchInButton.isVisible())
                total += 4 + punchInButton.getIdealWidth (h) + punchOutButton.getIdealWidth (h);

            total += Theme::groupGap + snapButton.getIdealWidth (h) + 4 + gridButton.getIdealWidth (h) + 6;

            if (selectToolButton.isVisible())
                total += 3 * (Theme::iconButtonWidth + 2) + 6;

            total += 2 * (Theme::iconButtonWidth + 4);

            if (meter->isVisible())
                total += meterWidth + Theme::groupGap;

            if (syncButton.isVisible())
                total += syncButton.getIdealWidth (h) + 4;

            return total;
        };

        for (auto* c : optional)
        {
            if (needed() > r.getWidth() && meterWidth > 180)
                meterWidth = 180;   // a narrower meter (still labelled) before any control goes

            if (needed() > r.getWidth())
                c->setVisible (false);
        }

        punchOutButton.setVisible (punchInButton.isVisible());
        splitToolButton.setVisible (selectToolButton.isVisible());
        eraseToolButton.setVisible (selectToolButton.isVisible());

        if (needed() > r.getWidth())
            for (auto* b : labelled)
                b->setShowText (false);

        if (needed() > r.getWidth())
            meterWidth = 150;   // the loudness number without its label

        if (needed() > r.getWidth())
            meter->setVisible (false);

        separatorXs.clearQuick();

        // Transport buttons
        for (auto* b : { &returnButton, &playButton, &recordButton, &loopButton })
        {
            b->setBounds (fit (r.removeFromLeft (Theme::iconButtonWidth)));
            r.removeFromLeft (4);
        }

        r.removeFromLeft (Theme::groupGap - 4);
        position->setBounds (lcdFit (r.removeFromLeft (Theme::positionDisplayWidth)));
        r.removeFromLeft (8);
        tempo->setBounds (lcdFit (r.removeFromLeft (Theme::bpmDisplayWidth)));
        r.removeFromLeft (8);

        for (auto* lcd : { signature.get(), key.get() })
        {
            if (! lcd->isVisible())
                continue;

            lcd->setBounds (lcdFit (r.removeFromLeft (lcd->getIdealWidth())));
            r.removeFromLeft (8);
        }

        separatorXs.add (r.getX() + Theme::groupGap / 2 - 4);
        r.removeFromLeft (Theme::groupGap);

        // Recording helpers
        for (auto* b : { &clickButton, &countInButton })
        {
            b->setBounds (fit (r.removeFromLeft (b->getIdealWidth (h))));
            r.removeFromLeft (6);
        }

        lowLatencyButton.setBounds (fit (r.removeFromLeft (Theme::iconButtonWidth)));
        r.removeFromLeft (6);

        if (punchInButton.isVisible())
        {
            r.removeFromLeft (4);
            punchInButton.setBounds (fit (r.removeFromLeft (punchInButton.getIdealWidth (h))));
            punchOutButton.setBounds (fit (r.removeFromLeft (punchOutButton.getIdealWidth (h))));
        }

        separatorXs.add (r.getX() + Theme::groupGap / 2);
        r.removeFromLeft (Theme::groupGap);

        // Editing: snap and its grid, the tools, undo and redo
        snapButton.setBounds (fit (r.removeFromLeft (snapButton.getIdealWidth (h))));
        r.removeFromLeft (4);
        gridButton.setBounds (fit (r.removeFromLeft (gridButton.getIdealWidth (h))));
        r.removeFromLeft (6);

        if (selectToolButton.isVisible())
        {
            for (auto* b : { &selectToolButton, &splitToolButton, &eraseToolButton })
            {
                b->setBounds (fit (r.removeFromLeft (Theme::iconButtonWidth)));
                r.removeFromLeft (2);
            }

            r.removeFromLeft (6);
        }

        for (auto* b : { &undoButton, &redoButton })
        {
            b->setBounds (fit (r.removeFromLeft (Theme::iconButtonWidth)));
            r.removeFromLeft (4);
        }

        // Right side, from the edge inwards
        if (meter->isVisible())
        {
            meter->setBounds (lcdFit (r.removeFromRight (meterWidth)).withHeight (h).withY ((getHeight() - h) / 2));
            r.removeFromRight (Theme::groupGap);
        }

        if (syncButton.isVisible())
            syncButton.setBounds (fit (r.removeFromRight (syncButton.getIdealWidth (h))));
    }
}
