/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "../Shell/BrowserPanel.h"
#include "TimelineView.h"
#include "ClipView.h"
#include "Ruler.h"
#include "TempoLane.h"
#include "SectionsLane.h"
#include "TrackHeader.h"
#include "AutomationLane.h"
#include "../Theme.h"

namespace ez
{
    namespace
    {
        constexpr int refreshHz = 30;
        constexpr int structureCheckEveryTicks = 6;   // ~5 times a second
        constexpr double extraSecondsAfterSong = 30.0;
    }

    //==============================================================================
    /** Lane backgrounds, grid, clips, rubber-band selection, menus, file drops. */
    class TimelineView::Lanes : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                public juce::DragAndDropTarget,      // sounds from the browser
                                private ClipView::Host
    {
    public:
        Lanes (TimelineView& o) : owner (o), session (o.session), view (o.view) {}

        void rebuild()
        {
            clipViews.clear();
            autoLanes.clear();

            if (auto* edit = session.getEdit())
            {
                const auto tracks = te::getAudioTracks (*edit);

                for (int t = 0; t < tracks.size(); ++t)
                {
                    for (auto* clip : tracks[t]->getClips())
                        addAndMakeVisible (clipViews.add (new ClipView (session, view, *this, *clip, t)));

                    const auto lanes = session.getShownAutomationLanes (t);

                    for (int l = 0; l < (int) lanes.size(); ++l)
                    {
                        auto* lane = autoLanes.add (new AutomationLane (session, view, t, l, lanes[(size_t) l]));
                        lane->onProblem = [this] (const juce::String& m) { if (owner.onProblem) owner.onProblem ("Automation", m); };
                        addAndMakeVisible (lane);
                    }
                }
            }

            repaint();
        }

        void clear()
        {
            clipViews.clear();
            autoLanes.clear();
            repaint();
        }

        /** After zoom/scroll/track height: everything moves. */
        void layoutClips()
        {
            for (auto* c : clipViews)
                c->updateBounds();

            for (auto* lane : autoLanes)
            {
                // Which of its track's lanes this is: the order they were made in
                int index = 0;

                for (auto* other : autoLanes)
                {
                    if (other == lane)
                        break;

                    if (other->getTrackIndex() == lane->getTrackIndex())
                        ++index;
                }

                lane->setBounds (0, view.topOfLane (lane->getTrackIndex(), index), getWidth(), TimelineViewState::automationLaneHeight);
            }

            repaint();
        }

        /** Periodic: only clips that changed (e.g. by undo) move or redraw. */
        void refreshClips()
        {
            for (auto* c : clipViews)
                c->refreshIfChanged();

            for (auto* lane : autoLanes)
                lane->refreshIfChanged();
        }

        EditorTarget* getFocusedLane() const
        {
            for (auto* lane : autoLanes)
                if (lane->hasEditFocus())
                    return lane;

            return nullptr;
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::laneBackground);

            auto* edit = session.getEdit();

            if (edit == nullptr)
                return;

            const auto numTracks = session.getNumAudioTracks();

            // Every other lane a shade lighter, so a long row is easy to follow across the screen
            g.setColour (Theme::laneAlternate);

            for (int t = 1; t < numTracks; t += 2)
                g.fillRect (0, view.topOfTrack (t), getWidth(), view.rowHeight (t));

            if (view.focusedTrack >= 0 && view.focusedTrack < numTracks)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect (0, view.topOfTrack (view.focusedTrack), getWidth(), view.rowHeight (view.focusedTrack));
            }

            // Grid: beats faint, bars stronger
            const auto& tempo = edit->tempoSequence;
            const auto gridBeats = view.getGridBeats (session.getBpm());
            const auto gridBottom = (float) juce::jmax (getHeight(), 0);
            const auto clip = g.getClipBounds();

            const auto lines = session.getGridLines (tempo.toBeats (view.xToTime (clip.getX())).inBeats(),
                                                     tempo.toBeats (view.xToTime (clip.getRight())).inBeats(), gridBeats);

            // Every other bar a whisper lighter, so bars read as blocks when zoomed in
            {
                int previousBarX = -1, barNumber = 0;

                for (auto [beat, isBar] : lines)
                {
                    if (! isBar)
                        continue;

                    const auto x = view.timeToX (tempo.toTime (te::BeatPosition::fromBeats (beat)));
                    barNumber = session.getBarAt (beat + 1.0e-7).first;

                    if (previousBarX >= 0 && x - previousBarX >= 24 && (barNumber % 2) == 0)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.012f));
                        g.fillRect (previousBarX, 0, x - previousBarX, getHeight());
                    }

                    previousBarX = x;
                }
            }

            for (auto [beat, isBar] : lines)
            {
                g.setColour (isBar ? Theme::gridBar : Theme::gridBeat);
                g.drawVerticalLine (view.timeToX (tempo.toTime (te::BeatPosition::fromBeats (beat))), 0.0f, gridBottom);
            }

            // Track separators
            g.setColour (Theme::laneSeparator);

            for (int t = 1; t <= numTracks; ++t)
                g.drawHorizontalLine (view.topOfTrack (t) - 1, 0.0f, (float) getWidth());

            // An empty song: say how to start, in the middle of what's on screen
            if (! dropPoint.has_value() && ! session.isRecording())
            {
                bool anyClips = false;

                for (auto* track : te::getAudioTracks (*edit))
                    anyClips = anyClips || ! track->getClips().isEmpty();

                if (! anyClips)
                {
                    auto visible = getLocalBounds();

                    // the viewport scrolls the headers and lanes together: its view area is in their parent's
                    // coordinates, and the headers cover its left part
                    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
                        visible = viewport->getViewArea().translated (-getX(), -getY()).getIntersection (getLocalBounds());

                    const auto box = visible.withSizeKeepingCentre (juce::jmin (420, visible.getWidth() - 40), 74);

                    if (box.getWidth() > 160)
                    {
                        g.setColour (Theme::panel.withAlpha (0.85f));
                        g.fillRoundedRectangle (box.toFloat(), (float) Theme::cardRadius);
                        g.setColour (Theme::hairline);
                        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), (float) Theme::cardRadius, 1.0f);
                        auto inner = box.reduced (16, 12);
                        g.setColour (Theme::text);
                        g.setFont (Theme::font (Theme::titleFontSize, Theme::Weight::semibold));
                        g.drawText (numTracks == 0 ? "No tracks yet" : "Nothing recorded yet", inner.removeFromTop (22), juce::Justification::centred, false);
                        g.setColour (Theme::textDim);
                        g.setFont (Theme::font (Theme::smallFontSize + 0.5f));
                        g.drawFittedText (numTracks == 0 ? "Press Ctrl+T for a track, or drop audio files here"
                                                         : "Drop samples or audio files here, or arm a track (Shift+R) and press R to record",
                                          inner, juce::Justification::centred, 2);
                    }
                }
            }

            // Below the last track: a hint where dropped files go
            if (dropPoint.has_value())
            {
                const auto track = view.trackAt (dropPoint->y);
                auto area = track < numTracks ? juce::Rectangle<int> (0, view.topOfTrack (track), getWidth(), view.trackHeight)
                                              : juce::Rectangle<int> (0, view.topOfTrack (numTracks), getWidth(), view.trackHeight);
                g.setColour (Theme::selection.withAlpha (0.15f));
                g.fillRect (area);
                g.setColour (Theme::selection);
                g.drawVerticalLine (view.timeToX (snappedTimeAt (dropPoint->x)), (float) area.getY(), (float) area.getBottom());

                if (track >= numTracks)
                    g.drawText ("Drop here for a new track", area, juce::Justification::centred);
            }

            // While recording: the take growing on each armed track
            if (session.isRecording())
            {
                const auto tracks = te::getAudioTracks (*edit);
                const auto now = view.timeToX (session.getPosition());

                for (int t = 0; t < tracks.size(); ++t)
                {
                    for (auto* input : edit->getEditInputDevices().getDevicesForTargetTrack (*tracks[t]))
                    {
                        if (! input->isRecording (tracks[t]->itemID))
                            continue;

                        const auto start = view.timeToX (input->getPunchInTime (tracks[t]->itemID));
                        g.setColour (Theme::record.withAlpha (0.45f));
                        g.fillRect (juce::Rectangle<int> (start, view.topOfTrack (t) + 2, juce::jmax (0, now - start), view.trackHeight - 4));
                    }
                }
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // A frozen track: frosted over its clips (they play from the rendered audio until it's unfrozen)
            for (int t = 0; t < session.getNumAudioTracks(); ++t)
                if (session.isTrackFrozen (t))
                {
                    const juce::Rectangle<int> lane (0, view.topOfTrack (t), getWidth(), view.rowHeight (t) - 1);
                    const auto visible = lane.getIntersection (g.getClipBounds());

                    if (visible.isEmpty())
                        continue;

                    g.setColour (Theme::frozen.withAlpha (0.16f));
                    g.fillRect (visible);

                    {   // frost: fine diagonal lines, so it reads as "locked" even in a grey-scale glance
                        juce::Graphics::ScopedSaveState save (g);
                        g.reduceClipRegion (visible);
                        g.setColour (Theme::frozen.withAlpha (0.10f));

                        for (int x = visible.getX() - visible.getHeight() - (visible.getX() % 12); x < visible.getRight(); x += 12)
                            g.drawLine ((float) x, (float) visible.getBottom(), (float) (x + visible.getHeight()), (float) visible.getY());
                    }

                    g.setColour (Theme::frozen.withAlpha (0.6f));
                    g.drawHorizontalLine (lane.getY(), (float) visible.getX(), (float) visible.getRight());
                    g.drawHorizontalLine (lane.getBottom() - 1, (float) visible.getX(), (float) visible.getRight());

                    // Tag at the right of the lane (the clip names sit on the left). The lanes are always as wide
                    // as the screen (time scrolls by the view, not the viewport), so this never depends on the
                    // repainted region: a partial repaint drew a second tag when it did.
                    const auto text = juce::String ("Frozen: Ctrl+Alt+F to edit");
                    const auto tagFont = Theme::font (Theme::smallFontSize, true);
                    g.setFont (tagFont);
                    const auto tagW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (tagFont, text)) + 16;
                    const auto tag = juce::Rectangle<int> (getWidth() - tagW - 14, lane.getY() + 4, tagW, 18);

                    if (tag.getX() > 40 && tag.intersects (g.getClipBounds()))
                    {
                        g.setColour (Theme::frozen.withAlpha (0.9f));
                        g.fillRoundedRectangle (tag.toFloat(), 4.0f);
                        g.setColour (Theme::background);
                        g.drawText (text, tag, juce::Justification::centred);
                    }
                }

            if (rubberBand.isEmpty())
                return;

            g.setColour (Theme::selection.withAlpha (0.12f));
            g.fillRect (rubberBand);
            g.setColour (Theme::selection);
            g.drawRect (rubberBand);
        }

        //==============================================================================
        void mouseDown (const juce::MouseEvent& e) override
        {
            rubberBand = {};
            const auto track = view.trackAt (e.y);
            const bool onTrack = track >= 0 && track < session.getNumAudioTracks();

            if (onTrack)
                view.focusedTrack = track;

            if (e.mods.isPopupMenu())
            {
                showLaneMenu (onTrack ? track : -1, snappedTimeAt (e.x));
                return;
            }

            selectionBeforeRubberBand.clear();

            if (e.mods.isShiftDown())
                for (auto* c : session.getSelectedClips())
                    selectionBeforeRubberBand.add (c->itemID);   // IDs: an undo during the drag could delete the clips

            if (! e.mods.isShiftDown())
                session.selectClip (nullptr);

            session.setPosition (snappedTimeAt (e.x));
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 4)
                return;

            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition());
            juce::Array<te::Clip*> selection;

            if (auto* edit = session.getEdit())
                for (auto id : selectionBeforeRubberBand)
                    if (auto* c = te::findClipForID (*edit, id))
                        selection.add (c);

            for (auto* c : clipViews)
                if (c->getClip().getClipTrack() != nullptr && c->getBounds().intersects (rubberBand))
                    selection.addIfNotAlreadyThere (&c->getClip());

            session.selectClips (selection);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (! rubberBand.isEmpty())
            {
                rubberBand = {};
                repaint();
            }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto track = view.trackAt (e.y);

            if (e.mods.isPopupMenu() || track < 0 || track >= session.getNumAudioTracks())
                return;

            const bool made = session.createMidiClip (track, snappedTimeAt (e.x), oneBar (snappedTimeAt (e.x)));
            owner.report (made, "New MIDI clip");

            if (made && owner.onEditClip)
                owner.onEditClip();
        }

        //==============================================================================
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            if (! session.hasSong())
                return false;

            for (const auto& f : files)
                if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;wma;caf"))
                    return true;

            return false;
        }

        void fileDragEnter (const juce::StringArray&, int x, int y) override    { dropPoint = juce::Point<int> (x, y); repaint(); }
        void fileDragMove (const juce::StringArray&, int x, int y) override     { dropPoint = juce::Point<int> (x, y); repaint(); }
        void fileDragExit (const juce::StringArray&) override                   { dropPoint.reset(); repaint(); }

        // From the browser: the same as dropping the files from Explorer
        bool isInterestedInDragSource (const SourceDetails& d) override     { const auto f = BrowserDrag::filesIn (d.description); return ! f.isEmpty() && isInterestedInFileDrag (f); }
        void itemDragEnter (const SourceDetails& d) override                { dropPoint = d.localPosition; repaint(); }
        void itemDragMove (const SourceDetails& d) override                 { dropPoint = d.localPosition; repaint(); }
        void itemDragExit (const SourceDetails&) override                   { dropPoint.reset(); repaint(); }
        void itemDropped (const SourceDetails& d) override                  { filesDropped (BrowserDrag::filesIn (d.description), d.localPosition.x, d.localPosition.y); }

        void filesDropped (const juce::StringArray& files, int x, int y) override
        {
            dropPoint.reset();
            repaint();

            if (! session.hasSong())
                return;

            // Below the last track means "on a new track"; several files go one after another
            const auto numTracks = session.getNumAudioTracks();
            const auto track = juce::jlimit (0, numTracks, view.trackAt (y));
            juce::Array<juce::File> toImport;

            for (const auto& path : files)
                toImport.add (juce::File (path));

            juce::StringArray problems;

            if (session.importAudioFilesAt (toImport, track, snappedTimeAt (x), problems))
                view.focusedTrack = track;

            if (! problems.isEmpty() && owner.onProblem)
                owner.onProblem ("Some files weren't imported", problems.joinIntoString ("\n"));
        }

    private:
        TimelineView& owner;
        Session& session;
        TimelineViewState& view;
        juce::OwnedArray<ClipView> clipViews;
        juce::OwnedArray<AutomationLane> autoLanes;
        juce::Rectangle<int> rubberBand;
        juce::Array<te::EditItemID> selectionBeforeRubberBand;
        std::optional<juce::Point<int>> dropPoint;

        te::TimePosition snappedTimeAt (int x) const
        {
            const auto time = view.xToTime (x);
            return view.snapEnabled ? session.snapToGrid (time, view.getGridBeats (session.getBpm())) : time;
        }

        te::Clip* findClip (te::EditItemID id) const
        {
            auto* edit = session.getEdit();
            auto* clip = edit != nullptr ? te::findClipForID (*edit, id) : nullptr;
            return clip != nullptr && clip->getClipTrack() != nullptr ? clip : nullptr;
        }

        te::TimeDuration oneBar (te::TimePosition at) const
        {
            return session.getBarLengthAt (at);   // follows the tempo and time signature there
        }

        //==============================================================================
        // ClipView::Host
        int numTracks() const override      { return session.getNumAudioTracks(); }

        std::pair<te::TimeDuration, int> previewSelectionShift (te::TimeDuration delta, int trackDelta) override
        {
            juce::Array<ClipView*> selected;
            auto earliest = te::TimePosition::fromSeconds (1.0e12);
            int topTrack = std::numeric_limits<int>::max(), bottomTrack = 0;

            for (auto* c : clipViews)
            {
                if (! session.isClipSelected (c->getClip()))
                    continue;

                selected.add (c);
                earliest = juce::jmin (earliest, c->getClip().getPosition().getStart());
                topTrack = juce::jmin (topTrack, c->getTrackIndex());
                bottomTrack = juce::jmax (bottomTrack, c->getTrackIndex());
            }

            if (selected.isEmpty())
                return {};

            // Keep the whole selection inside the song: nothing before 0, nothing off the tracks
            delta = juce::jmax (delta, te::TimePosition() - earliest);
            trackDelta = juce::jlimit (-topTrack, juce::jmax (0, numTracks() - 1 - bottomTrack), trackDelta);

            for (auto* c : selected)
                c->showPreviewShift (delta, trackDelta);

            return { delta, trackDelta };
        }

        void endSelectionPreview() override
        {
            for (auto* c : clipViews)
                c->endPreview();
        }

        void clipsMayHaveChangedTrack() override
        {
            owner.checkStructureSoon = true;
        }

        void editClip (te::Clip& clip) override
        {
            if (dynamic_cast<te::MidiClip*> (&clip) == nullptr || clip.getClipTrack() == nullptr)
                return;

            session.selectClip (&clip);

            if (owner.onEditClip)
                owner.onEditClip();
        }

        void showClipMenu (te::Clip& clip) override
        {
            enum { cutId = 1, copyId, pasteId, duplicateId, splitId, deleteId, renameId, muteId, colourBase = 100, takeBase = 200,
                   normaliseId = 300, reverseId, followTempoId, shortFadesId, loopTheseId, fadeInBase = 320, fadeOutBase = 340, gainBase = 360, transposeBase = 400,
                   editNotesId = 500, quantiseBase = 510, noteShiftBase = 530, freezeTrackId = 550 };

            struct NamedColour { const char* name; juce::Colour colour; };
            static const NamedColour colours[] = {
                { "Default",  juce::Colours::transparentBlack },
                { "Red",      juce::Colour (0xffd9534f) }, { "Orange", juce::Colour (0xffe8913a) },
                { "Yellow",   juce::Colour (0xffd8c03a) }, { "Green",  juce::Colour (0xff4fae5c) },
                { "Teal",     juce::Colour (0xff3aa8a0) }, { "Blue",   juce::Colour (0xff3b78b8) },
                { "Purple",   juce::Colour (0xff8a5cc7) }, { "Pink",   juce::Colour (0xffd16ba5) },
                { "Grey",     juce::Colour (0xff8a8f99) } };

            const auto numSelected = session.getSelectedClips().size();
            const auto which = numSelected > 1 ? juce::String (numSelected) + " clips" : juce::String ("clip");

            juce::PopupMenu menu;
            menu.addItem (cutId,       "Cut " + which + "\tCtrl+X");
            menu.addItem (copyId,      "Copy " + which + "\tCtrl+C");
            menu.addItem (pasteId,     "Paste at playhead\tCtrl+V", session.hasClipsToPaste());
            menu.addItem (duplicateId, "Duplicate " + which + "\tCtrl+D");
            menu.addItem (splitId,     "Split at playhead\tCtrl+E", clip.getPosition().time.contains (session.getPosition()));
            menu.addItem (deleteId,    "Delete " + which + "\tDel");
            menu.addSeparator();
            menu.addItem (renameId,    "Rename clip...");
            menu.addItem (muteId,      clip.isMuted() ? "Unmute clip" : "Mute clip");

            juce::PopupMenu colourMenu;

            for (int i = 0; i < (int) std::size (colours); ++i)
                colourMenu.addItem (colourBase + i, colours[i].name);

            menu.addSubMenu ("Clip colour", colourMenu);

            // Audio: fades, gain, normalise, reverse, transpose, tempo
            static const double fadeChoices[] = { 0.0, 0.01, 0.05, 0.25, 1.0, 4.0 };
            static const float gainChoices[] = { -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f };

            if (dynamic_cast<te::AudioClipBase*> (&clip) != nullptr)
            {
                juce::PopupMenu audioMenu, fadeInMenu, fadeOutMenu, gainMenu, transposeMenu;
                const auto fadeName = [] (double s) { return s <= 0.0 ? juce::String ("None") : s < 1.0 ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, 0) + " s"; };

                for (int i = 0; i < (int) std::size (fadeChoices); ++i)
                {
                    fadeInMenu.addItem (fadeInBase + i, fadeName (fadeChoices[i]), true, std::abs (session.getClipFadeIn (clip) - fadeChoices[i]) < 0.001);
                    fadeOutMenu.addItem (fadeOutBase + i, fadeName (fadeChoices[i]), true, std::abs (session.getClipFadeOut (clip) - fadeChoices[i]) < 0.001);
                }

                for (int i = 0; i < (int) std::size (gainChoices); ++i)
                    gainMenu.addItem (gainBase + i, (gainChoices[i] > 0 ? "+" : "") + juce::String ((int) gainChoices[i]) + " dB", true,
                                      std::abs (session.getClipGainDb (clip) - gainChoices[i]) < 0.05f);

                for (int st = 12; st >= -12; --st)
                    transposeMenu.addItem (transposeBase + 24 + st, st == 0 ? juce::String ("None") : (st > 0 ? "+" : "") + juce::String (st)
                                           + (std::abs (st) == 12 ? " (an octave)" : ""), true, juce::roundToInt (session.getClipTranspose (clip)) == st);

                audioMenu.addItem (shortFadesId, "Short fades (no clicks)\tF");
                audioMenu.addSubMenu ("Fade in", fadeInMenu);
                audioMenu.addSubMenu ("Fade out", fadeOutMenu);
                audioMenu.addSubMenu ("Clip gain (" + juce::String (session.getClipGainDb (clip), 1) + " dB)", gainMenu);
                audioMenu.addItem (normaliseId, "Normalise (peak at -1 dB)\tCtrl+Shift+N");
                audioMenu.addItem (reverseId, "Reverse\tCtrl+Alt+R", true, session.isClipReversed (clip));
                audioMenu.addSubMenu ("Transpose (semitones)", transposeMenu);
                audioMenu.addItem (followTempoId, "Follow the song tempo\tCtrl+J", true, session.isClipFollowingTempo (clip));
                menu.addSubMenu ("Audio" + (numSelected > 1 ? " (" + juce::String (numSelected) + " clips)" : juce::String()), audioMenu);
            }

            // MIDI: open the notes, quantise, transpose (what every DAW offers on a pattern)
            static const std::pair<double, const char*> quantiseChoices[] = { { 1.0, "1/4" }, { 0.5, "1/8" }, { 0.25, "1/16" }, { 0.125, "1/32" },
                                                                              { 1.0 / 3.0, "1/8 triplets" }, { 1.0 / 6.0, "1/16 triplets" } };
            static const int noteShifts[] = { 12, 1, -1, -12 };

            if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
            {
                juce::PopupMenu midiMenu, quantiseMenu, shiftMenu;
                const auto hasNotes = ! midi->getSequence().getNotes().isEmpty();

                for (int i = 0; i < (int) std::size (quantiseChoices); ++i)
                    quantiseMenu.addItem (quantiseBase + i, quantiseChoices[i].second, hasNotes);

                for (int i = 0; i < (int) std::size (noteShifts); ++i)
                    shiftMenu.addItem (noteShiftBase + i, (noteShifts[i] > 0 ? "Up " : "Down ") + juce::String (std::abs (noteShifts[i]))
                                                          + (std::abs (noteShifts[i]) == 12 ? " (an octave)" : " semitone"), hasNotes);

                midiMenu.addItem (editNotesId, "Edit notes\tDouble-click");
                midiMenu.addSubMenu ("Quantise every note to", quantiseMenu, hasNotes);
                midiMenu.addSubMenu ("Transpose every note", shiftMenu, hasNotes);
                menu.addSubMenu ("Notes", midiMenu);
            }

            menu.addItem (loopTheseId, (session.getSelectedClips().size() > 1 ? juce::String ("Loop these clips") : juce::String ("Loop this clip")) + "\tCtrl+L");

            // Takes (recording over a loop keeps every pass): pick one; split first to comp parts
            if (const auto takes = session.getTakeNames (clip); takes.size() > 1)
            {
                juce::PopupMenu takeMenu;
                const auto current = session.getCurrentTake (clip);

                for (int i = 0; i < juce::jmin (takes.size(), 99); ++i)
                {
                    juce::PopupMenu::Item item (takes[i]);
                    item.itemID = takeBase + i;
                    item.isTicked = i == current;

                    if (i == (current + 1) % takes.size())
                        item.shortcutKeyDescription = "T";

                    takeMenu.addItem (item);
                }

                takeMenu.addSeparator();
                takeMenu.addItem (-1, "Tip: split the clip (Ctrl+E), then pick a take for each part", false);
                menu.addSubMenu ("Takes (" + juce::String (current + 1) + " of " + juce::String (takes.size()) + ")", takeMenu);
            }

            if (const auto track = te::getAudioTracks (clip.edit).indexOf (dynamic_cast<te::AudioTrack*> (clip.getClipTrack())); track >= 0)
            {
                menu.addSeparator();
                menu.addItem (freezeTrackId, session.isTrackFrozen (track) ? "Unfreeze the track\tCtrl+Alt+F" : "Freeze the track (save CPU)\tCtrl+Alt+F");
            }

            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                                [safeThis = juce::Component::SafePointer<Lanes> (this), clipID = clip.itemID] (int result)
                                {
                                    if (safeThis == nullptr || result <= 0)
                                        return;

                                    auto* clipPtr = safeThis->findClip (clipID);

                                    if (clipPtr == nullptr)   // deleted, or the song closed, while the menu was open
                                        return;

                                    auto& o = safeThis->owner;
                                    auto& s = safeThis->session;

                                    // Audio edits go to every selected clip (as their keys do), one undo step
                                    const auto onSelection = [&o, &s] (std::function<bool (te::Clip&)> edit, const char* what)
                                    {
                                        o.report (s.editSelectedAudioClips (edit), what);
                                    };

                                    switch (result)
                                    {
                                        case cutId:       o.cut(); break;
                                        case copyId:      o.copy(); break;
                                        case pasteId:     o.paste(); break;
                                        case duplicateId: o.duplicate(); break;
                                        case splitId:     o.splitAtPlayhead(); break;
                                        case deleteId:    o.deleteSelection(); break;
                                        case renameId:    safeThis->askToRename (*clipPtr); break;
                                        case muteId:      o.report (s.setClipMuted (*clipPtr, ! clipPtr->isMuted()), "Mute"); break;
                                        case loopTheseId:   if (! o.loopSelection()) o.report (false, "Loop"); break;
                                        case shortFadesId:
                                            onSelection ([&s] (te::Clip& c) { return s.setClipFades (c, juce::jmax (0.01, s.getClipFadeIn (c)), juce::jmax (0.01, s.getClipFadeOut (c))); }, "Fade");
                                            break;
                                        case normaliseId:
                                            onSelection ([&s] (te::Clip& c) { return s.normaliseClip (c); }, "Normalise");
                                            break;
                                        case reverseId:
                                        {
                                            const auto on = ! s.isClipReversed (*clipPtr);
                                            onSelection ([&s, on] (te::Clip& c) { return s.setClipReversed (c, on); }, "Reverse");
                                            break;
                                        }
                                        case followTempoId:
                                        {
                                            const auto on = ! s.isClipFollowingTempo (*clipPtr);
                                            onSelection ([&s, on] (te::Clip& c) { return s.setClipFollowsTempo (c, on); }, "Tempo");
                                            break;
                                        }
                                        case editNotesId:
                                            s.selectClip (clipPtr);

                                            if (o.onEditClip)
                                                o.onEditClip();
                                            break;
                                        case freezeTrackId:
                                        {
                                            const auto track = te::getAudioTracks (clipPtr->edit).indexOf (dynamic_cast<te::AudioTrack*> (clipPtr->getClipTrack()));
                                            o.report (s.setTrackFrozen (track, ! s.isTrackFrozen (track)), "Freeze");
                                            break;
                                        }
                                        default:
                                            if (result >= noteShiftBase && result < noteShiftBase + (int) std::size (noteShifts))
                                            {
                                                if (auto* midi = dynamic_cast<te::MidiClip*> (clipPtr))
                                                    o.report (s.moveMidiNotes (*midi, midi->getSequence().getNotes(), 0.0, noteShifts[result - noteShiftBase]), "Transpose");
                                            }
                                            else if (result >= quantiseBase && result < quantiseBase + (int) std::size (quantiseChoices))
                                            {
                                                if (auto* midi = dynamic_cast<te::MidiClip*> (clipPtr))
                                                    o.report (s.quantiseMidiNotes (*midi, midi->getSequence().getNotes(), quantiseChoices[result - quantiseBase].first), "Quantise");
                                            }
                                            else if (result >= transposeBase && result < transposeBase + 49)
                                            {
                                                const auto st = (float) (result - transposeBase - 24);
                                                onSelection ([&s, st] (te::Clip& c) { return s.setClipTranspose (c, st); }, "Transpose");
                                            }
                                            else if (result >= gainBase)
                                            {
                                                const auto db = gainChoices[juce::jlimit (0, (int) std::size (gainChoices) - 1, result - gainBase)];
                                                onSelection ([&s, db] (te::Clip& c) { return s.setClipGainDb (c, db); }, "Clip gain");
                                            }
                                            else if (result >= fadeOutBase)
                                            {
                                                const auto len = fadeChoices[juce::jlimit (0, (int) std::size (fadeChoices) - 1, result - fadeOutBase)];
                                                onSelection ([&s, len] (te::Clip& c) { return s.setClipFades (c, s.getClipFadeIn (c), len); }, "Fade");
                                            }
                                            else if (result >= fadeInBase)
                                            {
                                                const auto len = fadeChoices[juce::jlimit (0, (int) std::size (fadeChoices) - 1, result - fadeInBase)];
                                                onSelection ([&s, len] (te::Clip& c) { return s.setClipFades (c, len, s.getClipFadeOut (c)); }, "Fade");
                                            }
                                            else if (result >= takeBase)
                                                o.report (s.setClipTake (*clipPtr, result - takeBase), "Take");
                                            else if (result >= colourBase && result < colourBase + (int) std::size (colours))
                                                o.report (s.setClipColour (*clipPtr, colours[result - colourBase].colour), "Colour");
                                            break;
                                    }
                                });
        }

        void askToRename (te::Clip& clip)
        {
            auto* window = new juce::AlertWindow ("Rename clip", "New name:", juce::MessageBoxIconType::NoIcon, this);
            window->addTextEditor ("name", clip.getName());
            window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            window->enterModalState (true, juce::ModalCallbackFunction::create (
                [safeThis = juce::Component::SafePointer<Lanes> (this), clipID = clip.itemID, window] (int result)
                {
                    if (safeThis == nullptr || result != 1)
                        return;

                    auto* clipPtr = safeThis->findClip (clipID);

                    if (clipPtr == nullptr)
                        return;

                    safeThis->owner.report (safeThis->session.renameClip (*clipPtr, window->getTextEditorContents ("name")), "Rename");
                }), true);
        }

        void showLaneMenu (int track, te::TimePosition time)
        {
            enum { pasteHereId = 1, newMidiId, selectAllId, addTrackId, playheadId, loopBarId, zoomFitId, freezeId };

            juce::PopupMenu menu;

            if (track >= 0)
            {
                menu.addSectionHeader (session.getTrackName (track));
                menu.addItem (pasteHereId, "Paste here\tCtrl+V", session.hasClipsToPaste());
                menu.addItem (newMidiId,   "New MIDI clip here\tDouble-click");

                if (session.getTrackKind (track) == Session::TrackKind::normal)
                    menu.addItem (freezeId, session.isTrackFrozen (track) ? "Unfreeze this track\tCtrl+Alt+F" : "Freeze this track\tCtrl+Alt+F",
                                  ! session.isRecording());

                menu.addSeparator();
            }

            menu.addItem (playheadId,  "Move the playhead here");
            menu.addItem (loopBarId,   "Loop this bar\tL");
            menu.addSeparator();
            menu.addItem (selectAllId, "Select all clips\tCtrl+A");
            menu.addItem (zoomFitId,   "Zoom to fit the song");
            menu.addItem (addTrackId,  "Add audio track\tCtrl+T");

            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                                [safeThis = juce::Component::SafePointer<Lanes> (this), track, time] (int result)
                                {
                                    if (safeThis == nullptr || result <= 0)
                                        return;

                                    auto& o = safeThis->owner;
                                    auto& s = safeThis->session;

                                    switch (result)
                                    {
                                        case pasteHereId: o.report (s.pasteClips (time, track), "Paste"); break;
                                        case newMidiId:   o.report (s.createMidiClip (track, time, safeThis->oneBar (time)), "New MIDI clip"); break;
                                        case selectAllId: o.selectAll(); break;
                                        case addTrackId:  s.addAudioTrack(); break;
                                        case playheadId:  s.setPosition (time); break;
                                        case zoomFitId:   o.zoomToFit(); break;
                                        case freezeId:    o.report (s.setTrackFrozen (track, ! s.isTrackFrozen (track)), "Freeze"); break;
                                        case loopBarId:
                                        {
                                            // the bar under the click: from its first beat for one bar
                                            const auto& tempo = s.getEdit()->tempoSequence;
                                            const auto bar = s.getBarAt (tempo.toBeats (time).inBeats() + 1.0e-7).first;
                                            const auto start = tempo.toTime (te::BeatPosition::fromBeats (s.getBarStartBeat (bar)));
                                            s.setLoopRange (start, start + safeThis->oneBar (start));
                                            s.setLooping (true);
                                            break;
                                        }
                                        default: break;
                                    }
                                });
        }
    };

    //==============================================================================
    /** Headers on the left, lanes on the right; scrolled vertically by the viewport. */
    class TimelineView::TracksArea : public juce::Component
    {
    public:
        TracksArea (TimelineView& o) : owner (o), lanes (o)
        {
            addAndMakeVisible (lanes);
        }

        bool isRenamingATrack() const
        {
            for (auto* h : headers)
                if (h->isRenaming())
                    return true;

            return false;
        }

        void rebuild()
        {
            headers.clear();
            laneHeaders.clear();

            for (int t = 0; t < owner.session.getNumAudioTracks(); ++t)
            {
                addAndMakeVisible (headers.add (new TrackHeader (owner.session, owner.view, t)));
                const auto shown = owner.session.getShownAutomationLanes (t);

                for (int l = 0; l < (int) shown.size(); ++l)
                {
                    auto* h = laneHeaders.add (new AutomationLaneHeader (owner.session, t, l, shown[(size_t) l]));
                    h->onProblem = [this] (const juce::String& m) { if (owner.onProblem) owner.onProblem ("Automation", m); };
                    addAndMakeVisible (h);
                }
            }

            lanes.rebuild();
            resized();
        }

        /** Drops every view of the song (used while it closes). */
        void clear()
        {
            headers.clear();
            laneHeaders.clear();
            lanes.clear();
        }

        void refreshHeaders()
        {
            for (auto* h : headers)
                h->refresh();

            for (auto* h : laneHeaders)
                h->refresh();
        }

        Lanes& getLanes() noexcept      { return lanes; }

        void resized() override
        {
            auto r = getLocalBounds();
            auto headerColumn = r.removeFromLeft (Theme::trackHeaderWidth);

            int laneHeader = 0;

            for (int t = 0; t < headers.size(); ++t)
            {
                headers[t]->setBounds (headerColumn.removeFromTop (owner.view.trackHeight));

                for (int l = 0; l < owner.view.lanesOf (t) && laneHeader < laneHeaders.size(); ++l)
                    laneHeaders[laneHeader++]->setBounds (headerColumn.removeFromTop (TimelineViewState::automationLaneHeight));
            }

            lanes.setBounds (r);
            lanes.layoutClips();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (e.mods.isCommandDown() || e.mods.isShiftDown() || e.mods.isAltDown())
                owner.handleWheel (e, wheel, e.getEventRelativeTo (&lanes).x);
            else
                juce::Component::mouseWheelMove (e, wheel);   // the viewport scrolls tracks
        }

    private:
        TimelineView& owner;
        Lanes lanes;
        juce::OwnedArray<TrackHeader> headers;
        juce::OwnedArray<AutomationLaneHeader> laneHeaders;   // in track order, each track's lanes top to bottom
    };

    //==============================================================================
    /** The playhead line across ruler and lanes. Never takes mouse clicks. */
    class TimelineView::PlayheadOverlay : public juce::Component
    {
    public:
        PlayheadOverlay (Session& s, TimelineViewState& v) : session (s), view (v)
        {
            setInterceptsMouseClicks (false, false);
        }

        void update()
        {
            const auto x = session.hasSong() ? view.timeToX (session.getPosition()) : -1;

            if (x != lastX)
            {
                repaint (lastX - 2, 0, 5, getHeight());
                repaint (x - 2, 0, 5, getHeight());
                lastX = x;
            }
        }

        void paint (juce::Graphics& g) override
        {
            if (lastX >= 0)
            {
                // A 1 px line with a soft glow either side: thin, but easy to find
                g.setColour (Theme::playhead.withAlpha (0.12f));
                g.fillRect (lastX - 1, 0, 3, getHeight());
                g.setColour (Theme::playhead);
                g.drawVerticalLine (lastX, 0.0f, (float) getHeight());
            }
        }

    private:
        Session& session;
        TimelineViewState& view;
        int lastX = -1;
    };

    //==============================================================================
    TimelineView::TimelineView (Session& s)
        : session (s)
    {
        ruler = std::make_unique<Ruler> (session, view);
        tempoLane = std::make_unique<TempoLane> (session, view);
        tempoLane->onWheelElsewhere = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { handleWheel (e, w, e.x); };
        tempoLane->onProblem = [this] (const juce::String& title, const juce::String& message) { if (onProblem) onProblem (title, message); };
        sectionsLane = std::make_unique<SectionsLane> (session, view);
        sectionsLane->onWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { handleWheel (e, w, e.x); };
        sectionsLane->onProblem = [this] (const juce::String& title, const juce::String& message) { if (onProblem) onProblem (title, message); };
        tracksArea = std::make_unique<TracksArea> (*this);
        playhead = std::make_unique<PlayheadOverlay> (session, view);

        viewport.setViewedComponent (tracksArea.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
        viewport.setWantsKeyboardFocus (false);   // arrow keys are editing shortcuts, not viewport scrolling

        horizontalScroll.setAutoHide (false);
        horizontalScroll.addListener (this);

        emptyMessage.setText ("No song open", juce::dontSendNotification);
        emptyMessage.setJustificationType (juce::Justification::centred);
        emptyMessage.setColour (juce::Label::textColourId, Theme::textDim);

        addTrackButton.setTooltip ("Add a track: audio, instrument, bus, or import a file (Ctrl+T adds an audio track straight away)");
        addTrackButton.onClick = [this] { if (onAddTrack) onAddTrack (addTrackButton); };
        zoomOutButton.setTooltip ("Zoom out (-)");
        zoomInButton.setTooltip ("Zoom in (=)");
        zoomOutButton.onClick = [this] { zoom (1.0 / 1.5); };   // the same steps as - and =
        zoomInButton.onClick = [this] { zoom (1.5); };
        zoomFitButton.setTooltip ("Zoom to fit: the whole song across the screen (Z)");
        zoomFitButton.onClick = [this] { zoomToFit(); };

        readButton.setTooltip ("Read: automation lanes move their controls as the song plays (Shift+W)");
        writeButton.setTooltip ("Write: move a control while the song plays to record it into its lane (W). Turns Read on too");
        readButton.onClick = [this] { session.setAutomationRead (! session.isAutomationReadOn()); refreshAutomationButtons(); };
        writeButton.onClick = [this] { session.setAutomationWrite (! session.isAutomationWriteOn()); refreshAutomationButtons(); };

        for (auto* b : { &addTrackButton, &zoomOutButton, &zoomInButton, &zoomFitButton, &readButton, &writeButton })
            addAndMakeVisible (b);

        addAndMakeVisible (*ruler);
        addAndMakeVisible (*tempoLane);
        addAndMakeVisible (*sectionsLane);
        addAndMakeVisible (viewport);
        addAndMakeVisible (horizontalScroll);
        addChildComponent (emptyMessage);
        addAndMakeVisible (*playhead);

        session.addListener (this);
        rebuild();
        startTimerHz (refreshHz);
    }

    TimelineView::~TimelineView()
    {
        session.removeListener (this);
    }

    //==============================================================================
    void TimelineView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);

        // Corner above the track headers: the tempo lane's name, then + Track and zoom
        const auto lanes = Theme::sectionsLaneHeight + Theme::tempoLaneHeight;
        const auto topHeight = lanes + Theme::rulerHeight;
        g.setColour (Theme::bar);
        g.fillRect (0, 0, Theme::trackHeaderWidth, topHeight);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (Theme::sectionsLaneHeight - 1, 0.0f, (float) Theme::trackHeaderWidth);
        g.drawHorizontalLine (lanes - 1, 0.0f, (float) Theme::trackHeaderWidth);
        g.drawHorizontalLine (topHeight - 1, 0.0f, (float) Theme::trackHeaderWidth);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (Theme::captionFontSize, true));
        g.drawText ("SECTIONS", juce::Rectangle<int> (10, 0, Theme::trackHeaderWidth - 20, Theme::sectionsLaneHeight),
                    juce::Justification::centredLeft, true);
        g.drawText ("TEMPO  TIME  KEY", juce::Rectangle<int> (10, Theme::sectionsLaneHeight, Theme::trackHeaderWidth - 20, Theme::tempoLaneHeight),
                    juce::Justification::centredLeft, true);
        g.drawVerticalLine (Theme::trackHeaderWidth - 1, 0.0f, (float) getHeight());
    }

    void TimelineView::resized()
    {
        auto r = getLocalBounds();
        sectionsLane->setBounds (r.removeFromTop (Theme::sectionsLaneHeight).withTrimmedLeft (Theme::trackHeaderWidth));
        tempoLane->setBounds (r.removeFromTop (Theme::tempoLaneHeight).withTrimmedLeft (Theme::trackHeaderWidth));
        auto top = r.removeFromTop (Theme::rulerHeight);
        auto corner = top.removeFromLeft (Theme::trackHeaderWidth).reduced (8, 3).withTrimmedBottom (1);
        addTrackButton.setBounds (corner.removeFromLeft (addTrackButton.getIdealWidth (corner.getHeight())));
        zoomInButton.setBounds (corner.removeFromRight (corner.getHeight() + 2));
        zoomOutButton.setBounds (corner.removeFromRight (corner.getHeight() + 2));
        zoomFitButton.setVisible (corner.getWidth() > 190);   // narrow headers: Z still fits
        if (zoomFitButton.isVisible())
            zoomFitButton.setBounds (corner.removeFromRight (zoomFitButton.getIdealWidth (corner.getHeight())));
        corner.removeFromLeft (8);
        readButton.setBounds (corner.removeFromLeft (readButton.getIdealWidth (corner.getHeight())));
        corner.removeFromLeft (4);
        writeButton.setBounds (corner.removeFromLeft (juce::jmin (corner.getWidth(), writeButton.getIdealWidth (corner.getHeight()))));
        ruler->setBounds (top);

        auto bottom = r.removeFromBottom (Theme::scrollBarSize);
        bottom.removeFromLeft (Theme::trackHeaderWidth);
        horizontalScroll.setBounds (bottom);

        viewport.setBounds (r);
        emptyMessage.setBounds (r);
        layoutTracks();

        // Playhead spans ruler + lanes, never the headers or the scrollbar
        playhead->setBounds (Theme::trackHeaderWidth, 0, getWidth() - Theme::trackHeaderWidth,
                             getHeight() - Theme::scrollBarSize);
        updateScrollBar();
    }

    //==============================================================================
    void TimelineView::zoom (double factor)
    {
        view.zoom (factor, tracksArea->getLanes().getWidth() / 2);
        layoutForView();
    }

    void TimelineView::zoomToFit()
    {
        const auto width = juce::jmax (100, tracksArea->getLanes().getWidth() - 20);
        const auto seconds = juce::jmax (10.0, session.getSongLength().inSeconds() * 1.05 + 2.0);
        view.pixelsPerSecond = juce::jlimit (TimelineViewState::minPixelsPerSecond, TimelineViewState::maxPixelsPerSecond, width / seconds);
        view.viewStartSeconds = 0.0;
        layoutForView();
    }

    bool TimelineView::loopSelection()
    {
        const auto clips = session.getSelectedClips();

        if (clips.isEmpty())
            return false;

        auto start = clips.getFirst()->getPosition().getStart(), end = clips.getFirst()->getPosition().getEnd();

        for (auto* c : clips)
        {
            start = juce::jmin (start, c->getPosition().getStart());
            end = juce::jmax (end, c->getPosition().getEnd());
        }

        session.setLoopRange (start, end);
        return session.setLooping (true);
    }

    void TimelineView::toggleSnap()
    {
        view.snapEnabled = ! view.snapEnabled;
    }

    void TimelineView::setFixedGridBeats (double beats)
    {
        view.fixedGridBeats = juce::jmax (0.0, beats);
        layoutForView();
        repaint();
    }

    void TimelineView::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel, int xInLanes)
    {
        const auto amount = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;

        if (e.mods.isCommandDown())
        {
            view.zoom (amount > 0 ? 1.25 : 0.8, xInLanes);
        }
        else if (e.mods.isAltDown())
        {
            view.setTrackHeight (view.trackHeight + (amount > 0 ? 8 : -8));
            layoutTracks();
        }
        else
            view.viewStartSeconds = juce::jmax (0.0, view.viewStartSeconds - amount * 200.0 / view.pixelsPerSecond);

        layoutForView();
    }

    void TimelineView::layoutTracks()
    {
        // At least as tall as the view, so files can be dropped below the last track
        const auto contentHeight = view.topOfTrack (session.getNumAudioTracks());
        tracksArea->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (contentHeight + view.trackHeight, viewport.getMaximumVisibleHeight()));
        tracksArea->resized();
    }

    void TimelineView::layoutForView()
    {
        tracksArea->getLanes().layoutClips();
        ruler->repaint();
        tempoLane->repaint();
        sectionsLane->repaint();
        playhead->update();
        updateScrollBar();
    }

    void TimelineView::updateScrollBar()
    {
        const auto visibleSeconds = juce::jmax (1, tracksArea->getLanes().getWidth()) / view.pixelsPerSecond;
        const auto totalSeconds = juce::jmax (session.getSongLength().inSeconds() + extraSecondsAfterSong,
                                              view.viewStartSeconds + visibleSeconds);

        horizontalScroll.setRangeLimits (0.0, totalSeconds, juce::dontSendNotification);
        horizontalScroll.setCurrentRange (view.viewStartSeconds, visibleSeconds, juce::dontSendNotification);
    }

    void TimelineView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        view.viewStartSeconds = juce::jmax (0.0, newRangeStart);
        layoutForView();
    }

    void TimelineView::followPlayhead()
    {
        if (! session.isPlaying() || ! following)
            return;

        const auto x = view.timeToX (session.getPosition());
        const auto width = tracksArea->getLanes().getWidth();

        if (x < 0 || x > width - 20)   // page along with the song
        {
            view.viewStartSeconds = juce::jmax (0.0, session.getPosition().inSeconds() - 0.1 * width / view.pixelsPerSecond);
            layoutForView();
        }
    }

    //==============================================================================
    // Edit actions
    bool TimelineView::report (bool ok, const juce::String& title)
    {
        if (! ok && onProblem != nullptr)
            onProblem (title, session.getLastError());

        return ok;
    }

    int TimelineView::getPasteTrack() const
    {
        return juce::jlimit (0, juce::jmax (0, session.getNumAudioTracks() - 1), view.focusedTrack);
    }

    // With nothing selected, these quietly do nothing (pressing Ctrl+C on nothing isn't an error)
    bool TimelineView::copy()               { return ! session.getSelectedClips().isEmpty() && report (session.copySelectedClips(), "Copy"); }
    bool TimelineView::cut()                { return ! session.getSelectedClips().isEmpty() && report (session.cutSelectedClips(), "Cut"); }
    bool TimelineView::duplicate()          { return ! session.getSelectedClips().isEmpty() && report (session.duplicateSelectedClips(), "Duplicate"); }
    bool TimelineView::deleteSelection()    { return ! session.getSelectedClips().isEmpty() && report (session.deleteSelectedClips(), "Delete"); }
    void TimelineView::selectAll()          { session.selectAllClips(); }
    void TimelineView::deselectAll()        { session.selectClip (nullptr); }

    bool TimelineView::paste()
    {
        if (! session.hasClipsToPaste() || session.getNumAudioTracks() == 0)
            return false;

        return report (session.pasteClips (session.getPosition(), getPasteTrack()), "Paste");
    }

    bool TimelineView::splitAtPlayhead()
    {
        return session.hasSong() && report (session.splitClipsAt (session.getPosition()), "Split");
    }

    void TimelineView::nudge (int direction)
    {
        const auto step = te::TimeDuration::fromSeconds (direction * view.getGridSeconds (session.getBpm()));

        if (! session.getSelectedClips().isEmpty())
            session.moveSelectedClips (step, 0);   // refused at the start of the song: nothing to report
        else if (session.hasSong())
            session.setPosition (juce::jmax (te::TimePosition(), session.getPosition() + step));
    }

    void TimelineView::goToEnd()
    {
        if (session.hasSong())
            session.setPosition (te::TimePosition() + session.getSongLength());
    }

    //==============================================================================
    juce::String TimelineView::describeStructure() const
    {
        juce::String s;

        if (auto* edit = session.getEdit())
        {
            const auto tracks = te::getAudioTracks (*edit);

            for (int t = 0; t < tracks.size(); ++t)
            {
                auto* track = tracks[t];
                s << "T" << track->itemID.toString();

                for (const auto& lane : session.getShownAutomationLanes (t))
                    s << "L" << lane.toString();

                for (auto* clip : track->getClips())
                    s << "C" << clip->itemID.toString();
            }
        }

        return s;
    }

    void TimelineView::rebuild()
    {
        emptyMessage.setVisible (! session.hasSong());
        viewport.setVisible (session.hasSong());
        view.focusedTrack = juce::jlimit (0, juce::jmax (0, session.getNumAudioTracks() - 1), view.focusedTrack);
        view.lanesPerTrack.clear();

        for (int t = 0; t < session.getNumAudioTracks(); ++t)
            view.lanesPerTrack.push_back ((int) session.getShownAutomationLanes (t).size());

        tracksArea->rebuild();
        layoutTracks();
        lastStructure = describeStructure();
        layoutForView();
    }

    void TimelineView::sessionSongClosing()
    {
        view = {};
        tracksArea->clear();   // the song is still open here: don't rebuild from it
        lastStructure = {};
    }

    void TimelineView::sessionSongChanged()     { rebuild(); refreshAutomationButtons(); }

    EditorTarget* TimelineView::getFocusedEditor() const
    {
        return isShowing() ? tracksArea->getLanes().getFocusedLane() : nullptr;
    }

    void TimelineView::refreshAutomationButtons()
    {
        readButton.setToggleState (session.isAutomationReadOn(), juce::dontSendNotification);
        writeButton.setToggleState (session.isAutomationWriteOn(), juce::dontSendNotification);
        writeButton.setFillOverride (session.isAutomationWriteOn() ? std::optional<juce::Colour> (Theme::record.withAlpha (0.55f)) : std::nullopt);   // red: it records
        readButton.setEnabled (session.hasSong());
        writeButton.setEnabled (session.hasSong());
    }

    void TimelineView::sessionStateChanged()
    {
        checkStructureSoon = true;   // never rebuild here: we may be inside a ClipView's mouse handler
        tracksArea->refreshHeaders();
        refreshAutomationButtons();
        tracksArea->getLanes().repaint();   // selection, mute etc.
        ruler->repaint();
        tempoLane->repaint();   // tempo, time signature, key
    }

    void TimelineView::timerCallback()
    {
        playhead->update();
        followPlayhead();

        if (session.isPlaying() || session.isRecording())
        {
            ruler->repaint();

            if (session.isRecording())
                tracksArea->getLanes().repaint();
        }

        if (++tick % structureCheckEveryTicks == 0 || std::exchange (checkStructureSoon, false))
        {
            if (describeStructure() != lastStructure)
            {
                if (! tracksArea->isRenamingATrack())   // never wipe a name being typed; retry next time
                    rebuild();
            }
            else
            {
                tracksArea->getLanes().refreshClips();   // clip moved/resized by undo etc.
                tempoLane->repaint();                    // an undo may have changed the tempo map
                sectionsLane->repaint();
            }

            tracksArea->refreshHeaders();
        }
    }
}
