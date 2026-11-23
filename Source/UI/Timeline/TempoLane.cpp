/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "TempoLane.h"
#include "../Theme.h"
#include "../Widgets.h"

namespace ez
{
    namespace
    {
        // Quarter-note bottoms only for now (see Session::isValidTimeSig)
        const std::pair<int, int> commonTimeSigs[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 } };
        const juce::Colour tempoLine { 0xff7fe0a8 };

        juce::String bpmText (double bpm)
        {
            return std::abs (bpm - std::round (bpm)) < 0.005 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 2);
        }
    }

    //==============================================================================
    void TempoLane::addTimeSigItems (juce::PopupMenu& menu, int firstId, int currentNumerator, int currentDenominator)
    {
        for (int i = 0; i < (int) std::size (commonTimeSigs); ++i)
        {
            const auto [n, d] = commonTimeSigs[i];
            menu.addItem (firstId + i, juce::String (n) + "/" + juce::String (d), true, n == currentNumerator && d == currentDenominator);
        }

        juce::PopupMenu more;

        for (int n = 1; n <= 16; ++n)
            for (int d : { 4 })
                if (std::find (std::begin (commonTimeSigs), std::end (commonTimeSigs), std::make_pair (n, d)) == std::end (commonTimeSigs))
                    more.addItem (firstId + 100 + n * 32 + d, juce::String (n) + "/" + juce::String (d), true, n == currentNumerator && d == currentDenominator);

        menu.addSubMenu ("More", more);
    }

    bool TempoLane::timeSigForId (int id, int firstId, int& numerator, int& denominator)
    {
        const auto offset = id - firstId;

        if (juce::isPositiveAndBelow (offset, (int) std::size (commonTimeSigs)))
        {
            std::tie (numerator, denominator) = commonTimeSigs[offset];
            return true;
        }

        if (offset >= 100 + 32 && offset < 100 + 17 * 32)
        {
            numerator = (offset - 100) / 32;
            denominator = (offset - 100) % 32;
            return Session::isValidTimeSig (numerator, denominator);
        }

        return false;
    }

    void TempoLane::addKeyItems (juce::PopupMenu& menu, int firstId, int currentRoot, bool currentMinor)
    {
        menu.addItem (firstId, "No key", true, currentRoot < 0);
        juce::PopupMenu major, minor;

        for (int root = 0; root < 12; ++root)
        {
            major.addItem (firstId + 1 + root, Session::describeKey (root, false), true, currentRoot == root && ! currentMinor);
            minor.addItem (firstId + 13 + root, Session::describeKey (root, true), true, currentRoot == root && currentMinor);
        }

        menu.addSubMenu ("Major", major);
        menu.addSubMenu ("Minor", minor);
    }

    bool TempoLane::keyForId (int id, int firstId, int& root, bool& minor)
    {
        const auto offset = id - firstId;

        if (offset == 0)                            { root = -1; minor = false; return true; }
        if (offset >= 1 && offset <= 12)            { root = offset - 1; minor = false; return true; }
        if (offset >= 13 && offset <= 24)           { root = offset - 13; minor = true; return true; }

        return false;
    }

    //==============================================================================
    TempoLane::TempoLane (Session& s, TimelineViewState& v) : session (s), view (v)
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);
        bpmEditor.setInputRestrictions (6, "0123456789.");
        bpmEditor.setJustification (juce::Justification::centredLeft);
        bpmEditor.setFont (Theme::font (Theme::smallFontSize, true));
        bpmEditor.onReturnKey = [this] { commitTypedBpm(); };
        bpmEditor.onEscapeKey = [this] { editingTempo = -1; typingAfterAdd = false; bpmEditor.setVisible (false); };
        bpmEditor.onFocusLost = [this] { commitTypedBpm(); };
        addChildComponent (bpmEditor);
    }

    int TempoLane::xForBeat (double beat) const
    {
        auto* edit = session.getEdit();
        return edit != nullptr ? view.timeToX (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat))) : 0;
    }

    double TempoLane::beatAtX (int x) const
    {
        auto* edit = session.getEdit();
        return edit != nullptr ? juce::jmax (0.0, edit->tempoSequence.toBeats (view.xToTime (x)).inBeats()) : 0.0;
    }

    int TempoLane::barNear (double beat) const
    {
        const auto [bar, inBar] = session.getBarAt (beat);
        const auto barLength = session.getBarStartBeat (bar + 1) - session.getBarStartBeat (bar);
        return inBar > barLength * 0.5 ? bar + 1 : bar;
    }

    double TempoLane::snapToBar (double beat) const
    {
        return session.getBarStartBeat (barNear (beat));
    }

    void TempoLane::rebuildItems()
    {
        items.clear();

        if (session.getEdit() == nullptr)
            return;

        // Labels at the same beat share one flag: "92 BPM  4/4  G minor"
        const auto font = Theme::font (Theme::captionFontSize + 0.5f, true);
        std::map<long long, int> rightEdgeAt;   // beat (in 1/1000) -> where the text so far ends

        const auto add = [&] (Kind kind, int index, double beat, const juce::String& text)
        {
            const auto key = (long long) std::llround (beat * 1000.0);
            const auto x0 = xForBeat (beat);
            const auto start = rightEdgeAt.count (key) != 0 ? rightEdgeAt[key] + 6 : x0 + 2;
            const auto width = (kind == Kind::tempo ? 12 : 0) + juce::GlyphArrangement::getStringWidthInt (font, text) + 4;
            items.push_back ({ kind, index, beat, { start, 1, width, getHeight() - 2 }, text });
            rightEdgeAt[key] = start + width;
        };

        const auto tempos = session.getTempoMarkers();

        for (int i = 0; i < (int) tempos.size(); ++i)
            add (Kind::tempo, i, tempos[(size_t) i].beat, bpmText (tempos[(size_t) i].bpm) + " BPM" + (tempos[(size_t) i].ramp && i + 1 < (int) tempos.size() ? juce::String (" (glide)") : juce::String()));

        const auto sigs = session.getTimeSigMarkers();

        for (int i = 0; i < (int) sigs.size(); ++i)
            add (Kind::timeSig, i, sigs[(size_t) i].beat, juce::String (sigs[(size_t) i].numerator) + "/" + juce::String (sigs[(size_t) i].denominator));

        const auto keys = session.getKeyMarkers();

        for (int i = 0; i < (int) keys.size(); ++i)
            if (i > 0 || keys[(size_t) i].root >= 0)   // "No key" at the start says nothing
                add (Kind::key, i, keys[(size_t) i].beat, Session::describeKey (keys[(size_t) i].root, keys[(size_t) i].minor));
    }

    const TempoLane::Item* TempoLane::itemAt (juce::Point<int> p) const
    {
        for (auto it = items.rbegin(); it != items.rend(); ++it)
            if (it->bounds.contains (p))
                return &*it;

        return nullptr;
    }

    //==============================================================================
    void TempoLane::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::bar);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        auto* edit = session.getEdit();

        if (edit == nullptr)
            return;

        rebuildItems();

        // The tempo line: low tempos near the bottom, high near the top of the lane
        const auto tempos = session.getTempoMarkers();
        auto lo = 1.0e9, hi = 0.0;

        for (auto& t : tempos)
        {
            lo = juce::jmin (lo, t.bpm);
            hi = juce::jmax (hi, t.bpm);
        }

        lo -= 10.0;
        hi += 10.0;
        const auto yFor = [&] (double bpm) { return (float) getHeight() - 4.0f - (float) ((bpm - lo) / (hi - lo)) * ((float) getHeight() - 10.0f); };

        juce::Path line;
        const auto area = g.getClipBounds();   // during playback only thin strips repaint
        const auto fromX = juce::jmax (0, area.getX() - 3), toX = juce::jmin (getWidth(), area.getRight() + 3);

        for (int x = fromX; x <= toX; x += 3)
        {
            const auto y = yFor (session.getBpmAt (view.xToTime (x)));

            if (x == fromX)  line.startNewSubPath ((float) x, y);
            else             line.lineTo ((float) x, y);
        }

        g.setColour (tempoLine.withAlpha (0.8f));
        g.strokePath (line, juce::PathStrokeType (1.5f));

        // Flags and values
        const auto font = Theme::font (Theme::captionFontSize + 0.5f, true);
        g.setFont (font);

        for (auto& item : items)
        {
            if (item.bounds.getRight() < 0 || item.bounds.getX() > getWidth())
                continue;

            const bool isDragged = dragging.has_value() && dragging->kind == item.kind && dragging->index == item.index && dragMoved;
            auto r = item.bounds.translated (isDragged ? dragX - xForBeat (item.beat) : 0, 0);

            if (item.kind == Kind::tempo)
            {
                g.setColour (juce::Colours::white);
                g.drawVerticalLine (r.getX(), 3.0f, (float) getHeight() - 3.0f);
                Icons::draw (g, Icon::flag, r.removeFromLeft (12).withSizeKeepingCentre (10, 10).toFloat(), juce::Colours::white, 1.5f);
            }

            // A chip behind the value, so the tempo line never runs through the text
            const auto textWidth = (int) juce::GlyphArrangement::getStringWidth (font, item.text) + 8;
            g.setColour (Theme::bar.withAlpha (0.92f));
            g.fillRoundedRectangle (r.withWidth (juce::jmin (r.getWidth(), textWidth)).reduced (0, 3).toFloat(), 3.0f);

            g.setColour (isDragged ? Theme::selection : item.kind == Kind::key ? juce::Colour (0xffd8c7ff) : item.kind == Kind::timeSig ? juce::Colour (0xffbfe3ff) : juce::Colours::white);
            g.drawText (item.text, r.withTrimmedLeft (2), juce::Justification::centredLeft, false);
        }
    }

    void TempoLane::resized()
    {
        bpmEditor.setBounds (bpmEditor.getBounds().withHeight (getHeight() - 2).withY (1));
    }

    //==============================================================================
    void TempoLane::report (bool ok, const juce::String& title)
    {
        if (! ok && onProblem)
            onProblem (title, session.getLastError());

        repaint();
    }

    void TempoLane::mouseMove (const juce::MouseEvent& e)
    {
        rebuildItems();
        const auto* item = itemAt (e.getPosition());

        if (item == nullptr)
        {
            setTooltip ("Tempo, time signature and key. Double-click to add a tempo change; right-click for more.");
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        const auto what = item->kind == Kind::tempo ? juce::String ("Tempo") : item->kind == Kind::timeSig ? juce::String ("Time signature") : juce::String ("Key");
        setTooltip (what + (item->index == 0 ? " at the start of the song" : " change at bar " + juce::String (session.getBarAt (item->beat).first + 1))
                    + ". Double-click to change" + (item->index > 0 ? ", drag to move" : juce::String())
                    + (item->kind == Kind::tempo ? ", scroll for +/- 1 BPM" : juce::String (", scroll to step")) + ", right-click for more.");
        setMouseCursor (item->index > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    }

    void TempoLane::mouseDown (const juce::MouseEvent& e)
    {
        rebuildItems();
        dragging.reset();
        dragMoved = false;
        const auto* item = itemAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (item != nullptr)   showItemMenu (*item);
            else                   showEmptyMenu (beatAtX (e.x), e.x);

            return;
        }

        if (item != nullptr)
        {
            dragging = *item;
            dragBeat = item->beat;
            dragX = xForBeat (item->beat);
        }
    }

    void TempoLane::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging.has_value() || dragging->index == 0 || e.getDistanceFromDragStart() < 3)
            return;

        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::escapeKey))   // Esc cancels (the lane never has the keyboard)
        {
            dragging.reset();
            dragMoved = false;
            repaint();
            return;
        }

        dragMoved = true;
        const auto beat = beatAtX (e.x);

        if (dragging->kind == Kind::timeSig)
        {
            // Bars as they will be after the move: the previous signature's bars
            const auto sigs = session.getTimeSigMarkers();
            const auto& prev = sigs[(size_t) juce::jlimit (0, (int) sigs.size() - 1, dragging->index - 1)];
            const auto barLength = prev.numerator * 4.0 / prev.denominator;
            dragBeat = prev.beat + juce::jmax (1.0, std::round ((beat - prev.beat) / barLength)) * barLength;
        }
        else
        {
            dragBeat = e.mods.isAltDown() ? std::round (beat) : snapToBar (beat);
        }

        dragX = xForBeat (dragBeat);
        repaint();
    }

    void TempoLane::mouseUp (const juce::MouseEvent& e)
    {
        if (! dragging.has_value())
        {
            if (! e.mods.isPopupMenu() && e.getNumberOfClicks() == 1 && e.mouseWasClicked())
            {
                // A plain click on empty lane moves the playhead (snapped to the beat)
                if (auto* edit = session.getEdit())
                    session.setPosition (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (std::round (beatAtX (e.x)))));
            }

            return;
        }

        const auto item = *std::exchange (dragging, std::nullopt);

        if (! std::exchange (dragMoved, false))
            return;

        if (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::escapeKey))
        {
            repaint();
            return;
        }

        if (item.kind == Kind::tempo)         report (session.moveTempoChange (item.index, dragBeat), "Could not move the tempo change");
        else if (item.kind == Kind::key)      report (session.moveKeyChange (item.index, dragBeat), "Could not move the key change");
        else
        {
            const auto sigs = session.getTimeSigMarkers();
            const auto& prev = sigs[(size_t) juce::jlimit (0, (int) sigs.size() - 1, item.index - 1)];
            const auto bars = juce::roundToInt ((dragBeat - prev.beat) / (prev.numerator * 4.0 / prev.denominator));
            report (session.moveTimeSigChange (item.index, prev.bar + bars), "Could not move the time signature change");
        }
    }

    bool TempoLane::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey && dragging.has_value())
        {
            dragging.reset();
            dragMoved = false;
            repaint();
            return true;
        }

        return false;
    }

    void TempoLane::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;

        rebuildItems();
        dragging.reset();
        dragMoved = false;

        if (const auto* item = itemAt (e.getPosition()))
        {
            if (item->kind == Kind::tempo)
                startTypingBpm (item->index);
            else
                showItemMenu (*item);

            return;
        }

        // A new tempo change at the nearest bar, then type its value
        const auto beat = e.mods.isAltDown() ? std::round (beatAtX (e.x)) : snapToBar (beatAtX (e.x));
        auto* edit = session.getEdit();

        if (edit == nullptr)
            return;

        const auto bpm = session.getBpmAt (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));

        if (! session.addTempoChange (beat, bpm))
        {
            report (false, "Could not add a tempo change");
            return;
        }

        const auto tempos = session.getTempoMarkers();

        for (int i = 0; i < (int) tempos.size(); ++i)
            if (std::abs (tempos[(size_t) i].beat - beat) < 1.0e-6)
            {
                startTypingBpm (i);
                typingAfterAdd = true;
            }
    }

    void TempoLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        rebuildItems();
        const auto* item = itemAt (e.getPosition());
        const auto amount = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;

        if (item == nullptr || amount == 0.0f)
        {
            if (onWheelElsewhere)
                onWheelElsewhere (e, wheel);

            return;
        }

        const auto direction = amount > 0 ? 1 : -1;

        if (item->kind == Kind::tempo)
        {
            const auto tempos = session.getTempoMarkers();

            if (juce::isPositiveAndBelow (item->index, (int) tempos.size()))
                report (session.setTempoChangeBpm (item->index, tempos[(size_t) item->index].bpm + direction * (e.mods.isShiftDown() ? 0.1 : 1.0)), "Could not change the tempo");
        }
        else if (item->kind == Kind::key)
        {
            const auto keys = session.getKeyMarkers();

            if (juce::isPositiveAndBelow (item->index, (int) keys.size()))
            {
                const auto& k = keys[(size_t) item->index];
                const auto root = k.root < 0 ? 0 : (k.root + direction + 12) % 12;
                auto* edit = session.getEdit();

                if (edit != nullptr)
                    report (session.setKeyAt (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (k.beat + 1.0e-6)), root, k.minor), "Could not change the key");
            }
        }
        else
        {
            const auto sigs = session.getTimeSigMarkers();
            auto* edit = session.getEdit();

            if (edit != nullptr && juce::isPositiveAndBelow (item->index, (int) sigs.size()))
            {
                const auto& sig = sigs[(size_t) item->index];
                report (session.setTimeSigAt (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (sig.beat + 1.0e-6)),
                                              juce::jlimit (1, 32, sig.numerator + direction), sig.denominator), "Could not change the time signature");
            }
        }
    }

    //==============================================================================
    void TempoLane::startTypingBpm (int tempoIndex)
    {
        const auto tempos = session.getTempoMarkers();

        if (! juce::isPositiveAndBelow (tempoIndex, (int) tempos.size()))
            return;

        rebuildItems();
        editingTempo = tempoIndex;
        typingAfterAdd = false;
        const auto x = xForBeat (tempos[(size_t) tempoIndex].beat);
        bpmEditor.setBounds (juce::jlimit (0, juce::jmax (0, getWidth() - 70), x + 12), 1, 64, getHeight() - 2);
        bpmEditor.setText (bpmText (tempos[(size_t) tempoIndex].bpm), false);
        bpmEditor.setVisible (true);
        bpmEditor.grabKeyboardFocus();
        bpmEditor.selectAll();
    }

    void TempoLane::commitTypedBpm()
    {
        if (! bpmEditor.isVisible())
            return;

        bpmEditor.setVisible (false);   // first: committing can run again from onFocusLost
        const auto index = std::exchange (editingTempo, -1);
        const auto typed = bpmEditor.getText().getDoubleValue();

        if (index >= 0 && typed > 0.0)
            report (session.setTempoChangeBpm (index, typed, std::exchange (typingAfterAdd, false)), "Could not change the tempo");   // joins its add

        typingAfterAdd = false;

        // Give the keyboard back so Space and the shortcuts work again
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (p->getWantsKeyboardFocus())
            {
                p->grabKeyboardFocus();
                break;
            }
    }

    //==============================================================================
    void TempoLane::showEmptyMenu (double beat, int x)
    {
        auto* edit = session.getEdit();

        if (edit == nullptr)
            return;

        const auto bar = barNear (beat);
        const auto barBeat = session.getBarStartBeat (bar);
        const auto time = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (barBeat));
        const auto [num, den] = session.getTimeSigAt (time);
        const auto key = session.getKeyAt (time);

        juce::PopupMenu menu, sigMenu, keyMenu;
        menu.addSectionHeader ("Bar " + juce::String (bar + 1));
        menu.addItem (1, "Add a tempo change here", bar > 0);
        addTimeSigItems (sigMenu, 1000, num, den);
        menu.addSubMenu ("Add a time signature change here", sigMenu, bar > 0);
        addKeyItems (keyMenu, 3000, key.root, key.minor);
        menu.addSubMenu ("Add a key change here", keyMenu, bar > 0);
        menu.addSeparator();
        menu.addItem (2, "Move the playhead here");
        juce::ignoreUnused (x);

        juce::Component::SafePointer<TempoLane> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, bar, barBeat, time] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& s = safe->session;
            int n = 4, d = 4, root = -1;
            bool minor = false;

            if (result == 1)
            {
                if (s.addTempoChange (barBeat, s.getBpmAt (time)))
                {
                    const auto tempos = s.getTempoMarkers();

                    for (int i = 0; i < (int) tempos.size(); ++i)
                        if (std::abs (tempos[(size_t) i].beat - barBeat) < 1.0e-6)
                        {
                            safe->startTypingBpm (i);
                            safe->typingAfterAdd = true;
                        }
                }
                else
                {
                    safe->report (false, "Could not add a tempo change");
                }
            }
            else if (result == 2)                               s.setPosition (time);
            else if (timeSigForId (result, 1000, n, d))         safe->report (s.addTimeSigChange (bar, n, d), "Could not add a time signature change");
            else if (keyForId (result, 3000, root, minor))      safe->report (s.addKeyChange (barBeat, root, minor), "Could not add a key change");

            safe->repaint();
        });
    }

    void TempoLane::showItemMenu (const Item& item)
    {
        auto* edit = session.getEdit();

        if (edit == nullptr)
            return;

        const auto time = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (item.beat + 1.0e-6));
        juce::PopupMenu menu, sub;
        const auto where = item.index == 0 ? juce::String ("At the start") : "Bar " + juce::String (session.getBarAt (item.beat).first + 1);

        if (item.kind == Kind::tempo)
        {
            const auto tempos = session.getTempoMarkers();

            if (! juce::isPositiveAndBelow (item.index, (int) tempos.size()))
                return;

            menu.addSectionHeader (where + ": " + bpmText (tempos[(size_t) item.index].bpm) + " BPM");
            menu.addItem (1, "Type a tempo...");
            menu.addItem (2, "Glide to the next tempo", item.index + 1 < (int) tempos.size(), tempos[(size_t) item.index].ramp);
        }
        else if (item.kind == Kind::timeSig)
        {
            const auto [n, d] = session.getTimeSigAt (time);
            menu.addSectionHeader (where + ": " + juce::String (n) + "/" + juce::String (d));
            addTimeSigItems (sub, 1000, n, d);
            menu.addSubMenu ("Change to", sub);
        }
        else
        {
            const auto key = session.getKeyAt (time);
            menu.addSectionHeader (where + ": " + Session::describeKey (key.root, key.minor));
            addKeyItems (sub, 3000, key.root, key.minor);
            menu.addSubMenu ("Change to", sub);
        }

        menu.addSeparator();
        menu.addItem (3, "Delete", item.index > 0);
        menu.addItem (4, "Move the playhead here");

        juce::Component::SafePointer<TempoLane> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, item, time] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& s = safe->session;
            int n = 4, d = 4, root = -1;
            bool minor = false;

            if (result == 1)
                safe->startTypingBpm (item.index);
            else if (result == 2)
            {
                const auto tempos = s.getTempoMarkers();

                if (juce::isPositiveAndBelow (item.index, (int) tempos.size()))
                    safe->report (s.setTempoRamp (item.index, ! tempos[(size_t) item.index].ramp), "Could not change the tempo");
            }
            else if (result == 3)
                safe->report (item.kind == Kind::tempo   ? s.deleteTempoChange (item.index)
                            : item.kind == Kind::timeSig ? s.deleteTimeSigChange (item.index)
                                                         : s.deleteKeyChange (item.index), "Could not delete it");
            else if (result == 4)
                s.setPosition (time);
            else if (timeSigForId (result, 1000, n, d))
                safe->report (s.setTimeSigAt (time, n, d), "Could not change the time signature");
            else if (keyForId (result, 3000, root, minor))
                safe->report (s.setKeyAt (time, root, minor), "Could not change the key");

            safe->repaint();
        });
    }
}
