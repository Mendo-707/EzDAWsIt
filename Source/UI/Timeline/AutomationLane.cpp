/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "AutomationLane.h"
#include "../Theme.h"

namespace ez
{
    namespace
    {
        // Copied points: times relative to the first, values 0..1 (so they paste into any lane)
        std::vector<Session::AutoPoint>& clipboard()
        {
            static std::vector<Session::AutoPoint> points;
            return points;
        }

        juce::String secondsText (double s)
        {
            return juce::String (s, 2) + " s";
        }
    }

    //==============================================================================
    void AutomationMenus::addParamItems (juce::PopupMenu& menu, Session& session, int trackIndex, int base, const Session::ParamRef& current)
    {
        const auto params = session.getAutomatableParams (trackIndex);
        juce::String group;
        juce::PopupMenu sub;

        const auto itemFor = [&] (size_t i)
        {
            const auto& p = params[i];
            return juce::PopupMenu::Item (p.name + (session.isAutomated (trackIndex, p.ref) ? "  (automated)" : ""))
                       .setID (base + (int) i).setTicked (p.ref == current);
        };

        const auto flush = [&]
        {
            if (sub.getNumItems() > 0)
                menu.addSubMenu (group, sub);

            sub = {};
        };

        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto& p = params[i];

            if (p.group == "Track" || p.group == "Sends")
            {
                menu.addItem (itemFor (i));
                continue;
            }

            if (p.group != group)
            {
                flush();
                group = p.group;
            }

            sub.addItem (itemFor (i));
        }

        flush();
    }

    void AutomationMenus::showLaneMenu (Session& session, int trackIndex, int laneIndex, const Session::ParamRef& param,
                                        std::function<void (const juce::String&)> onProblem, juce::PopupMenu menu,
                                        std::function<void (int)> onExtraItem)
    {
        enum { clearId = 9001, hideId, hideAllId, changeBase = 10000, addBase = 20000 };   // above the extra items' IDs

        juce::PopupMenu change, add;
        addParamItems (change, session, trackIndex, changeBase, param);
        addParamItems (add, session, trackIndex, addBase, {});

        if (menu.getNumItems() > 0)
            menu.addSeparator();

        menu.addSectionHeader (session.getParamName (trackIndex, param));
        menu.addSubMenu ("Change control", change);
        menu.addSubMenu ("Add a lane", add);
        menu.addItem (clearId, "Clear this lane (every point)", session.isAutomated (trackIndex, param));
        menu.addItem (hideId, "Hide this lane (points stay)");
        menu.addItem (hideAllId, "Hide this track's lanes (A)");

        const auto params = session.getAutomatableParams (trackIndex);
        const auto ids = session.getTrackIDs();
        const auto trackID = juce::isPositiveAndBelow (trackIndex, (int) ids.size()) ? ids[(size_t) trackIndex] : te::EditItemID();
        const auto* editWhenShown = session.getEdit();

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [&session, trackIndex, trackID, editWhenShown, laneIndex, param, params, onProblem, onExtraItem] (int result)
        {
            // Tracks may have moved (or the song changed) while the menu was open: act on the same track or not at all
            const auto idsNow = session.getTrackIDs();

            if (result <= 0 || session.getEdit() != editWhenShown || ! juce::isPositiveAndBelow (trackIndex, (int) idsNow.size())
                 || idsNow[(size_t) trackIndex] != trackID)
                return;

            bool ok = true;

            if (result >= addBase && juce::isPositiveAndBelow (result - addBase, (int) params.size()))
                ok = session.setAutomationLaneShown (trackIndex, params[(size_t) (result - addBase)].ref, true);
            else if (result >= changeBase && juce::isPositiveAndBelow (result - changeBase, (int) params.size()))
                ok = session.replaceAutomationLane (trackIndex, laneIndex, params[(size_t) (result - changeBase)].ref);
            else if (result == clearId)
                ok = session.clearAutomation (trackIndex, param);
            else if (result == hideId)
                ok = session.setAutomationLaneShown (trackIndex, param, false);
            else if (result == hideAllId)
                session.toggleAutomationLanes (trackIndex);
            else if (onExtraItem)
                onExtraItem (result);

            if (! ok && onProblem)
                onProblem (session.getLastError());
        });
    }

    //==============================================================================
    AutomationLane::AutomationLane (Session& s, TimelineViewState& v, int track, int lane, Session::ParamRef ref)
        : session (s), view (v), trackIndex (track), laneIndex (lane), param (std::move (ref))
    {
        setOpaque (true);
        reload();
    }

    juce::String AutomationLane::describePoints() const
    {
        juce::String s;

        for (const auto& p : session.getAutomationPoints (trackIndex, param))
            s << p.seconds << "," << p.value << "," << p.curve << ";";

        if (s.isEmpty())
            s << session.getParamValue (trackIndex, param);   // an empty lane's dashed line follows the control

        return s;
    }

    void AutomationLane::reload()
    {
        const auto before = points.size();
        points = session.getAutomationPoints (trackIndex, param);

        if (points.size() != before)
            selected.clear();   // indexes changed under us (undo, recording)

        for (auto it = selected.begin(); it != selected.end();)
            it = *it >= (int) points.size() ? selected.erase (it) : std::next (it);

        drawnFor = describePoints();
    }

    void AutomationLane::refreshIfChanged()
    {
        if (drag == Drag::none && describePoints() != drawnFor)
        {
            reload();
            repaint();
        }

        if (hasEditFocus() != focusShown)
            repaint();   // the "keys go here" mark
    }

    float AutomationLane::valueToY (float value) const
    {
        return (float) pad + (1.0f - juce::jlimit (0.0f, 1.0f, value)) * (float) juce::jmax (1, getHeight() - 2 * pad);
    }

    float AutomationLane::yToValue (float y) const
    {
        return juce::jlimit (0.0f, 1.0f, 1.0f - (y - (float) pad) / (float) juce::jmax (1, getHeight() - 2 * pad));
    }

    juce::Point<float> AutomationLane::pointPos (int i) const
    {
        const auto& p = points[(size_t) i];
        return { (float) view.timeToX (te::TimePosition::fromSeconds (p.seconds)), valueToY (p.value) };
    }

    int AutomationLane::pointAt (juce::Point<int> pos) const
    {
        int best = -1;
        auto bestDistance = (float) hitRadius;

        for (int i = 0; i < (int) points.size(); ++i)
            if (const auto d = pointPos (i).getDistanceFrom (pos.toFloat()); d <= bestDistance)
            {
                best = i;
                bestDistance = d;
            }

        return best;
    }

    double AutomationLane::secondsAt (int x) const
    {
        return view.xToTime (x).inSeconds();
    }

    double AutomationLane::snapSeconds (double seconds, bool snap) const
    {
        const auto t = te::TimePosition::fromSeconds (juce::jmax (0.0, seconds));
        return snap && view.snapEnabled ? session.snapToGrid (t, view.getGridBeats (session.getBpm())).inSeconds() : t.inSeconds();
    }

    int AutomationLane::segmentAt (double seconds) const
    {
        // The point the line at this time starts from (-1 before the first, or past the last)
        for (int i = (int) points.size() - 1; i >= 0; --i)
            if (points[(size_t) i].seconds <= seconds)
                return i < (int) points.size() - 1 ? i : -1;

        return -1;
    }

    void AutomationLane::report (bool ok)
    {
        if (! ok && onProblem)
            onProblem (session.getLastError());
    }

    //==============================================================================
    void AutomationLane::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::laneBackground.darker (0.25f));
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        // Guides at the top, middle and bottom of the control's range
        g.setColour (Theme::gridBeat);

        for (auto v : { 0.0f, 0.5f, 1.0f })
            g.drawHorizontalLine (juce::roundToInt (valueToY (v)), 0.0f, (float) getWidth());

        const auto colour = session.getTrackColour (trackIndex).isTransparent() ? Theme::accent : session.getTrackColour (trackIndex);
        const auto clip = g.getClipBounds();

        if (points.empty())
        {
            // Not automated: a dashed line where the control is now
            const auto y = valueToY (session.getParamValue (trackIndex, param));
            const float dashes[] = { 4.0f, 4.0f };
            g.setColour (Theme::textDim.withAlpha (0.6f));
            g.drawDashedLine ({ (float) clip.getX(), y, (float) clip.getRight(), y }, dashes, 2, 1.0f);
            g.setFont (Theme::font (Theme::captionFontSize));
            g.drawText ("Double-click to add a point", getLocalBounds().reduced (8, 2), juce::Justification::topLeft, true);
        }
        else
        {
            juce::Path line, fill;
            const auto x0 = juce::jmax (0, clip.getX() - 2), x1 = clip.getRight() + 2;

            for (int x = x0; x <= x1; x += 3)
            {
                const auto y = valueToY (session.getAutomationValueAt (trackIndex, param, secondsAt (x)));

                if (x == x0)
                {
                    line.startNewSubPath ((float) x, y);
                    fill.startNewSubPath ((float) x, (float) getHeight());
                }

                line.lineTo ((float) x, y);
                fill.lineTo ((float) x, y);
            }

            fill.lineTo ((float) x1, (float) getHeight());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.14f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (line, juce::PathStrokeType (1.6f));

            for (int i = 0; i < (int) points.size(); ++i)
            {
                const auto p = pointPos (i);

                if (p.x < (float) clip.getX() - 8.0f || p.x > (float) clip.getRight() + 8.0f)
                    continue;

                const bool isSelected = selected.count (i) > 0;
                const auto r = isSelected ? 4.5f : (i == hover ? 4.5f : 3.5f);
                g.setColour (isSelected ? Theme::selection : Theme::laneBackground);
                g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
                g.setColour (isSelected ? Theme::selection : colour);
                g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.4f);
            }
        }

        if (! box.isEmpty())
        {
            g.setColour (Theme::selection.withAlpha (0.12f));
            g.fillRect (box);
            g.setColour (Theme::selection);
            g.drawRect (box);
        }

        focusShown = hasEditFocus();

        if (focusShown)
        {
            g.setColour (Theme::accent.withAlpha (0.5f));
            g.fillRect (0, 0, 2, getHeight());   // keys go here
        }

        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());   // the opaque lane covers the track separator
    }

    //==============================================================================
    void AutomationLane::mouseDown (const juce::MouseEvent& e)
    {
        view.focusedTrack = trackIndex;
        moved = false;
        gestureStarted = false;
        box = {};
        pointsAtMouseDown = points.size();
        session.selectClip (nullptr);   // keys (Delete, Ctrl+C...) now mean these points, not a clip
        const auto index = pointAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (index >= 0 && selected.count (index) == 0)
                selected = { index };

            repaint();
            showMenu (index, e.getPosition());
            return;
        }

        if (index >= 0)
        {
            if (e.mods.isShiftDown())
            {
                if (selected.erase (index) == 0)
                    selected.insert (index);
            }
            else if (selected.count (index) == 0)
            {
                selected = { index };
            }

            drag = selected.count (index) > 0 ? Drag::points : Drag::none;
            downIndex = index;
            dragStart.clear();

            for (auto i : selected)
                dragStart.push_back ({ i, points[(size_t) i] });
        }
        else if (e.mods.isCommandDown() && segmentAt (secondsAt (e.x)) >= 0)
        {
            drag = Drag::curve;
            curveIndex = segmentAt (secondsAt (e.x));
            curveStart = points[(size_t) curveIndex].curve;
        }
        else
        {
            drag = Drag::box;
            selectedBeforeBox = e.mods.isShiftDown() ? selected : std::set<int>();

            if (! e.mods.isShiftDown())
                selected.clear();
        }

        repaint();
    }

    void AutomationLane::moveSelected (double dt, float dv)
    {
        // In the direction of travel, so a point never meets a neighbour that is about to move too
        auto order = dragStart;

        if (dt > 0)
            std::reverse (order.begin(), order.end());

        for (const auto& [index, start] : order)
        {
            if (! session.moveAutomationPoint (trackIndex, param, index, start.seconds + dt, juce::jlimit (0.0f, 1.0f, start.value + dv), gestureStarted))
            {
                report (false);
                break;
            }

            gestureStarted = true;
        }
    }

    void AutomationLane::mouseDrag (const juce::MouseEvent& e)
    {
        if (! moved && e.getDistanceFromDragStart() < 3)
            return;

        moved = true;

        if (drag != Drag::none && drag != Drag::box && session.getAutomationPoints (trackIndex, param).size() != pointsAtMouseDown)
        {
            drag = Drag::none;   // an undo took points away mid-drag: indexes would point at the wrong ones
            reload();
            repaint();
            return;
        }

        if (drag == Drag::points && ! dragStart.empty())
        {
            Session::AutoPoint anchor;

            for (const auto& [i, start] : dragStart)
                if (i == downIndex)
                    anchor = start;

            // The point under the mouse lands on the grid; the others keep their distance from it
            const auto raw = anchor.seconds + secondsAt (e.x) - secondsAt (e.getMouseDownX());
            const auto dt = e.mods.isShiftDown() ? 0.0 : snapSeconds (raw, ! e.mods.isAltDown()) - anchor.seconds;
            const auto dv = -(float) (e.y - e.getMouseDownY()) / (float) juce::jmax (1, getHeight() - 2 * pad);
            moveSelected (dt, dv);
            reload();
            updateTooltip (e.getPosition());
        }
        else if (drag == Drag::curve)
        {
            const auto amount = juce::jlimit (-1.0f, 1.0f, curveStart + (float) (e.getMouseDownY() - e.y) / 40.0f);
            report (session.setAutomationCurve (trackIndex, param, curveIndex, amount, gestureStarted));
            gestureStarted = true;
            reload();
        }
        else if (drag == Drag::box)
        {
            box = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
            selected = selectedBeforeBox;

            for (int i = 0; i < (int) points.size(); ++i)
                if (box.toFloat().expanded (2.0f).contains (pointPos (i)))
                    selected.insert (i);
        }

        repaint();
    }

    void AutomationLane::mouseUp (const juce::MouseEvent& e)
    {
        juce::ignoreUnused (e);
        drag = Drag::none;
        box = {};
        reload();
        repaint();

        if (auto* lanes = getParentComponent())
            if (auto* area = lanes->getParentComponent())
                area->repaint();   // the focused-track highlight, on the lanes and the headers
    }

    void AutomationLane::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;

        if (const auto index = pointAt (e.getPosition()); index >= 0)
        {
            report (session.deleteAutomationPoints (trackIndex, param, { index }));
            selected.clear();
        }
        else
        {
            const auto added = session.addAutomationPoint (trackIndex, param, snapSeconds (secondsAt (e.x), ! e.mods.isAltDown()),
                                                           yToValue ((float) e.y));
            report (added >= 0);
            reload();

            if (added >= 0)
                selected = { added };
        }

        reload();
        repaint();
    }

    void AutomationLane::updateTooltip (juce::Point<int> pos)
    {
        const auto name = session.getParamName (trackIndex, param);

        if (const auto i = pointAt (pos); i >= 0)
            setTooltip (name + ": " + session.formatParamValue (trackIndex, param, points[(size_t) i].value) + " at " + secondsText (points[(size_t) i].seconds)
                        + ". Drag to move (Shift: up/down only, Alt: off the grid), Shift+click to add to the selection, double-click to delete, right-click for more");
        else
            setTooltip (name + " here: " + session.formatParamValue (trackIndex, param, yToValue ((float) pos.y))
                        + ". Double-click to add a point, drag to select points, Ctrl+drag a line to bend it, right-click for more");
    }

    void AutomationLane::mouseMove (const juce::MouseEvent& e)
    {
        const auto i = pointAt (e.getPosition());

        if (i != hover)
        {
            hover = i;
            repaint();
        }

        updateTooltip (e.getPosition());
    }

    void AutomationLane::mouseExit (const juce::MouseEvent&)
    {
        if (hover >= 0)
        {
            hover = -1;
            repaint();
        }
    }

    //==============================================================================
    void AutomationLane::showMenu (int pointIndex, juce::Point<int> pos)
    {
        enum { deleteId = 1, straightId, easeInId, easeOutId, addHereId, selectAllId, pasteId };
        juce::PopupMenu menu;
        const auto seconds = snapSeconds (secondsAt (pos.x), true);
        const auto value = yToValue ((float) pos.y);
        const auto segment = pointIndex >= 0 ? pointIndex : segmentAt (secondsAt (pos.x));

        if (pointIndex >= 0)
        {
            menu.addSectionHeader (selected.size() > 1 ? juce::String ((int) selected.size()) + " points"
                                                       : session.formatParamValue (trackIndex, param, points[(size_t) pointIndex].value) + " at " + secondsText (points[(size_t) pointIndex].seconds));
            menu.addItem (deleteId, selected.size() > 1 ? "Delete the points (Delete)" : "Delete point (Delete, or double-click it)");
        }
        else
        {
            menu.addItem (addHereId, "Add a point here (double-click)");
        }

        if (segment >= 0 && segment < (int) points.size() - 1)
        {
            const auto c = points[(size_t) segment].curve;
            menu.addItem (straightId, "Line after it: straight", true, std::abs (c) < 0.01f);
            menu.addItem (easeInId,   "Line after it: ease in (Ctrl+drag the line to bend it)", true, c > 0.01f);
            menu.addItem (easeOutId,  "Line after it: ease out", true, c < -0.01f);
        }

        menu.addItem (selectAllId, "Select every point (Ctrl+A)", ! points.empty());
        menu.addItem (pasteId, "Paste at the playhead (Ctrl+V)", ! clipboard().empty());

        juce::Component::SafePointer<AutomationLane> safe (this);
        auto onProblemCopy = onProblem;
        AutomationMenus::showLaneMenu (session, trackIndex, laneIndex, param, onProblemCopy, menu,
                                       [safe, seconds, value, segment] (int result)
        {
            if (safe == nullptr)
                return;

            auto& s = safe->session;
            bool ok = true;

            switch (result)
            {
                case deleteId:    safe->deleteSelection(); break;
                case addHereId:   ok = s.addAutomationPoint (safe->trackIndex, safe->param, seconds, value) >= 0; break;
                case straightId:  ok = s.setAutomationCurve (safe->trackIndex, safe->param, segment, 0.0f); break;
                case easeInId:    ok = s.setAutomationCurve (safe->trackIndex, safe->param, segment, 0.6f); break;
                case easeOutId:   ok = s.setAutomationCurve (safe->trackIndex, safe->param, segment, -0.6f); break;
                case selectAllId: safe->selectAll(); break;
                case pasteId:     safe->pasteNotes(); break;
                default:          break;
            }

            safe->report (ok);
            safe->reload();
            safe->repaint();
        });
    }

    //==============================================================================
    bool AutomationLane::hasEditFocus() const
    {
        return focus.hasFocus() && isShowing();
    }

    bool AutomationLane::deleteSelection()
    {
        if (selected.empty())
            return false;

        juce::Array<int> indexes;

        for (auto i : selected)
            indexes.add (i);

        report (session.deleteAutomationPoints (trackIndex, param, indexes));
        selected.clear();
        reload();
        repaint();
        return true;
    }

    void AutomationLane::selectAll()
    {
        selected.clear();

        for (int i = 0; i < (int) points.size(); ++i)
            selected.insert (i);

        repaint();
    }

    void AutomationLane::deselectAll()
    {
        selected.clear();
        repaint();
    }

    void AutomationLane::nudge (int direction)
    {
        if (selected.empty())
            return;

        dragStart.clear();

        for (auto i : selected)
            dragStart.push_back ({ i, points[(size_t) i] });

        gestureStarted = false;
        moveSelected (direction * view.getGridSeconds (session.getBpm()), 0.0f);
        reload();
        repaint();
    }

    bool AutomationLane::handleKey (const juce::KeyPress& key)
    {
        const auto code = key.getKeyCode();

        if ((code != juce::KeyPress::upKey && code != juce::KeyPress::downKey) || selected.empty()
             || key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
            return false;

        dragStart.clear();

        for (auto i : selected)
            dragStart.push_back ({ i, points[(size_t) i] });

        gestureStarted = false;
        const auto step = key.getModifiers().isShiftDown() ? 0.005f : 0.02f;
        moveSelected (0.0, code == juce::KeyPress::upKey ? step : -step);
        reload();
        repaint();
        return true;
    }

    void AutomationLane::copyNotes()
    {
        if (selected.empty())
            return;

        auto& board = clipboard();
        board.clear();
        const auto first = points[(size_t) *selected.begin()].seconds;

        for (auto i : selected)
        {
            auto p = points[(size_t) i];
            p.seconds -= first;
            board.push_back (p);
        }
    }

    void AutomationLane::cutNotes()
    {
        copyNotes();
        deleteSelection();
    }

    void AutomationLane::pasteNotes()
    {
        auto pasted = clipboard();
        const auto at = session.getPosition().inSeconds();

        for (auto& p : pasted)
            p.seconds += at;

        report (session.addAutomationPoints (trackIndex, param, pasted));
        selected.clear();
        reload();
        repaint();
    }

    void AutomationLane::duplicateNotes()
    {
        if (selected.empty())
            return;

        // Straight after the selection, one grid step on
        std::vector<Session::AutoPoint> copies;
        const auto first = points[(size_t) *selected.begin()].seconds, last = points[(size_t) *selected.rbegin()].seconds;
        const auto offset = (last - first) + view.getGridSeconds (session.getBpm());

        for (auto i : selected)
        {
            auto p = points[(size_t) i];
            p.seconds += offset;
            copies.push_back (p);
        }

        report (session.addAutomationPoints (trackIndex, param, copies));
        selected.clear();
        reload();
        repaint();
    }

    //==============================================================================
    AutomationLaneHeader::AutomationLaneHeader (Session& s, int track, int lane, Session::ParamRef ref)
        : session (s), trackIndex (track), laneIndex (lane), param (std::move (ref))
    {
        paramButton.setButtonText (session.getParamName (trackIndex, param));
        paramButton.setTooltip ("The control this lane draws: click to change it (or add another lane)");
        paramButton.onClick = [this] { showMenu(); };
        paramButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (paramButton);

        hideButton.setTooltip ("Hide this lane (its points stay; A shows or hides all of this track's lanes)");
        hideButton.onClick = [this]
        {
            if (! session.setAutomationLaneShown (trackIndex, param, false) && onProblem)
                onProblem (session.getLastError());
        };
        addAndMakeVisible (hideButton);

        clearButton.setTooltip ("Delete every point in this lane (undo brings them back)");
        clearButton.setWantsKeyboardFocus (false);
        clearButton.onClick = [this]
        {
            if (! session.clearAutomation (trackIndex, param) && onProblem)
                onProblem (session.getLastError());
        };
        addAndMakeVisible (clearButton);

        refresh();
    }

    void AutomationLaneHeader::refresh()
    {
        const auto automated = session.isAutomated (trackIndex, param);
        auto text = session.formatParamValue (trackIndex, param, session.getAutomationValueAt (trackIndex, param, session.getPosition().inSeconds()));

        if (automated && ! session.isAutomationReadOn())
            text << " (Read is off)";

        clearButton.setEnabled (automated);

        if (text != valueText)
        {
            valueText = text;
            repaint();
        }
    }

    void AutomationLaneHeader::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::panel.darker (0.15f));
        const auto colour = session.getTrackColour (trackIndex);
        g.setColour (colour.isTransparent() ? Theme::border : colour.withAlpha (0.7f));
        g.fillRect (8, 4, 3, getHeight() - 8);   // belongs to the track above
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize));
        g.drawText (valueText, juce::Rectangle<int> (18, getHeight() / 2, getWidth() - 18 - 64, getHeight() / 2 - 2),
                    juce::Justification::centredLeft, true);
    }

    void AutomationLaneHeader::resized()
    {
        auto r = getLocalBounds().withTrimmedLeft (16).reduced (2, 3);
        auto top = r.removeFromTop (r.getHeight() / 2);
        hideButton.setBounds (top.removeFromRight (top.getHeight() + 2));
        paramButton.setBounds (top.reduced (0, 1));
        clearButton.setBounds (r.removeFromRight (56).reduced (0, 1));
    }

    void AutomationLaneHeader::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            showMenu();
    }

    void AutomationLaneHeader::showMenu()
    {
        AutomationMenus::showLaneMenu (session, trackIndex, laneIndex, param, onProblem);
    }
}
