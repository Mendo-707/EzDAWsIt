/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "PianoRollView.h"
#include "../Timeline/ClipView.h"

namespace ez
{
    namespace
    {
        constexpr int toolbarHeight = 32, keyboardWidth = 58, velocityHeight = 52, rulerHeight = 20, inspectorWidth = 200;
        constexpr int inspectorMinPanelWidth = 880;   // narrower than this and the inspector starts hidden
        constexpr int minRowHeight = 8, maxRowHeight = 28;
        constexpr double minPixelsPerBeat = 8.0, maxPixelsPerBeat = 400.0;
        constexpr int resizeZone = 6, dragThreshold = 3;
        constexpr double scrollPaddingBeats = 8.0;

        bool isBlackKey (int pitch)
        {
            const int n = pitch % 12;
            return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        }

        const double gridChoices[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        const char* const gridNames[] = { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
        constexpr int defaultGridIndex = 4;

        const double strengthChoices[] = { 1.0, 0.75, 0.5, 0.25 };
        const char* const strengthNames[] = { "100%", "75%", "50%", "25%" };
        const double swingChoices[] = { 0.0, 0.25, 0.5, 0.67, 0.75 };
        const char* const swingNames[] = { "0%", "25%", "50%", "67%", "75%" };
        const int velocityChoices[] = { 32, 64, 90, 110, 127 };
        const char* const velocityNames[] = { "Very soft (32)", "Soft (64)", "Medium (90)", "Loud (110)", "Maximum (127)" };

        const char* const rootNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const char* const scaleNames[] = { "No scale", "Major", "Minor" };

        /** "11.2.3": bar . beat . sixteenth, counted from 1, following the song's time signatures. */
        juce::String barBeatText (const Session& session, double songBeat)
        {
            const auto [bar, inBar] = session.getBarAt (songBeat);
            const auto beat = juce::jmax (0, (int) std::floor (inBar + 1.0e-9));
            const auto sixteenth = juce::jlimit (0, 3, (int) std::floor ((inBar - beat) * 4.0 + 1.0e-6));
            return juce::String (bar + 1) + "." + juce::String (beat + 1) + "." + juce::String (sixteenth + 1);
        }

        /** "2 bars", "1 bar 2 beats", "1.5 beats" (bars as long as the clip's time signature says). */
        juce::String lengthText (double beats, double beatsPerBar)
        {
            const auto bars = (int) std::floor (beats / beatsPerBar + 1.0e-6);
            const auto rest = beats - bars * beatsPerBar;
            juce::StringArray parts;

            if (bars > 0)
                parts.add (juce::String (bars) + (bars == 1 ? " bar" : " bars"));

            if (rest > 1.0e-3 || bars == 0)
            {
                const auto r = juce::String (juce::roundToInt (rest * 100.0) / 100.0);
                parts.add (r + (std::abs (rest - 1.0) < 1.0e-3 ? " beat" : " beats"));
            }

            return parts.joinIntoString (" ");
        }

        // Menu items (the ranges leave room for the lists)
        enum MenuID
        {
            menuDraw = 1, menuSelect, menuErase,
            menuCut, menuCopy, menuPaste, menuDuplicate, menuDelete,
            menuSelectAll, menuSelectNone, menuInvert,
            menuUp, menuDown, menuOctaveUp, menuOctaveDown,
            menuQuantise, menuLegato, menuLengthToGrid,
            menuSnap, menuFollowKey, menuHighlight, menuFold, menuFollow, menuInspector,
            menuZoomIn, menuZoomOut, menuTaller, menuShorter, menuFit,
            menuGrid = 100, menuStrength = 200, menuSwing = 300, menuRoot = 400, menuScaleType = 500, menuVelocity = 600
        };
    }

    //==============================================================================
    /** The keys down the left: click to hear a note. */
    class PianoRollView::Keyboard : public juce::Component
    {
    public:
        explicit Keyboard (PianoRollView& o) : owner (o)    { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);
            const auto firstRow = juce::jmax (0, owner.rowAt (0)), lastRow = juce::jmin (owner.rowCount() - 1, owner.rowAt (getHeight()));

            for (int row = firstRow; row <= lastRow; ++row)
            {
                const auto pitch = owner.rowPitches.getUnchecked (row);
                const juce::Rectangle<int> r (0, row * owner.rowHeight - owner.scrollY, getWidth(), owner.rowHeight);
                const bool black = isBlackKey (pitch);
                g.setColour (pitch == heldPitch ? Theme::accent : black ? juce::Colour (0xff17191d) : juce::Colour (0xffd9dce2));
                g.fillRect (black ? r.withTrimmedRight (getWidth() / 3) : r.withTrimmedBottom (1));

                if (pitch % 12 == 0 && owner.rowHeight >= 10)
                {
                    g.setColour (Theme::ink);
                    g.setFont (Theme::font (juce::jmin (10.0f, (float) owner.rowHeight - 2.0f), true));
                    g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3), r.withTrimmedRight (4),
                                juce::Justification::centredRight, false);
                }
            }

            g.setColour (Theme::laneSeparator);
            g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        }

        void mouseDown (const juce::MouseEvent& e) override     { play (e.y); }
        void mouseDrag (const juce::MouseEvent& e) override     { play (e.y); }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (auto* clip = owner.getClip())
                owner.session.stopAudition (*clip);

            heldPitch = -1;
            repaint();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;   // scrolls the grid: defined below it

    private:
        PianoRollView& owner;
        int heldPitch = -1;

        void play (int y)
        {
            const auto pitch = owner.yToPitch (y);

            if (pitch == heldPitch)
                return;

            heldPitch = pitch;

            if (auto* clip = owner.getClip())
                owner.session.auditionNote (*clip, pitch, owner.newNoteVelocity);

            repaint();
        }
    };

    //==============================================================================
    /** The note grid. */
    class PianoRollView::Grid : public juce::Component
    {
    public:
        explicit Grid (PianoRollView& o) : owner (o)    { setOpaque (true); }

        /** Esc: abandons a drag in progress without changing anything. True if there was one. */
        bool cancelGesture()
        {
            const bool wasActive = mode != Mode::none;

            if (wasActive)
                if (auto* clip = owner.getClip())
                    owner.session.stopAudition (*clip);

            mode = Mode::none;
            dragging = false;
            copyOnDrag = false;
            rubberBand = {};
            deltaBeats = 0.0;
            deltaPitch = 0;
            eraseList.clear();

            if (wasActive)
                repaint();

            return wasActive;
        }

        //==============================================================================
        juce::Rectangle<float> noteBounds (const te::MidiNote& n, bool withPreview) const
        {
            auto start = n.getStartBeat().inBeats(), length = n.getLengthBeats().inBeats();
            auto pitch = n.getNoteNumber();

            if (withPreview && dragging && owner.selected.contains (const_cast<te::MidiNote*> (&n)))
            {
                if (mode == Mode::move && ! copyOnDrag)   { start += deltaBeats; pitch += deltaPitch; }
                if (mode == Mode::resize)                 length = juce::jmax (Session::minNoteBeats, length + deltaBeats);
                if (mode == Mode::resizeStart)            { start += deltaBeats; length = juce::jmax (Session::minNoteBeats, length - deltaBeats); }
            }

            return rectFor (start, length, pitch);
        }

        te::MidiNote* noteAt (te::MidiClip& clip, juce::Point<int> p) const
        {
            const auto& notes = clip.getSequence().getNotes();

            for (int i = notes.size(); --i >= 0;)
                if (noteBounds (*notes.getUnchecked (i), false).expanded (0.0f, 1.0f).contains (p.toFloat()))
                    return notes.getUnchecked (i);

            return nullptr;
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::laneBackground);
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            const auto w = getWidth(), h = getHeight();

            // Rows: black keys darker; with a scale chosen, its notes are lifted and its root marked
            const auto firstRow = juce::jmax (0, owner.rowAt (0)), lastRow = juce::jmin (owner.rowCount() - 1, owner.rowAt (h));
            const bool showScale = owner.scaleType != 0 && owner.highlightScale;

            for (int row = firstRow; row <= lastRow; ++row)
            {
                const auto pitch = owner.rowPitches.getUnchecked (row);
                const auto y = row * owner.rowHeight - owner.scrollY;

                if (showScale)
                {
                    if (! owner.isInScale (pitch))
                    {
                        g.setColour (juce::Colour (0x40000000));
                        g.fillRect (0, y, w, owner.rowHeight);
                    }
                    else if ((pitch - owner.scaleRoot + 120) % 12 == 0)
                    {
                        g.setColour (Theme::accent.withAlpha (0.10f));
                        g.fillRect (0, y, w, owner.rowHeight);
                    }
                }
                else if (isBlackKey (pitch))
                {
                    g.setColour (juce::Colour (0x26000000));
                    g.fillRect (0, y, w, owner.rowHeight);
                }

                g.setColour (pitch % 12 == 0 ? Theme::gridBar : Theme::laneSeparator.withAlpha (0.6f));
                g.drawHorizontalLine (y + owner.rowHeight - 1, 0.0f, (float) w);
            }

            // Vertical lines on the song's grid (which need not start at the clip's beat 0)
            const auto contentStart = owner.session.getContentStartInSongBeats (*clip);
            auto step = owner.gridBeats;

            while (step * owner.pixelsPerBeat < 9.0)
                step *= 2.0;

            const auto firstSongBeat = std::floor ((owner.xToBeat (0) + contentStart) / step) * step;

            for (auto songBeat = firstSongBeat; ; songBeat += step)
            {
                const auto x = juce::roundToInt (owner.beatToX (songBeat - contentStart));

                if (x > w)
                    break;

                const auto inBar = owner.session.getBarAt (songBeat + 1.0e-7).second;
                const bool isBar = inBar < 1.0e-5;
                const bool isBeat = std::abs (songBeat - std::round (songBeat)) < 1.0e-6;
                g.setColour (isBar ? Theme::gridBar.brighter (0.25f) : isBeat ? Theme::gridBar : Theme::gridBeat);
                g.drawVerticalLine (x, 0.0f, (float) h);
            }

            // Outside the clip's edges: notes there exist but don't play
            const auto clipStart = clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats();
            const auto clipEnd = clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats();
            const auto x1 = juce::roundToInt (owner.beatToX (clipStart)), x2 = juce::roundToInt (owner.beatToX (clipEnd));
            g.setColour (juce::Colour (0x73000000));

            if (x1 > 0)   g.fillRect (0, 0, juce::jmin (w, x1), h);
            if (x2 < w)   g.fillRect (juce::jmax (0, x2), 0, w - juce::jmax (0, x2), h);

            g.setColour (Theme::textDim);
            g.drawVerticalLine (x1, 0.0f, (float) h);
            g.drawVerticalLine (x2, 0.0f, (float) h);

            // Notes
            const auto base = ClipView::colourFor (*clip).withBrightness (0.85f);   // the same colour as the clip in the timeline
            const auto visible = getLocalBounds().toFloat();
            const bool copying = dragging && mode == Mode::move && copyOnDrag;

            for (auto* n : clip->getSequence().getNotes())
            {
                const auto r = noteBounds (*n, true);

                if (! r.intersects (visible))
                    continue;

                const bool isSelected = owner.selected.contains (n);
                const auto fill = base.withMultipliedBrightness (0.55f + 0.45f * (float) n->getVelocity() / 127.0f);
                g.setColour (eraseList.contains (n) ? fill.withAlpha (0.3f) : fill);
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (eraseList.contains (n) ? Theme::danger : isSelected ? Theme::selection : Theme::ink.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, isSelected || eraseList.contains (n) ? 1.5f : 1.0f);

                if (copying && isSelected)   // the copy that will be dropped
                {
                    const auto ghost = rectFor (n->getStartBeat().inBeats() + deltaBeats, n->getLengthBeats().inBeats(), n->getNoteNumber() + deltaPitch);
                    g.setColour (fill.withAlpha (0.55f));
                    g.fillRoundedRectangle (ghost, 2.0f);
                    g.setColour (Theme::selection);
                    g.drawRoundedRectangle (ghost.reduced (0.5f), 2.0f, 1.5f);
                }
            }

            // The note being drawn
            if (mode == Mode::draw)
            {
                const auto length = dragging && drawEnd - drawStart >= Session::minNoteBeats ? drawEnd - drawStart : owner.newNoteBeats;
                const auto r = rectFor (drawStart, length, drawPitch);
                g.setColour (base.withAlpha (0.6f));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (Theme::selection);
                g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.5f);
            }

            if (! rubberBand.isEmpty())
            {
                g.setColour (Theme::selection.withAlpha (0.12f));
                g.fillRect (rubberBand);
                g.setColour (Theme::selection);
                g.drawRect (rubberBand);
            }

            // Playhead
            const auto playX = juce::roundToInt (owner.beatToX (clip->getContentBeatAtTime (owner.session.getPosition()).inBeats()));
            g.setColour (Theme::playhead);
            g.drawVerticalLine (playX, 0.0f, (float) h);
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override     { updateCursor (e); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            mode = Mode::none;
            dragging = false;
            copyOnDrag = false;
            rubberBand = {};
            deltaBeats = 0.0;
            deltaPitch = 0;
            eraseList.clear();
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            if (e.mods.isMiddleButtonDown())   // pan
            {
                mode = Mode::pan;
                panStartBeat = owner.viewStartBeat;
                panStartY = owner.scrollY;
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                return;
            }

            owner.pruneSelection();
            anchor = noteAt (*clip, e.getPosition());

            if (e.mods.isPopupMenu())   // right-click: a note you click is selected first
            {
                if (anchor != nullptr && ! owner.selected.contains (anchor))
                {
                    owner.selected.clear();
                    owner.selected.add (anchor);
                    owner.repaintAll();
                }

                owner.showContextMenu (owner.snapBeat (*clip, owner.xToBeat (e.x), true, e.mods.isAltDown()));
                return;
            }

            if (owner.tool == Tool::erase)
            {
                mode = Mode::erase;

                if (anchor != nullptr)
                    eraseList.addIfNotAlreadyThere (anchor);

                repaint();
                return;
            }

            if (anchor == nullptr)
            {
                if (owner.tool == Tool::draw && ! e.mods.isShiftDown())
                {
                    mode = Mode::draw;
                    drawPitch = owner.yToPitch (e.y);
                    drawStart = owner.snapBeat (*clip, owner.xToBeat (e.x), true, e.mods.isAltDown());
                    drawEnd = drawStart;
                    owner.selected.clear();
                    owner.session.auditionNote (*clip, drawPitch, owner.newNoteVelocity);
                    owner.repaintAll();
                    return;
                }

                selectionBeforeBand = e.mods.isShiftDown() ? owner.selected : Session::NoteList();

                if (! e.mods.isShiftDown())
                    owner.selected.clear();

                mode = Mode::band;
                owner.repaintAll();
                return;
            }

            if (e.mods.isShiftDown())
            {
                if (owner.selected.contains (anchor))   owner.selected.removeFirstMatchingValue (anchor);
                else                                    owner.selected.add (anchor);

                owner.repaintAll();
                return;
            }

            if (! owner.selected.contains (anchor))
            {
                owner.selected.clear();
                owner.selected.add (anchor);
            }

            mode = isOnRightEdge (*anchor, e.x) ? Mode::resize : isOnLeftEdge (*anchor, e.x) ? Mode::resizeStart : Mode::move;
            copyOnDrag = mode == Mode::move && e.mods.isCommandDown();
            anchorStart = anchor->getStartBeat().inBeats();
            anchorLength = anchor->getLengthBeats().inBeats();
            anchorPitch = anchor->getNoteNumber();
            owner.newNoteVelocity = anchor->getVelocity();
            owner.session.auditionNote (*clip, anchorPitch, anchor->getVelocity());
            owner.repaintAll();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* clip = owner.getClip();

            if (clip == nullptr || mode == Mode::none)
                return;

            if (mode == Mode::pan)
            {
                owner.viewStartBeat = panStartBeat - e.getDistanceFromDragStartX() / owner.pixelsPerBeat;
                owner.scrollY = panStartY - e.getDistanceFromDragStartY();
                owner.clampView();
                owner.repaintAll();
                return;
            }

            if (mode == Mode::erase)   // everything the pointer crosses goes
            {
                dragging = true;

                if (auto* n = noteAt (*clip, e.getPosition()))
                    eraseList.addIfNotAlreadyThere (n);

                repaint();
                return;
            }

            if (! dragging && e.getDistanceFromDragStart() < dragThreshold)
                return;

            dragging = true;
            const bool bypass = e.mods.isAltDown();

            if (mode == Mode::draw)
            {
                drawEnd = owner.snapBeat (*clip, owner.xToBeat (e.x), false, bypass);
                repaint();
                return;
            }

            if (mode == Mode::band)
            {
                rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
                owner.selected = selectionBeforeBand;

                for (auto* n : clip->getSequence().getNotes())
                    if (noteBounds (*n, false).intersects (rubberBand.toFloat()))
                        owner.selected.addIfNotAlreadyThere (n);

                owner.repaintAll();
                return;
            }

            owner.pruneSelection();

            if (owner.selected.isEmpty())   // e.g. an undo removed the notes mid-drag
            {
                mode = Mode::none;
                return;
            }

            const auto draggedBeats = e.getDistanceFromDragStartX() / owner.pixelsPerBeat;
            const bool snapping = owner.snapEnabled && ! bypass;
            const auto shortest = snapping ? owner.gridBeats : Session::minNoteBeats;

            if (mode == Mode::resize)
            {
                const auto newEnd = owner.snapBeat (*clip, anchorStart + anchorLength + draggedBeats, false, bypass);
                deltaBeats = juce::jmax (newEnd, anchorStart + shortest) - (anchorStart + anchorLength);
            }
            else if (mode == Mode::resizeStart)
            {
                const auto newStart = owner.snapBeat (*clip, anchorStart + draggedBeats, false, bypass);
                auto lowest = -1.0e12, highest = 1.0e12;

                for (auto* n : owner.selected)   // the same limits Session applies, so the preview is what you get
                {
                    lowest = juce::jmax (lowest, -n->getStartBeat().inBeats());
                    highest = juce::jmin (highest, n->getLengthBeats().inBeats() - Session::minNoteBeats);
                }

                highest = juce::jmin (highest, anchorLength - shortest);
                deltaBeats = juce::jlimit (lowest, juce::jmax (lowest, highest), newStart - anchorStart);
            }
            else
            {
                // Keep the whole selection on the keyboard and after beat 0
                double earliest = 1.0e12;
                int lowest = 127, highest = 0;

                for (auto* n : owner.selected)
                {
                    earliest = juce::jmin (earliest, n->getStartBeat().inBeats());
                    lowest = juce::jmin (lowest, n->getNoteNumber());
                    highest = juce::jmax (highest, n->getNoteNumber());
                }

                deltaBeats = juce::jmax (-earliest, owner.snapBeat (*clip, anchorStart + draggedBeats, false, bypass) - anchorStart);

                // Pitch moves by rows (which are semitones unless the view is folded to a scale)
                const auto rowDelta = owner.rowAt (e.y) - owner.rowAt (e.getMouseDownY());
                const auto targetRow = juce::jlimit (0, owner.rowCount() - 1, owner.pitchToRow (anchorPitch) + rowDelta);
                const auto newPitchDelta = juce::jlimit (-lowest, 127 - highest, owner.rowPitches.getUnchecked (targetRow) - anchorPitch);

                if (newPitchDelta != deltaPitch)
                {
                    deltaPitch = newPitchDelta;
                    owner.session.auditionNote (*clip, anchorPitch + deltaPitch, owner.newNoteVelocity);
                }
            }

            owner.repaintAll();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            auto* clip = owner.getClip();
            const auto finished = std::exchange (mode, Mode::none);
            const bool wasDragging = std::exchange (dragging, false);
            const bool wasCopy = std::exchange (copyOnDrag, false);

            if (clip == nullptr)
                return;

            owner.session.stopAudition (*clip);

            if (finished == Mode::pan)
            {
                setMouseCursor (juce::MouseCursor::NormalCursor);   // the next move sets the right one
                return;
            }

            if (finished == Mode::band)
            {
                rubberBand = {};   // a plain click just deselects (seek with the ruler, not here)
            }
            else if (finished == Mode::draw)
            {
                const auto length = wasDragging && drawEnd - drawStart >= Session::minNoteBeats ? drawEnd - drawStart : owner.newNoteBeats;

                if (auto* added = owner.session.addMidiNote (*clip, drawPitch, drawStart, length, owner.newNoteVelocity))
                {
                    owner.selected.clear();
                    owner.selected.add (added);

                    if (wasDragging)
                        owner.newNoteBeats = juce::jmax (Session::minNoteBeats, length);   // new notes copy the last length you drew
                }
                else
                {
                    owner.report (false, "Could not add a note");
                }
            }
            else if (finished == Mode::erase)
            {
                const auto doomed = std::exchange (eraseList, {});

                if (! doomed.isEmpty())
                {
                    for (auto* n : doomed)
                        owner.selected.removeFirstMatchingValue (n);

                    owner.report (owner.session.deleteMidiNotes (*clip, doomed), "Could not delete the notes");
                }
            }
            else if (wasDragging && finished == Mode::move && (deltaBeats != 0.0 || deltaPitch != 0))
            {
                if (wasCopy)
                {
                    auto copies = owner.session.copyMoveMidiNotes (*clip, owner.selected, deltaBeats, deltaPitch);

                    if (copies.isEmpty())   owner.report (false, "Could not copy the notes");
                    else                    owner.selected = copies;
                }
                else
                {
                    owner.report (owner.session.moveMidiNotes (*clip, owner.selected, deltaBeats, deltaPitch), "Could not move the notes");
                }
            }
            else if (wasDragging && finished == Mode::resize && deltaBeats != 0.0)
            {
                if (owner.session.resizeMidiNotes (*clip, owner.selected, deltaBeats))
                    owner.newNoteBeats = juce::jmax (Session::minNoteBeats, anchorLength + deltaBeats);   // new notes copy the last length you set
                else
                    owner.report (false, "Could not change the notes");
            }
            else if (wasDragging && finished == Mode::resizeStart && deltaBeats != 0.0)
            {
                owner.report (owner.session.resizeMidiNotesFromStart (*clip, owner.selected, deltaBeats), "Could not change the notes");
            }
            else if (! wasDragging && finished == Mode::move && owner.selected.size() > 1)
            {
                owner.pruneSelection();   // an undo may have removed the note since the click
                const bool stillThere = owner.selected.contains (anchor);
                owner.selected.clear();   // a plain click on one of several selected notes selects just it

                if (stillThere)
                    owner.selected.add (anchor);
            }

            deltaBeats = 0.0;
            deltaPitch = 0;
            owner.repaintAll();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto* clip = owner.getClip();

            // Draw adds on a single click and Erase on a click, so a double-click would undo itself there
            if (clip == nullptr || e.mods.isPopupMenu() || e.mods.isMiddleButtonDown() || owner.tool != Tool::select)
                return;

            if (auto* note = noteAt (*clip, e.getPosition()))
            {
                owner.selected.removeFirstMatchingValue (note);
                owner.report (owner.session.deleteMidiNotes (*clip, { note }), "Could not delete the note");
            }
            else
            {
                const auto pitch = owner.yToPitch (e.y);
                const auto beat = owner.snapBeat (*clip, owner.xToBeat (e.x), true, e.mods.isAltDown());

                if (auto* added = owner.session.addMidiNote (*clip, pitch, beat, owner.newNoteBeats, owner.newNoteVelocity))
                {
                    owner.selected.clear();
                    owner.selected.add (added);
                    owner.session.auditionNote (*clip, pitch, owner.newNoteVelocity, true);   // the mouse is already up: let it stop itself
                }
                else
                {
                    owner.report (false, "Could not add a note");
                }
            }

            owner.repaintAll();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            const auto amount = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

            if (amount == 0.0f)
                return;

            if (e.mods.isCommandDown())
            {
                owner.zoomTime (amount > 0 ? 1.25 : 0.8, e.x);
            }
            else if (e.mods.isAltDown())
            {
                owner.setRowHeight (owner.rowHeight + (amount > 0 ? 2 : -2), e.y);
            }
            else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
            {
                owner.viewStartBeat -= amount * 240.0 / owner.pixelsPerBeat;
                owner.clampView();
            }
            else
            {
                owner.scrollY -= juce::roundToInt (amount * 160.0f);
                owner.clampView();
            }

            owner.repaintAll();
        }

    private:
        enum class Mode { none, move, resize, resizeStart, band, draw, erase, pan };

        PianoRollView& owner;
        Mode mode = Mode::none;
        bool dragging = false, copyOnDrag = false;
        te::MidiNote* anchor = nullptr;   // only compared or used straight after mouseDown found it
        double anchorStart = 0.0, anchorLength = 0.0, deltaBeats = 0.0;
        int anchorPitch = 60, deltaPitch = 0;
        double drawStart = 0.0, drawEnd = 0.0;
        int drawPitch = 60;
        double panStartBeat = 0.0;
        int panStartY = 0;
        juce::Rectangle<int> rubberBand;
        Session::NoteList selectionBeforeBand, eraseList;

        juce::Rectangle<float> rectFor (double start, double length, int pitch) const
        {
            return { (float) owner.beatToX (start), (float) owner.pitchToY (pitch) + 1.0f,
                     juce::jmax (3.0f, (float) (length * owner.pixelsPerBeat) - 1.0f), (float) owner.rowHeight - 2.0f };
        }

        bool isOnRightEdge (const te::MidiNote& n, int x) const
        {
            const auto r = noteBounds (n, false);
            return r.getWidth() > 10.0f && (float) x >= r.getRight() - (float) resizeZone;
        }

        bool isOnLeftEdge (const te::MidiNote& n, int x) const
        {
            const auto r = noteBounds (n, false);
            return r.getWidth() > 10.0f && (float) x <= r.getX() + (float) resizeZone;
        }

        /** The pointer says what a click will do. */
        void updateCursor (const juce::MouseEvent& e)
        {
            auto* clip = owner.getClip();
            auto* note = clip != nullptr ? noteAt (*clip, e.getPosition()) : nullptr;
            auto shape = juce::MouseCursor::NormalCursor;

            if (owner.tool == Tool::erase)
                shape = note != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor;
            else if (note != nullptr)
                shape = isOnRightEdge (*note, e.x) || isOnLeftEdge (*note, e.x) ? juce::MouseCursor::LeftRightResizeCursor
                       : e.mods.isCommandDown()                                  ? juce::MouseCursor::CopyingCursor
                                                                                 : juce::MouseCursor::UpDownLeftRightResizeCursor;
            else if (owner.tool == Tool::draw)
                shape = juce::MouseCursor::CrosshairCursor;

            setMouseCursor (shape);
        }
    };

    void PianoRollView::Keyboard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        owner.grid->mouseWheelMove (e.getEventRelativeTo (owner.grid.get()), w);
    }

    //==============================================================================
    /** Bar numbers along the top. Click or drag to move the playhead. */
    class PianoRollView::Ruler : public juce::Component,
                                 public juce::SettableTooltipClient
    {
    public:
        explicit Ruler (PianoRollView& o) : owner (o)
        {
            setOpaque (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Bars. Click or drag to move the playhead (Alt ignores the grid).");
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bar);
            g.setColour (Theme::laneSeparator);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            const auto contentStart = owner.session.getContentStartInSongBeats (*clip);
            const auto beatsPerBar = owner.session.getBeatsPerBarAt (clip->getPosition().getStart());
            const auto labelEvery = juce::jmax (1, (int) std::ceil (44.0 / (owner.pixelsPerBeat * beatsPerBar)));

            for (auto songBeat = std::floor (owner.xToBeat (0) + contentStart); ; songBeat += 1.0)
            {
                const auto x = juce::roundToInt (owner.beatToX (songBeat - contentStart));

                if (x > getWidth())
                    break;

                const auto [barIndex, inBar] = owner.session.getBarAt (songBeat + 1.0e-7);
                const bool isBar = inBar < 1.0e-5;

                if (isBar)
                {
                    g.setColour (Theme::gridBar.brighter (0.25f));
                    g.drawVerticalLine (x, 2.0f, (float) getHeight());

                    if (barIndex % labelEvery == 0)
                    {
                        g.setColour (Theme::text);
                        g.setFont (Theme::font (Theme::smallFontSize - 0.5f, true));
                        g.drawText (juce::String (barIndex + 1), x + 4, 1, 44, getHeight() - 3, juce::Justification::centredLeft, false);
                    }
                }
                else if (owner.pixelsPerBeat >= 12.0)
                {
                    g.setColour (Theme::gridBar);
                    g.drawVerticalLine (x, (float) getHeight() - 6.0f, (float) getHeight());
                }
            }

            const auto playX = juce::roundToInt (owner.beatToX (clip->getContentBeatAtTime (owner.session.getPosition()).inBeats()));
            g.setColour (Theme::playhead);
            g.drawVerticalLine (playX, 0.0f, (float) getHeight());
        }

        void mouseDown (const juce::MouseEvent& e) override     { seek (e); }
        void mouseDrag (const juce::MouseEvent& e) override     { seek (e); }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            owner.grid->mouseWheelMove (e.getEventRelativeTo (owner.grid.get()), w);
        }

    private:
        PianoRollView& owner;

        void seek (const juce::MouseEvent& e)
        {
            if (auto* clip = owner.getClip())
            {
                const auto beat = juce::jmax (0.0, owner.snapBeat (*clip, owner.xToBeat (e.x), false, e.mods.isAltDown()));
                owner.session.setPosition (clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (beat)));
                repaint();
            }
        }
    };

    //==============================================================================
    /** One stem per note: its height is how hard the note is played. Drag to change. */
    class PianoRollView::VelocityLane : public juce::Component,
                                        public juce::SettableTooltipClient
    {
    public:
        explicit VelocityLane (PianoRollView& o) : owner (o)
        {
            setOpaque (true);
            setTooltip ("Velocity: how hard each note is played. Drag across the stems to change them.");
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bar);
            g.setColour (Theme::laneSeparator);
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());

            if (auto* clip = owner.getClip())
            {
                for (auto* n : clip->getSequence().getNotes())
                {
                    const auto x = (float) owner.beatToX (n->getStartBeat().inBeats());

                    if (x < -4.0f || x > (float) getWidth())
                        continue;

                    const auto top = yFor (n->getVelocity());
                    g.setColour (owner.selected.contains (n) ? Theme::selection : Theme::accent);
                    g.fillRect (x, top, 2.0f, (float) getHeight() - top);
                    g.fillEllipse (x - 2.0f, top - 3.0f, 6.0f, 6.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            owner.pruneSelection();
            gestureStarted = false;   // the first change of the drag starts its undo step
            apply (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override     { apply (e); }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            owner.grid->mouseWheelMove (e.getEventRelativeTo (owner.grid.get()), w);
        }

    private:
        PianoRollView& owner;
        bool gestureStarted = false;

        float yFor (int velocity) const
        {
            return 5.0f + (1.0f - (float) velocity / 127.0f) * ((float) getHeight() - 8.0f);
        }

        void apply (const juce::MouseEvent& e)
        {
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            const auto velocity = juce::jlimit (1, 127, juce::roundToInt (127.0f * (1.0f - ((float) e.y - 5.0f) / ((float) getHeight() - 8.0f))));
            Session::NoteList targets;

            for (auto* n : clip->getSequence().getNotes())
                if (std::abs (owner.beatToX (n->getStartBeat().inBeats()) - e.x) <= 5.0)
                    targets.add (n);

            if (targets.isEmpty())
                return;

            // Stacked notes (a chord) share an x: with a selection, only the selected ones change
            Session::NoteList chosen;

            for (auto* n : targets)
                if (owner.selected.contains (n))
                    chosen.add (n);

            if (owner.session.setMidiNoteVelocity (*clip, chosen.isEmpty() ? targets : chosen, velocity, gestureStarted))
                gestureStarted = true;   // the rest of the drag joins this undo step

            owner.newNoteVelocity = velocity;
            owner.repaintAll();
        }
    };

    //==============================================================================
    /** The left column: the clip's name, where it is and whether it loops, the key, and the selected
        notes' values. Anything you can change by dragging is also a typed value here. */
    class PianoRollView::Inspector : public juce::Component
    {
    public:
        static constexpr int contentHeight = 552;

        explicit Inspector (PianoRollView& o) : owner (o)
        {
            nameEditor.setMultiLine (false);
            nameEditor.setTooltip ("The clip's name (Enter to apply)");
            nameEditor.onReturnKey = [this] { commitName(); owner.returnFocusToMainWindow(); };
            nameEditor.onEscapeKey = [this] { refreshName (true); owner.returnFocusToMainWindow(); };
            nameEditor.onFocusLost = [this] { commitName(); };
            addAndMakeVisible (nameEditor);

            loopButton.setTooltip ("Repeat the clip's content for as long as the clip is. Drag the clip's right edge to make it longer.");
            loopButton.onClick = [this]
            {
                if (auto* clip = owner.getClip())
                {
                    owner.report (owner.session.setClipLooping (*clip, ! owner.session.isClipLooping (*clip)), "Could not change the clip");
                    owner.repaintAll();
                }
            };
            addAndMakeVisible (loopButton);

            for (auto* c : std::initializer_list<juce::Component*> { &owner.scaleRootBox, &owner.scaleTypeBox, &owner.followKeyButton, &owner.highlightButton, &owner.foldButton })
                addAndMakeVisible (c);

            setupSlider (selVelocity, 0, 1.0, 127.0, 1.0, "Velocity of the selected notes (click to type a number)", [this] (double v, bool continues)
            {
                if (auto* clip = owner.getClip())
                {
                    owner.pruneSelection();

                    if (! owner.selected.isEmpty())
                    {
                        owner.report (owner.session.setMidiNoteVelocity (*clip, owner.selected, juce::roundToInt (v), continues), "Could not change the notes");
                        owner.repaintAll();
                    }
                }
            });

            setupSlider (selLength, 1, Session::minNoteBeats, 16.0, Session::minNoteBeats, "Length in beats of the selected notes (click to type a number)", [this] (double v, bool continues)
            {
                if (auto* clip = owner.getClip())
                {
                    owner.pruneSelection();

                    if (! owner.selected.isEmpty())
                    {
                        owner.report (owner.session.setMidiNoteLength (*clip, owner.selected, v, continues), "Could not change the notes");
                        owner.repaintAll();
                    }
                }
            });

            setupSlider (newVelocity, 2, 1.0, 127.0, 1.0, "How hard notes you add are played", [this] (double v, bool) { owner.newNoteVelocity = juce::roundToInt (v); });
            setupSlider (newLength, 3, Session::minNoteBeats, 16.0, Session::minNoteBeats, "How long notes you add are, in beats", [this] (double v, bool) { owner.newNoteBeats = v; });
            selLength.setNumDecimalPlacesToDisplay (3);
            newLength.setNumDecimalPlacesToDisplay (3);

            // Nothing selected: a dash, not a stale "1"
            selVelocity.textFromValueFunction = [this] (double v) { return selVelocity.isEnabled() ? juce::String (juce::roundToInt (v)) : juce::String ("-"); };
            selLength.textFromValueFunction = [this] (double v)
            {
                return selLength.isEnabled() ? juce::String (v, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : juce::String ("-");
            };
            selLength.setTextValueSuffix (" beats");
            newLength.setTextValueSuffix (" beats");
            selLength.setSkewFactorFromMidPoint (1.0);
            newLength.setSkewFactorFromMidPoint (1.0);
        }

        /** Brings every value up to date with the clip and the selection. */
        void refresh()
        {
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            refreshName (false);
            const auto contentStartSong = owner.session.getContentStartInSongBeats (*clip);
            const auto startBeat = clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats();
            const auto endBeat = clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats();
            setText (startIndex, barBeatText (owner.session, contentStartSong + startBeat));
            setText (lengthIndex, lengthText (endBeat - startBeat, owner.session.getBeatsPerBarAt (clip->getPosition().getStart())));
            loopButton.setToggleState (owner.session.isClipLooping (*clip), juce::dontSendNotification);
            setText (hintIndex, owner.scaleType == 0 ? "Pick a key and a scale to light up the notes that fit it."
                                                     : "Lighter rows are notes in the key. Notes outside it still play.");

            owner.pruneSelection();
            const auto& sel = owner.selected;
            const bool any = ! sel.isEmpty();

            if (! any)
            {
                setText (selectedIndex, "No notes selected");
            }
            else if (sel.size() == 1)
            {
                setText (selectedIndex, juce::MidiMessage::getMidiNoteName (sel.getFirst()->getNoteNumber(), true, true, 3)
                                          + " at " + barBeatText (owner.session, contentStartSong + sel.getFirst()->getStartBeat().inBeats()));
            }
            else
            {
                int lowest = 127, highest = 0;

                for (auto* n : sel)
                {
                    lowest = juce::jmin (lowest, n->getNoteNumber());
                    highest = juce::jmax (highest, n->getNoteNumber());
                }

                setText (selectedIndex, juce::String (sel.size()) + " notes, " + juce::MidiMessage::getMidiNoteName (lowest, true, true, 3)
                                          + " to " + juce::MidiMessage::getMidiNoteName (highest, true, true, 3));
            }

            selVelocity.setEnabled (any);
            selLength.setEnabled (any);
            selVelocity.updateText();
            selLength.updateText();

            if (any)
            {
                double velocity = 0.0, length = 0.0;

                for (auto* n : sel)
                {
                    velocity += n->getVelocity();
                    length += n->getLengthBeats().inBeats();
                }

                if (! selVelocity.isMouseButtonDown())   selVelocity.setValue (std::round (velocity / sel.size()), juce::dontSendNotification);
                if (! selLength.isMouseButtonDown())     selLength.setValue (length / sel.size(), juce::dontSendNotification);
            }

            if (! newVelocity.isMouseButtonDown())   newVelocity.setValue (owner.newNoteVelocity, juce::dontSendNotification);
            if (! newLength.isMouseButtonDown())     newLength.setValue (owner.newNoteBeats, juce::dontSendNotification);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::panel);

            for (auto& t : texts)
            {
                if (t.heading)
                {
                    g.setColour (Theme::textDim);
                    g.setFont (Theme::font (Theme::captionFontSize, true));
                    g.drawText (t.text.toUpperCase(), t.bounds, juce::Justification::centredLeft, false);
                    g.setColour (Theme::laneSeparator);
                    g.drawHorizontalLine (t.bounds.getBottom() + 1, (float) t.bounds.getX(), (float) t.bounds.getRight());
                }
                else if (t.wrap)
                {
                    g.setColour (Theme::textDim);
                    g.setFont (Theme::font (Theme::smallFontSize));
                    g.drawFittedText (t.text, t.bounds, juce::Justification::topLeft, 3);
                }
                else
                {
                    g.setColour (t.dim ? Theme::textDim : Theme::text);
                    g.setFont (Theme::font (Theme::smallFontSize + 0.5f));
                    g.drawText (t.text, t.bounds, juce::Justification::centredLeft, true);
                }
            }

            g.setColour (Theme::laneSeparator);
            g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        }

        void resized() override
        {
            texts.clear();
            const int pad = 10, w = getWidth(), labelW = 50, rowH = 24, gapY = 6;
            const auto controlX = pad + labelW + 4, controlW = juce::jmax (40, w - pad - controlX - 1);
            int y = 8;

            const auto heading = [&] (const juce::String& title)
            {
                y += 4;
                texts.add ({ title, { pad, y, w - 2 * pad - 1, 14 }, true, false, false });
                y += 18;
            };

            const auto labelled = [&] (const juce::String& label, juce::Component& c)
            {
                texts.add ({ label, { pad, y, labelW, rowH }, false, true, false });
                c.setBounds (controlX, y, controlW, rowH);
                y += rowH + gapY;
            };

            const auto valueRow = [&] (const juce::String& label, int& index)
            {
                texts.add ({ label, { pad, y, labelW, 20 }, false, true, false });
                index = texts.size();
                texts.add ({ {}, { controlX, y, controlW, 20 }, false, false, false });
                y += 20 + 4;
            };

            heading ("Clip");
            labelled ("Name", nameEditor);
            valueRow ("Start", startIndex);
            valueRow ("Length", lengthIndex);
            loopButton.setBounds (pad, y, w - 2 * pad - 1, rowH);
            y += rowH + gapY;

            heading ("Scale");
            labelled ("Key", owner.scaleRootBox);
            labelled ("Scale", owner.scaleTypeBox);
            owner.followKeyButton.setBounds (pad, y, w - 2 * pad - 1, rowH);
            y += rowH + gapY;
            owner.highlightButton.setBounds (pad, y, w - 2 * pad - 1, rowH);
            y += rowH + gapY;
            owner.foldButton.setBounds (pad, y, w - 2 * pad - 1, rowH);
            y += rowH + gapY;
            hintIndex = texts.size();
            texts.add ({ {}, { pad, y, w - 2 * pad - 1, 40 }, false, true, true });
            y += 44;

            heading ("Selected");
            selectedIndex = texts.size();
            texts.add ({ {}, { pad, y, w - 2 * pad - 1, 16 }, false, false, false });
            y += 20;
            labelled ("Velocity", selVelocity);
            labelled ("Length", selLength);

            heading ("New notes");
            labelled ("Velocity", newVelocity);
            labelled ("Length", newLength);

            jassert (y + 8 <= contentHeight);
            refresh();
        }

    private:
        struct Text { juce::String text; juce::Rectangle<int> bounds; bool heading, dim, wrap; };

        PianoRollView& owner;
        juce::TextEditor nameEditor;
        IconButton loopButton { "Loop the clip", Icon::loop, "Loop clip" };
        juce::Slider selVelocity, selLength, newVelocity, newLength;
        juce::Array<Text> texts;
        int startIndex = -1, lengthIndex = -1, hintIndex = -1, selectedIndex = -1;

        struct Gesture { bool dragging = false, started = false; } gestures[4];

        void setText (int index, const juce::String& text)
        {
            if (index >= 0 && index < texts.size())
                texts.getReference (index).text = text;
        }

        void refreshName (bool force)
        {
            if (auto* clip = owner.getClip())
                if (force || ! nameEditor.hasKeyboardFocus (true))
                    nameEditor.setText (clip->getName(), false);
        }

        void commitName()
        {
            auto* clip = owner.getClip();

            if (clip == nullptr)
                return;

            const auto name = nameEditor.getText().trim();

            if (name.isEmpty() || name == clip->getName())
            {
                nameEditor.setText (clip->getName(), false);
                return;
            }

            owner.report (owner.session.renameClip (*clip, name), "Could not rename the clip");
            owner.repaintAll();
        }

        /** A bar you drag (one undo step per drag) or click to type in. */
        void setupSlider (juce::Slider& s, int gestureIndex, double lo, double hi, double step, const juce::String& tip,
                          std::function<void (double, bool)> apply)
        {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 20);
            s.setRange (lo, hi, step);
            s.setNumDecimalPlacesToDisplay (0);
            s.setTextBoxIsEditable (true);
            s.setScrollWheelEnabled (false);   // the wheel scrolls the inspector instead
            s.setTooltip (tip);
            s.setColour (juce::Slider::backgroundColourId, Theme::display);
            s.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.35f));
            s.setColour (juce::Slider::textBoxTextColourId, Theme::text);
            s.setColour (juce::Slider::textBoxOutlineColourId, Theme::border);
            s.onDragStart = [this, gestureIndex] { gestures[gestureIndex].dragging = true; gestures[gestureIndex].started = false; };
            s.onDragEnd = [this, gestureIndex] { gestures[gestureIndex].dragging = false; };
            s.onValueChange = [this, gestureIndex, &s, apply]
            {
                auto& gesture = gestures[gestureIndex];

                if (! gesture.dragging)
                    gesture.started = false;   // a typed value is its own undo step

                apply (s.getValue(), gesture.started);
                gesture.started = true;
            };
            addAndMakeVisible (s);
        }
    };

    //==============================================================================
    PianoRollView::PianoRollView (Session& s) : session (s)
    {
        keyboard = std::make_unique<Keyboard> (*this);
        grid = std::make_unique<Grid> (*this);
        ruler = std::make_unique<Ruler> (*this);
        velocityLane = std::make_unique<VelocityLane> (*this);
        rebuildRows();

        for (auto* c : std::initializer_list<juce::Component*> { keyboard.get(), grid.get(), ruler.get(), velocityLane.get(), &horizontalScroll, &verticalScroll, &inspectorViewport })
            addChildComponent (c);

        horizontalScroll.addListener (this);
        verticalScroll.addListener (this);
        horizontalScroll.setAutoHide (false);
        verticalScroll.setAutoHide (false);

        // The inspector scrolls (the panel can be as short as 150 px)
        inspector = std::make_unique<Inspector> (*this);
        inspectorViewport.setViewedComponent (inspector.get(), false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setScrollBarThickness (Theme::scrollBarSize);

        // Tools: one is always on
        drawTool.setTooltip ("Draw (1): click to add a note, drag to set how long it is. Shift+drag selects.");
        selectTool.setTooltip ("Select (2): click and drag to select and move notes. Double-click adds or deletes a note.");
        eraseTool.setTooltip ("Erase (3): click or drag across notes to delete them");
        drawTool.onClick = [this] { setTool (Tool::draw); };
        selectTool.onClick = [this] { setTool (Tool::select); };
        eraseTool.onClick = [this] { setTool (Tool::erase); };
        setTool (Tool::draw);

        for (int i = 0; i < (int) std::size (gridChoices); ++i)
            gridBox.addItem (gridNames[i], i + 1);

        for (int i = 0; i < (int) std::size (strengthChoices); ++i)
            strengthBox.addItem (strengthNames[i], i + 1);

        for (int i = 0; i < (int) std::size (swingChoices); ++i)
            swingBox.addItem (swingNames[i], i + 1);

        for (int i = 0; i < 12; ++i)
            scaleRootBox.addItem (rootNames[i], i + 1);

        for (int i = 0; i < (int) std::size (scaleNames); ++i)
            scaleTypeBox.addItem (scaleNames[i], i + 1);

        gridBox.setSelectedItemIndex (defaultGridIndex, juce::dontSendNotification);
        strengthBox.setSelectedItemIndex (0, juce::dontSendNotification);
        swingBox.setSelectedItemIndex (0, juce::dontSendNotification);
        scaleRootBox.setSelectedItemIndex (0, juce::dontSendNotification);
        scaleTypeBox.setSelectedItemIndex (0, juce::dontSendNotification);
        gridBeats = newNoteBeats = gridChoices[defaultGridIndex];

        gridBox.setTooltip ("Grid size: where notes snap, and how long new notes are. T means triplets.");
        strengthBox.setTooltip ("Quantise strength: how far notes move toward the grid (100% puts them right on it)");
        swingBox.setTooltip ("Swing: delays every second grid line when you quantise (67% is a triplet feel)");
        scaleRootBox.setTooltip ("The key's root note");
        scaleTypeBox.setTooltip ("The key's scale (it doesn't stop you using other notes)");
        gridBox.onChange = [this] { setGridIndex (gridBox.getSelectedItemIndex()); };
        strengthBox.onChange = [this] { setStrengthIndex (strengthBox.getSelectedItemIndex()); };
        swingBox.onChange = [this] { setSwingIndex (swingBox.getSelectedItemIndex()); };
        scaleRootBox.onChange = [this] { setFollowSongKey (false); setScaleRoot (scaleRootBox.getSelectedItemIndex()); };
        scaleTypeBox.onChange = [this] { setFollowSongKey (false); setScaleType (scaleTypeBox.getSelectedItemIndex()); };
        followKeyButton.setTooltip ("Use the song's key (set in the transport or the tempo lane). Picking a key here turns this off.");
        followKeyButton.setToggleState (true, juce::dontSendNotification);
        followKeyButton.onClick = [this] { setFollowSongKey (! followSongKey); };

        snapButton.setTooltip ("Snap notes to the grid when adding, moving and resizing (hold Alt to ignore it for one drag)");
        snapButton.setToggleState (true, juce::dontSendNotification);
        snapButton.onClick = [this] { setSnap (! snapEnabled); };

        quantiseButton.setTooltip ("Quantise (Q): pull note starts onto the grid, by the strength and swing set here. Acts on the selected notes, or all of them.");
        quantiseButton.onClick = [this] { quantiseNotes(); };

        followButton.setTooltip ("Follow the playhead: the view scrolls with the song while it plays");
        followButton.onClick = [this] { setFollowPlayhead (! followPlayhead); };
        fitButton.setTooltip ("Fit the view to the clip's notes (F)");
        fitButton.onClick = [this] { fitView(); };
        zoomOutButton.setTooltip ("Zoom out (Ctrl+wheel)");
        zoomInButton.setTooltip ("Zoom in (Ctrl+wheel)");
        zoomOutButton.onClick = [this] { zoomTime (0.8, grid->getWidth() * 0.5); repaintAll(); };
        zoomInButton.onClick = [this] { zoomTime (1.25, grid->getWidth() * 0.5); repaintAll(); };
        inspectorButton.setTooltip ("Show or hide the inspector");
        inspectorButton.onClick = [this] { inspectorOverride = ! isInspectorShown(); resized(); };

        highlightButton.setTooltip ("Light up the rows that are in the key");
        highlightButton.setToggleState (true, juce::dontSendNotification);
        highlightButton.onClick = [this] { setHighlightScale (! highlightScale); };
        foldButton.setTooltip ("Show only the rows in the key (and any row that already has a note). Needs a scale.");
        foldButton.onClick = [this] { setFoldToScale (! foldToScale); };

        for (auto* c : std::initializer_list<juce::Component*> { &gridBox, &strengthBox, &swingBox, &scaleRootBox, &scaleTypeBox, &followKeyButton, &drawTool, &selectTool, &eraseTool,
                                                                 &snapButton, &quantiseButton, &followButton, &fitButton, &zoomOutButton, &zoomInButton,
                                                                 &inspectorButton, &highlightButton, &foldButton })
        {
            c->setWantsKeyboardFocus (false);

            if (c != &scaleRootBox && c != &scaleTypeBox && c != &followKeyButton && c != &highlightButton && c != &foldButton)   // those live in the inspector
                addChildComponent (c);
        }

        for (auto* box : { &gridBox, &strengthBox, &swingBox, &scaleRootBox, &scaleTypeBox })
            box->setScrollWheelEnabled (false);

        session.addListener (this);
        startTimerHz (30);
    }

    PianoRollView::~PianoRollView()
    {
        session.removeListener (this);
    }

    void PianoRollView::sessionSongClosing()
    {
        // Clip IDs restart in every song: never let this one find a clip in the next song
        clipID = {};
        selected.clear();
        songWasClosed = true;   // the views are reset on the next tick, not from inside the notification
    }

    //==============================================================================
    te::MidiClip* PianoRollView::getClip() const
    {
        auto* edit = session.getEdit();

        if (edit == nullptr || ! clipID.isValid())
            return nullptr;

        auto* clip = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, clipID));
        return clip != nullptr && clip->getClipTrack() != nullptr ? clip : nullptr;
    }

    void PianoRollView::pruneSelection()
    {
        auto* clip = getClip();

        if (clip == nullptr)
        {
            selected.clear();
            return;
        }

        const auto& notes = clip->getSequence().getNotes();
        selected.removeIf ([&notes] (te::MidiNote* n) { return ! notes.contains (n); });
    }

    void PianoRollView::showClip (te::MidiClip* clip)
    {
        clipID = clip != nullptr ? clip->itemID : te::EditItemID();
        selected.clear();
        lastSignature = 0;
        const bool has = clip != nullptr;
        rebuildRows();
        applySongKey();

        for (auto* c : std::initializer_list<juce::Component*> { keyboard.get(), grid.get(), ruler.get(), velocityLane.get(), &horizontalScroll, &verticalScroll })
            c->setVisible (has);

        resized();   // the toolbar and the inspector come and go with the clip

        if (has)
        {
            // Start at the clip's left edge, centred on its notes (or middle C)
            viewStartBeat = juce::jmax (0.0, clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats() - 0.5);
            const auto& notes = clip->getSequence().getNotes();
            int sum = 0;

            for (auto* n : notes)
                sum += n->getNoteNumber();

            const auto centre = notes.isEmpty() ? 60 : sum / notes.size();
            scrollY = pitchToRow (centre) * rowHeight - grid->getHeight() / 2;
            clampView();
        }

        repaintAll();
    }

    juce::int64 PianoRollView::makeSignature (te::MidiClip& clip) const
    {
        juce::int64 h = clip.getSequence().getNotes().size();
        const auto mix = [&h] (double v) { h = h * 1000003 + (juce::int64) std::llround (v * 9600.0); };

        for (auto* n : clip.getSequence().getNotes())
        {
            mix (n->getStartBeat().inBeats());
            mix (n->getLengthBeats().inBeats());
            mix (n->getNoteNumber() * 128.0 + n->getVelocity());
        }

        mix (clip.getPosition().getStart().inSeconds());
        mix (clip.getPosition().getEnd().inSeconds());
        mix (clip.getPosition().getOffset().inSeconds());
        mix ((double) clip.getColour().getARGB());
        mix (session.getContentStartInSongBeats (clip));   // a tempo change moves the bar lines
        mix (clip.isLooping() ? 1.0 : 0.0);
        const auto key = session.getKeyAt (clip.getPosition().getStart());   // the song's key and bars
        mix (key.root * 2.0 + (key.minor ? 1.0 : 0.0));
        mix (session.getBeatsPerBarAt (clip.getPosition().getStart()));
        mix ((double) session.getTimeSigMarkers().size());

        if (auto* edit = session.getEdit())   // any tempo or time signature change anywhere moves the bar lines
            h = h * 1000003 + (juce::int64) edit->tempoSequence.getInternalSequence().hash();
        h = h * 1000003 + clip.getName().hashCode64();     // renamed elsewhere: the inspector follows
        return h;
    }

    void PianoRollView::timerCallback()
    {
        if (std::exchange (songWasClosed, false))
            showClip (nullptr);

        if (! isShowing())
            return;

        // Follow the selection: one selected MIDI clip becomes the clip being edited
        const auto selectedClips = session.getSelectedClips();
        auto* wanted = selectedClips.size() == 1 ? dynamic_cast<te::MidiClip*> (selectedClips.getFirst()) : nullptr;
        auto* clip = getClip();

        if (wanted != nullptr && wanted != clip)
        {
            showClip (wanted);
            clip = wanted;
        }
        else if (clip == nullptr && clipID.isValid())
        {
            showClip (nullptr);   // it was deleted
        }

        if (clip == nullptr)
            return;

        if (const auto signature = makeSignature (*clip); signature != lastSignature)
        {
            lastSignature = signature;   // notes changed (an edit, undo, recording): redraw
            pruneSelection();
            applySongKey();
            repaintAll();
        }

        // The playhead: repaint just the strips it left and entered
        const auto beat = clip->getContentBeatAtTime (session.getPosition()).inBeats();

        if (followPlayhead && session.isPlaying())
        {
            const auto x = beatToX (beat);

            if (x < 0.0 || x > grid->getWidth() - 24.0)
            {
                viewStartBeat = juce::jmax (0.0, beat - 0.1 * grid->getWidth() / pixelsPerBeat);
                clampView();
                repaintAll();
            }
        }

        if (beat != lastPlayheadBeat)
        {
            for (auto b : { lastPlayheadBeat, beat })
            {
                grid->repaint (juce::roundToInt (beatToX (b)) - 1, 0, 3, grid->getHeight());
                ruler->repaint (juce::roundToInt (beatToX (b)) - 1, 0, 3, ruler->getHeight());
            }

            lastPlayheadBeat = beat;
        }
    }

    void PianoRollView::report (bool ok, const juce::String& title)
    {
        if (! ok && onProblem)
            onProblem (title, session.getLastError());
    }

    void PianoRollView::returnFocusToMainWindow()
    {
        // Typing in the inspector took the keyboard: hand it back so Space, D and the rest work again
        for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        {
            if (p->getWantsKeyboardFocus())
            {
                p->grabKeyboardFocus();
                return;
            }
        }

        juce::Component::unfocusAllComponents();
    }

    //==============================================================================
    bool PianoRollView::hasEditFocus() const
    {
        return focusWatcher.hasFocus() && isShowing() && getClip() != nullptr;
    }

    bool PianoRollView::deleteSelection()
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip == nullptr || selected.isEmpty())
            return false;

        const auto notes = selected;

        if (session.deleteMidiNotes (*clip, notes))   selected.clear();
        else                                          report (false, "Could not delete the notes");

        repaintAll();
        return true;
    }

    void PianoRollView::selectAll()
    {
        if (auto* clip = getClip())
            selected = clip->getSequence().getNotes();

        repaintAll();
    }

    void PianoRollView::deselectAll()
    {
        if (grid->cancelGesture())   // Esc in the middle of a drag: just abandon the drag
            return;

        selected.clear();
        repaintAll();
    }

    void PianoRollView::invertSelection()
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return;

        pruneSelection();
        Session::NoteList inverted;

        for (auto* n : clip->getSequence().getNotes())
            if (! selected.contains (n))
                inverted.add (n);

        selected = inverted;
        repaintAll();
    }

    void PianoRollView::nudge (int direction)
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip != nullptr && ! selected.isEmpty())
            session.moveMidiNotes (*clip, selected, direction * gridBeats, 0);   // refused at beat 0: nothing to report
    }

    void PianoRollView::copyNotes()
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip != nullptr && ! selected.isEmpty())
            report (session.copyMidiNotes (*clip, selected), "Could not copy the notes");
    }

    void PianoRollView::cutNotes()
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip == nullptr || selected.isEmpty())
            return;

        const auto notes = selected;

        if (session.cutMidiNotes (*clip, notes))   selected.clear();
        else                                       report (false, "Could not cut the notes");

        repaintAll();
    }

    void PianoRollView::pasteNotes()
    {
        auto* clip = getClip();

        if (clip == nullptr || ! session.hasNoteClipboard())
            return;

        // At the playhead if it is inside the clip, otherwise at the left edge of what you can see
        const auto clipStart = clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats();
        const auto clipEnd = clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats();
        const auto playhead = clip->getContentBeatAtTime (session.getPosition()).inBeats();
        const auto at = playhead >= clipStart && playhead < clipEnd ? playhead : juce::jmax (clipStart, viewStartBeat);
        pasteAt (snapBeat (*clip, at, true));
    }

    void PianoRollView::pasteAt (double contentBeat)
    {
        auto* clip = getClip();

        if (clip == nullptr || ! session.hasNoteClipboard())
            return;

        auto added = session.pasteMidiNotes (*clip, contentBeat);

        if (added.isEmpty())
        {
            report (false, "Could not paste the notes");
            return;
        }

        selected = added;
        repaintAll();
    }

    void PianoRollView::duplicateNotes()
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip == nullptr || selected.isEmpty())
            return;

        auto added = session.duplicateMidiNotes (*clip, selected);

        if (added.isEmpty())
        {
            report (false, "Could not duplicate the notes");
            return;
        }

        selected = added;
        repaintAll();
    }

    void PianoRollView::transpose (int semitones)
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip == nullptr || selected.isEmpty())
            return;

        if (session.moveMidiNotes (*clip, selected, 0.0, semitones))   // refused at the top or bottom: nothing to report
            session.auditionNote (*clip, selected.getFirst()->getNoteNumber(), selected.getFirst()->getVelocity(), true);

        repaintAll();
    }

    void PianoRollView::quantiseNotes()
    {
        if (auto* clip = getClip())
        {
            pruneSelection();
            report (session.quantiseMidiNotes (*clip, selected, gridBeats, quantiseStrength, quantiseSwing), "Could not quantise");
            repaintAll();
        }
    }

    void PianoRollView::legatoNotes()
    {
        if (auto* clip = getClip())
        {
            pruneSelection();
            report (session.legatoMidiNotes (*clip, selected), "Could not join up the notes");
            repaintAll();
        }
    }

    void PianoRollView::setSelectedVelocity (int velocity)
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip != nullptr && ! selected.isEmpty())
        {
            report (session.setMidiNoteVelocity (*clip, selected, velocity), "Could not change the notes");
            repaintAll();
        }
    }

    void PianoRollView::setSelectedLengthToGrid()
    {
        auto* clip = getClip();
        pruneSelection();

        if (clip != nullptr && ! selected.isEmpty())
        {
            report (session.setMidiNoteLength (*clip, selected, gridBeats), "Could not change the notes");
            repaintAll();
        }
    }

    void PianoRollView::fitView()
    {
        auto* clip = getClip();

        if (clip == nullptr || grid->getWidth() <= 0 || grid->getHeight() <= 0)
            return;

        auto first = clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats();
        auto last = clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats();
        int lowest = 127, highest = 0;
        bool any = false;

        for (auto* n : clip->getSequence().getNotes())
        {
            first = juce::jmin (first, n->getStartBeat().inBeats());
            last = juce::jmax (last, n->getEndBeat().inBeats());
            lowest = juce::jmin (lowest, n->getNoteNumber());
            highest = juce::jmax (highest, n->getNoteNumber());
            any = true;
        }

        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, (grid->getWidth() - 24.0) / juce::jmax (1.0, last - first));
        viewStartBeat = juce::jmax (0.0, first - 12.0 / pixelsPerBeat);

        if (any)
        {
            rebuildRows();
            const auto rowsNeeded = juce::jmax (6, pitchToRow (lowest) - pitchToRow (highest) + 3);
            rowHeight = juce::jlimit (minRowHeight, maxRowHeight, grid->getHeight() / rowsNeeded);
            scrollY = juce::jmax (0, (pitchToRow (highest) - 1) * rowHeight);
        }

        clampView();
        repaintAll();
    }

    void PianoRollView::setTool (Tool newTool)
    {
        tool = newTool;
        drawTool.setToggleState (tool == Tool::draw, juce::dontSendNotification);
        selectTool.setToggleState (tool == Tool::select, juce::dontSendNotification);
        eraseTool.setToggleState (tool == Tool::erase, juce::dontSendNotification);
    }

    void PianoRollView::setGridIndex (int index)
    {
        index = juce::jlimit (0, (int) std::size (gridChoices) - 1, index);
        gridBeats = newNoteBeats = gridChoices[index];

        if (index == 0)   // "1 bar": as long as a bar is where the clip starts
            if (auto* clip = getClip())
                gridBeats = newNoteBeats = session.getBeatsPerBarAt (clip->getPosition().getStart());

        gridBox.setSelectedItemIndex (index, juce::dontSendNotification);
        repaintAll();
    }

    void PianoRollView::setStrengthIndex (int index)
    {
        index = juce::jlimit (0, (int) std::size (strengthChoices) - 1, index);
        quantiseStrength = strengthChoices[index];
        strengthBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }

    void PianoRollView::setSwingIndex (int index)
    {
        index = juce::jlimit (0, (int) std::size (swingChoices) - 1, index);
        quantiseSwing = swingChoices[index];
        swingBox.setSelectedItemIndex (index, juce::dontSendNotification);
    }

    void PianoRollView::setScaleRoot (int root)
    {
        scaleRoot = juce::jlimit (0, 11, root);
        scaleRootBox.setSelectedItemIndex (scaleRoot, juce::dontSendNotification);
        repaintAll();
    }

    void PianoRollView::setScaleType (int type)
    {
        scaleType = juce::jlimit (0, (int) std::size (scaleNames) - 1, type);
        scaleTypeBox.setSelectedItemIndex (scaleType, juce::dontSendNotification);
        repaintAll();
    }

    void PianoRollView::setFollowSongKey (bool shouldFollow)
    {
        followSongKey = shouldFollow;
        followKeyButton.setToggleState (shouldFollow, juce::dontSendNotification);
        applySongKey();
        repaintAll();
    }

    void PianoRollView::applySongKey()
    {
        if (gridBox.getSelectedItemIndex() == 0)   // a "1 bar" grid follows the time signature too
            if (auto* clip = getClip())
                gridBeats = session.getBeatsPerBarAt (clip->getPosition().getStart());

        auto* clip = getClip();

        if (! followSongKey || clip == nullptr)
            return;

        // The key where the clip starts
        const auto key = session.getKeyAt (clip->getPosition().getStart());
        const auto root = key.root >= 0 ? key.root : scaleRoot;
        const auto type = key.root >= 0 ? (key.minor ? 2 : 1) : 0;

        if (root != scaleRoot || type != scaleType)
        {
            scaleRoot = root;
            scaleType = type;
            scaleRootBox.setSelectedItemIndex (scaleRoot, juce::dontSendNotification);
            scaleTypeBox.setSelectedItemIndex (scaleType, juce::dontSendNotification);
            rebuildRows();
        }
    }

    void PianoRollView::setHighlightScale (bool shouldHighlight)
    {
        highlightScale = shouldHighlight;
        highlightButton.setToggleState (shouldHighlight, juce::dontSendNotification);
        repaintAll();
    }

    void PianoRollView::setFoldToScale (bool shouldFold)
    {
        // Keep whatever is in the middle of the view in the middle
        const auto centrePitch = yToPitch (grid->getHeight() / 2);
        foldToScale = shouldFold;
        foldButton.setToggleState (shouldFold, juce::dontSendNotification);
        rebuildRows();
        scrollY = pitchToRow (centrePitch) * rowHeight - grid->getHeight() / 2;
        clampView();
        repaintAll();
    }

    void PianoRollView::setFollowPlayhead (bool shouldFollow)
    {
        followPlayhead = shouldFollow;
        followButton.setToggleState (shouldFollow, juce::dontSendNotification);
    }

    void PianoRollView::setSnap (bool shouldSnap)
    {
        snapEnabled = shouldSnap;
        snapButton.setToggleState (shouldSnap, juce::dontSendNotification);
    }

    bool PianoRollView::handleKey (const juce::KeyPress& key)
    {
        if (! hasEditFocus())
            return false;

        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();

        if (! mods.isCommandDown() && ! mods.isAltDown())
        {
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                transpose ((code == juce::KeyPress::upKey ? 1 : -1) * (mods.isShiftDown() ? 12 : 1));
                return true;
            }

            if (mods.isShiftDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
            {
                auto* clip = getClip();
                pruneSelection();

                if (clip != nullptr && ! selected.isEmpty())
                    session.moveMidiNotes (*clip, selected, code == juce::KeyPress::leftKey ? -1.0 : 1.0, 0);

                return true;
            }
        }

        if (key == juce::KeyPress ('1'))    { setTool (Tool::draw);    return true; }
        if (key == juce::KeyPress ('2'))    { setTool (Tool::select);  return true; }
        if (key == juce::KeyPress ('3'))    { setTool (Tool::erase);   return true; }
        if (key == juce::KeyPress ('q'))    { quantiseNotes();         return true; }
        if (key == juce::KeyPress ('f'))    { fitView();               return true; }

        return false;
    }

    //==============================================================================
    void PianoRollView::showContextMenu (double contentBeatAtClick)
    {
        pruneSelection();
        const bool hasSelection = ! selected.isEmpty();
        juce::PopupMenu menu;

        const auto add = [] (juce::PopupMenu& m, int id, const juce::String& text, const juce::String& shortcut = {}, bool enabled = true, bool ticked = false)
        {
            juce::PopupMenu::Item item (text);
            item.itemID = id;
            item.isEnabled = enabled;
            item.isTicked = ticked;
            item.shortcutKeyDescription = shortcut;
            m.addItem (item);
        };

        add (menu, menuDraw, "Draw tool", "1", true, tool == Tool::draw);
        add (menu, menuSelect, "Select tool", "2", true, tool == Tool::select);
        add (menu, menuErase, "Erase tool", "3", true, tool == Tool::erase);
        menu.addSeparator();

        add (menu, menuCut, "Cut", "Ctrl+X", hasSelection);
        add (menu, menuCopy, "Copy", "Ctrl+C", hasSelection);
        add (menu, menuPaste, "Paste", "Ctrl+V", session.hasNoteClipboard());
        add (menu, menuDuplicate, "Duplicate", "Ctrl+D", hasSelection);
        add (menu, menuDelete, "Delete", "Del", hasSelection);
        menu.addSeparator();

        add (menu, menuSelectAll, "Select all", "Ctrl+A");
        add (menu, menuSelectNone, "Select none", "Esc", hasSelection);
        add (menu, menuInvert, "Invert selection");
        menu.addSeparator();

        juce::PopupMenu transposeMenu, velocityMenu, gridMenu, strengthMenu, swingMenu, rootMenu, scaleMenu, viewMenu;
        add (transposeMenu, menuUp, "Up a semitone", "Up", hasSelection);
        add (transposeMenu, menuDown, "Down a semitone", "Down", hasSelection);
        add (transposeMenu, menuOctaveUp, "Up an octave", "Shift+Up", hasSelection);
        add (transposeMenu, menuOctaveDown, "Down an octave", "Shift+Down", hasSelection);
        menu.addSubMenu ("Transpose", transposeMenu, hasSelection);

        for (int i = 0; i < (int) std::size (velocityChoices); ++i)
            add (velocityMenu, menuVelocity + i, velocityNames[i], {}, hasSelection);

        menu.addSubMenu ("Set velocity", velocityMenu, hasSelection);
        add (menu, menuQuantise, "Quantise", "Q");
        add (menu, menuLegato, "Legato (join notes up)");
        add (menu, menuLengthToGrid, "Set length to the grid size", {}, hasSelection);
        menu.addSeparator();

        for (int i = 0; i < (int) std::size (gridChoices); ++i)
            add (gridMenu, menuGrid + i, gridNames[i], {}, true, std::abs (gridBeats - gridChoices[i]) < 1.0e-9);

        for (int i = 0; i < (int) std::size (strengthChoices); ++i)
            add (strengthMenu, menuStrength + i, strengthNames[i], {}, true, std::abs (quantiseStrength - strengthChoices[i]) < 1.0e-9);

        for (int i = 0; i < (int) std::size (swingChoices); ++i)
            add (swingMenu, menuSwing + i, swingNames[i], {}, true, std::abs (quantiseSwing - swingChoices[i]) < 1.0e-9);

        for (int i = 0; i < 12; ++i)
            add (rootMenu, menuRoot + i, rootNames[i], {}, true, scaleRoot == i);

        for (int i = 0; i < (int) std::size (scaleNames); ++i)
            add (scaleMenu, menuScaleType + i, scaleNames[i], {}, true, scaleType == i);

        menu.addSubMenu ("Grid size", gridMenu);
        add (menu, menuSnap, "Snap to the grid", {}, true, snapEnabled);
        menu.addSubMenu ("Quantise strength", strengthMenu);
        menu.addSubMenu ("Swing", swingMenu);
        menu.addSeparator();

        menu.addSubMenu ("Key", rootMenu);
        menu.addSubMenu ("Scale", scaleMenu);
        add (menu, menuFollowKey, "Follow the song's key", {}, true, followSongKey);
        add (menu, menuHighlight, "Highlight the scale", {}, true, highlightScale);
        add (menu, menuFold, "Fold to the scale", {}, scaleType != 0, foldToScale);
        menu.addSeparator();

        add (viewMenu, menuZoomIn, "Zoom in", "Ctrl+wheel");
        add (viewMenu, menuZoomOut, "Zoom out", "Ctrl+wheel");
        add (viewMenu, menuTaller, "Taller rows", "Alt+wheel");
        add (viewMenu, menuShorter, "Shorter rows", "Alt+wheel");
        add (viewMenu, menuFit, "Fit to the notes", "F");
        add (viewMenu, menuFollow, "Follow the playhead", {}, true, followPlayhead);
        add (viewMenu, menuInspector, "Inspector", {}, true, isInspectorShown());
        menu.addSubMenu ("View", viewMenu);

        juce::Component::SafePointer<PianoRollView> safe (this);
        menu.showMenuAsync ({}, [safe, contentBeatAtClick] (int result)
        {
            if (safe != nullptr && result != 0)
                safe->handleMenuResult (result, contentBeatAtClick);
        });
    }

    void PianoRollView::handleMenuResult (int result, double contentBeatAtClick)
    {
        const auto inList = [result] (int base, int count) { return result >= base && result < base + count; };

        if (inList (menuGrid, (int) std::size (gridChoices)))                     { setGridIndex (result - menuGrid); return; }
        if (inList (menuStrength, (int) std::size (strengthChoices)))             { setStrengthIndex (result - menuStrength); return; }
        if (inList (menuSwing, (int) std::size (swingChoices)))                   { setSwingIndex (result - menuSwing); return; }
        if (inList (menuRoot, 12))                                                { setFollowSongKey (false); setScaleRoot (result - menuRoot); return; }
        if (inList (menuScaleType, (int) std::size (scaleNames)))                 { setFollowSongKey (false); setScaleType (result - menuScaleType); return; }
        if (inList (menuVelocity, (int) std::size (velocityChoices)))             { setSelectedVelocity (velocityChoices[result - menuVelocity]); return; }

        switch (result)
        {
            case menuDraw:          setTool (Tool::draw); break;
            case menuSelect:        setTool (Tool::select); break;
            case menuErase:         setTool (Tool::erase); break;
            case menuCut:           cutNotes(); break;
            case menuCopy:          copyNotes(); break;
            case menuPaste:         pasteAt (contentBeatAtClick); break;   // where you right-clicked
            case menuDuplicate:     duplicateNotes(); break;
            case menuDelete:        deleteSelection(); break;
            case menuSelectAll:     selectAll(); break;
            case menuSelectNone:    selected.clear(); repaintAll(); break;
            case menuInvert:        invertSelection(); break;
            case menuUp:            transpose (1); break;
            case menuDown:          transpose (-1); break;
            case menuOctaveUp:      transpose (12); break;
            case menuOctaveDown:    transpose (-12); break;
            case menuQuantise:      quantiseNotes(); break;
            case menuLegato:        legatoNotes(); break;
            case menuLengthToGrid:  setSelectedLengthToGrid(); break;
            case menuSnap:          setSnap (! snapEnabled); break;
            case menuFollowKey:     setFollowSongKey (! followSongKey); break;
            case menuHighlight:     setHighlightScale (! highlightScale); break;
            case menuFold:          setFoldToScale (! foldToScale); break;
            case menuFollow:        setFollowPlayhead (! followPlayhead); break;
            case menuInspector:     inspectorOverride = ! isInspectorShown(); resized(); break;
            case menuZoomIn:        zoomTime (1.25, grid->getWidth() * 0.5); repaintAll(); break;
            case menuZoomOut:       zoomTime (0.8, grid->getWidth() * 0.5); repaintAll(); break;
            case menuTaller:        setRowHeight (rowHeight + 2, grid->getHeight() / 2); repaintAll(); break;
            case menuShorter:       setRowHeight (rowHeight - 2, grid->getHeight() / 2); repaintAll(); break;
            case menuFit:           fitView(); break;
            default: break;
        }
    }

    //==============================================================================
    double PianoRollView::snapBeat (te::MidiClip& clip, double contentBeat, bool floorIt, bool bypassSnap) const
    {
        if (! snapEnabled || bypassSnap)
            return juce::jmax (0.0, contentBeat);

        // The song's grid, which may not line up with the clip's own beat 0
        const auto contentStart = session.getContentStartInSongBeats (clip);
        const auto cells = (contentBeat + contentStart) / gridBeats;
        auto snapped = (floorIt ? std::floor (cells + 1.0e-9) : std::round (cells)) * gridBeats - contentStart;

        while (snapped < -1.0e-9)   // tolerance: triplet grids give -1e-13 at the very start
            snapped += gridBeats;

        return juce::jmax (0.0, snapped);
    }

    bool PianoRollView::isInScale (int pitch) const
    {
        static const bool major[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
        static const bool minor[12] = { true, false, true, true, false, true, false, true, true, false, true, false };
        const auto degree = (pitch - scaleRoot + 120) % 12;
        return scaleType == 1 ? major[degree] : scaleType == 2 ? minor[degree] : true;
    }

    bool PianoRollView::isInspectorShown() const
    {
        return inspectorOverride.has_value() ? *inspectorOverride : getWidth() >= inspectorMinPanelWidth;
    }

    void PianoRollView::rebuildRows()
    {
        juce::BigInteger hasNote;

        if (auto* clip = getClip())
            for (auto* n : clip->getSequence().getNotes())
                hasNote.setBit (n->getNoteNumber());

        const bool fold = foldToScale && scaleType != 0;
        int direct[128];
        std::fill (std::begin (direct), std::end (direct), -1);
        rowPitches.clearQuick();

        for (int pitch = 127; pitch >= 0; --pitch)
        {
            if (! fold || isInScale (pitch) || hasNote[pitch])
            {
                direct[pitch] = rowPitches.size();
                rowPitches.add (pitch);
            }
        }

        // A pitch that isn't a row (only possible while a note is mid-move) uses the nearest row
        int below[128], above[128], last = -1;

        for (int p = 0; p < 128; ++p)    { if (direct[p] >= 0) last = p; below[p] = last; }
        last = -1;
        for (int p = 127; p >= 0; --p)   { if (direct[p] >= 0) last = p; above[p] = last; }

        for (int p = 0; p < 128; ++p)
        {
            if (direct[p] >= 0)
            {
                pitchRow[p] = direct[p];
            }
            else
            {
                const auto useBelow = below[p] >= 0 && (above[p] < 0 || p - below[p] <= above[p] - p);
                pitchRow[p] = direct[useBelow ? below[p] : above[p]];
            }
        }
    }

    void PianoRollView::zoomTime (double factor, double anchorX)
    {
        const auto anchorBeat = xToBeat (anchorX);
        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
        viewStartBeat = anchorBeat - anchorX / pixelsPerBeat;
        clampView();
    }

    void PianoRollView::setRowHeight (int newHeight, int anchorY)
    {
        const auto rowAtAnchor = (double) (anchorY + scrollY) / rowHeight;
        rowHeight = juce::jlimit (minRowHeight, maxRowHeight, newHeight);
        scrollY = juce::roundToInt (rowAtAnchor * rowHeight) - anchorY;
        clampView();
    }

    void PianoRollView::clampView()
    {
        viewStartBeat = juce::jmax (0.0, viewStartBeat);
        scrollY = juce::jlimit (0, juce::jmax (0, rowCount() * rowHeight - grid->getHeight()), scrollY);
        updateScrollBars();
    }

    void PianoRollView::updateScrollBars()
    {
        double lastBeat = 16.0;

        if (auto* clip = getClip())
        {
            lastBeat = juce::jmax (lastBeat, clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats());

            for (auto* n : clip->getSequence().getNotes())
                lastBeat = juce::jmax (lastBeat, n->getEndBeat().inBeats());
        }

        const auto visibleBeats = grid->getWidth() / pixelsPerBeat;
        horizontalScroll.setRangeLimits (0.0, juce::jmax (lastBeat + scrollPaddingBeats, viewStartBeat + visibleBeats), juce::dontSendNotification);
        horizontalScroll.setCurrentRange (viewStartBeat, visibleBeats, juce::dontSendNotification);
        verticalScroll.setRangeLimits (0.0, (double) rowCount() * rowHeight, juce::dontSendNotification);
        verticalScroll.setCurrentRange ((double) scrollY, (double) grid->getHeight(), juce::dontSendNotification);
    }

    void PianoRollView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        if (bar == &horizontalScroll)   viewStartBeat = juce::jmax (0.0, newRangeStart);
        else                            scrollY = juce::roundToInt (newRangeStart);

        repaintAll();
    }

    void PianoRollView::repaintAll()
    {
        rebuildRows();   // the notes decide which rows a folded view keeps
        clampView();
        keyboard->repaint();
        grid->repaint();
        ruler->repaint();
        velocityLane->repaint();
        inspector->refresh();
        repaint (toolbarArea);
    }

    //==============================================================================
    void PianoRollView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        auto* clip = getClip();

        if (clip == nullptr)
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawFittedText ("Select a MIDI clip to edit its notes here.\nDouble-click a MIDI clip, or an empty part of a track to make a new one.",
                              getLocalBounds().reduced (20), juce::Justification::centred, 3);
            return;
        }

        g.setColour (Theme::panel);
        g.fillRect (toolbarArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (toolbarArea.getBottom() - 1, (float) toolbarArea.getX(), (float) toolbarArea.getRight());

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize));

        for (auto& label : toolbarLabels)
            g.drawText (label.text, label.bounds, juce::Justification::centredRight, false);

        if (! titleArea.isEmpty())
        {
            auto title = titleArea;
            g.setColour (clip->getColour());
            g.fillRoundedRectangle (title.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat(), 2.0f);
            title.removeFromLeft (8);
            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize, true));
            g.drawText (clip->getName(), title, juce::Justification::centredLeft, true);
        }

        g.setColour (Theme::bar);
        g.fillRect (rulerCorner);
        g.fillRect (velocityLabelArea);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (rulerCorner.getBottom() - 1, (float) rulerCorner.getX(), (float) rulerCorner.getRight());
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize, true));
        g.drawText ("VELOCITY", velocityLabelArea.reduced (6, 0), juce::Justification::centredLeft, false);
    }

    /** Lays out the toolbar. When it is too narrow the least important items drop out, last first:
        the clip title, Fit, Follow, Swing, Strength. All of them are in the right-click menu. */
    void PianoRollView::layoutToolbar()
    {
        toolbarLabels.clear();
        titleArea = {};

        juce::Component* const all[] = { &drawTool, &selectTool, &eraseTool, &gridBox, &strengthBox, &swingBox, &snapButton, &quantiseButton,
                                         &followButton, &fitButton, &zoomOutButton, &zoomInButton, &inspectorButton };

        for (auto* c : all)
            c->setVisible (false);

        if (getClip() == nullptr)
            return;

        const auto h = Theme::buttonHeight - 2;
        const auto area = toolbarArea.reduced (10, 0);
        const auto labelFont = Theme::font (Theme::captionFontSize);
        const auto labelWidthFor = [&labelFont] (const char* t) { return t == nullptr ? 0 : juce::GlyphArrangement::getStringWidthInt (labelFont, t) + 6; };

        struct Item { juce::Component* c; int width; const char* label; int priority; int gapBefore; bool shown; };

        // Left to right, then (right cluster) right to left. Priority 0 is never dropped.
        std::vector<Item> left
        {
            { nullptr,        150,                           nullptr,    6, 0, true },   // the clip's title
            { &drawTool,      drawTool.getIdealWidth (h),    nullptr,    0, 12, true },
            { &selectTool,    selectTool.getIdealWidth (h),  nullptr,    0, 2, true },
            { &eraseTool,     eraseTool.getIdealWidth (h),   nullptr,    0, 2, true },
            { &gridBox,       66,                            "Grid",     0, 12, true },
            { &snapButton,    snapButton.getIdealWidth (h),  nullptr,    0, 6, true },
            { &quantiseButton, quantiseButton.getIdealWidth (h), nullptr, 0, 6, true },
            { &strengthBox,   64,                            "Strength", 2, 12, true },
            { &swingBox,      56,                            "Swing",    3, 8, true },
        };

        std::vector<Item> right
        {
            { &inspectorButton, inspectorButton.getIconOnlyWidth (h), nullptr, 0, 0, true },
            { &zoomInButton,    zoomInButton.getIconOnlyWidth (h),    nullptr, 0, 2, true },
            { &zoomOutButton,   zoomOutButton.getIconOnlyWidth (h),   nullptr, 0, 2, true },
            { &fitButton,       fitButton.getIconOnlyWidth (h),       nullptr, 5, 2, true },
            { &followButton,    followButton.getIconOnlyWidth (h),    nullptr, 4, 2, true },
        };

        const auto totalOf = [&] (const std::vector<Item>& items)
        {
            int total = 0;
            bool first = true;

            for (auto& i : items)
            {
                if (! i.shown)
                    continue;

                total += (first ? 0 : i.gapBefore) + labelWidthFor (i.label) + i.width;
                first = false;
            }

            return total;
        };

        while (totalOf (left) + totalOf (right) + 12 > area.getWidth())
        {
            Item* worst = nullptr;

            for (auto* list : { &left, &right })
                for (auto& i : *list)
                    if (i.shown && i.priority > 0 && (worst == nullptr || i.priority > worst->priority))
                        worst = &i;

            if (worst == nullptr)
                break;   // nothing left to drop: the rest is clipped

            worst->shown = false;
        }

        const auto bar = [&] (int x, int width) { return juce::Rectangle<int> (x, toolbarArea.getY() + (toolbarArea.getHeight() - h) / 2, width, h); };

        int x = area.getX();
        bool first = true;

        for (auto& i : left)
        {
            if (! i.shown)
                continue;

            if (! first)
                x += i.gapBefore;

            first = false;

            if (i.c == nullptr)   // the title takes whatever is left of its width after the rest has been placed
            {
                titleArea = { x, toolbarArea.getY(), i.width, toolbarArea.getHeight() };
                x += i.width;
                continue;
            }

            if (i.label != nullptr)
            {
                const auto lw = labelWidthFor (i.label);
                toolbarLabels.add ({ i.label, { x, toolbarArea.getY(), lw - 2, toolbarArea.getHeight() } });
                x += lw;
            }

            i.c->setBounds (bar (x, i.width));
            i.c->setVisible (true);
            x += i.width;
        }

        int rx = area.getRight();

        for (auto& i : right)
        {
            if (! i.shown)
                continue;

            rx -= i.width;
            i.c->setBounds (bar (rx, i.width));
            i.c->setVisible (true);
            rx -= i.gapBefore == 0 ? 6 : 2;
        }
    }

    void PianoRollView::resized()
    {
        auto r = getLocalBounds();
        const bool hasClip = getClip() != nullptr;
        const bool showInspector = isInspectorShown() && hasClip;
        inspectorButton.setToggleState (isInspectorShown(), juce::dontSendNotification);

        if (showInspector)
        {
            inspectorViewport.setBounds (r.removeFromLeft (inspectorWidth));
            inspector->setSize (inspectorViewport.getMaximumVisibleWidth(), Inspector::contentHeight);
        }

        inspectorViewport.setVisible (showInspector);

        toolbarArea = r.removeFromTop (toolbarHeight);
        layoutToolbar();

        auto rulerRow = r.removeFromTop (rulerHeight);
        rulerCorner = rulerRow.removeFromLeft (keyboardWidth);
        ruler->setBounds (rulerRow.withTrimmedRight (Theme::scrollBarSize));

        horizontalScroll.setBounds (r.removeFromBottom (Theme::scrollBarSize).withTrimmedLeft (keyboardWidth).withTrimmedRight (Theme::scrollBarSize));
        verticalScroll.setBounds (r.removeFromRight (Theme::scrollBarSize));

        auto bottom = r.removeFromBottom (juce::jmin (velocityHeight, r.getHeight() / 3));
        velocityLabelArea = bottom.removeFromLeft (keyboardWidth);
        velocityLane->setBounds (bottom);
        verticalScroll.setBounds (verticalScroll.getBounds().withBottom (bottom.getY()));

        keyboard->setBounds (r.removeFromLeft (keyboardWidth));
        grid->setBounds (r);
        clampView();
        repaint();
    }
}
