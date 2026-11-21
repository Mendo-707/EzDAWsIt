/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "ClipView.h"
#include "../Theme.h"
#include "../Widgets.h"

namespace ez
{
    namespace
    {
        constexpr int maxEdgeWidth = 6;      // pixels at each end that trim instead of move
        constexpr int dragThreshold = 4;
    }

    ClipView::ClipView (Session& s, TimelineViewState& v, Host& h, te::Clip& c, int index)
        : session (s), view (v), host (h), clip (&c), trackIndex (index)
    {
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        {
            const te::AudioFile file (wave->getPlaybackFile());

            if (file.isValid())
                thumbnail = std::make_unique<te::SmartThumbnail> (wave->edit.engine, file, *this, &wave->edit);
        }

        setRepaintsOnMouseActivity (false);
        updateBounds();
    }

    ClipView::~ClipView() = default;

    void ClipView::updateBounds()
    {
        if (previewing || ! isStillInSong())
            return;

        const auto position = clip->getPosition();
        const auto x1 = view.timeToX (position.getStart());
        const auto x2 = view.timeToX (position.getEnd());
        setBounds (x1, view.topOfTrack (trackIndex) + 2, juce::jmax (2, x2 - x1), view.trackHeight - 4);
    }

    void ClipView::showPreviewShift (te::TimeDuration delta, int trackDelta)
    {
        previewing = true;
        const auto position = clip->getPosition();
        setTopLeftPosition (view.timeToX (position.getStart() + delta), view.topOfTrack (trackIndex + trackDelta) + 2);
    }

    void ClipView::endPreview()
    {
        previewing = false;
        updateBounds();
    }

    juce::String ClipView::makeFingerprint() const
    {
        const auto p = clip->getPosition();
        juce::String s;
        s << p.getStart().inSeconds() << "|" << p.getLength().inSeconds() << "|" << p.getOffset().inSeconds()
          << "|" << clip->getName() << "|" << session.getCurrentTake (*clip) << "|" << (session.isClipSelected (*clip) ? 1 : 0)
          << "|" << (clip->isMuted() ? 1 : 0) << "|" << (int) clip->getColour().getARGB()
          << "|" << session.getClipFadeIn (*clip) << "|" << session.getClipFadeOut (*clip) << "|" << session.getClipGainDb (*clip)
          << "|" << (int) session.isClipReversed (*clip) << "|" << session.getClipTranspose (*clip) << "|" << (int) session.isClipFollowingTempo (*clip);

        if (auto* midi = dynamic_cast<te::MidiClip*> (clip.get()))
            s << "|" << midi->getSequence().getNumNotes();

        return s;
    }

    void ClipView::refreshIfChanged()
    {
        auto fingerprint = makeFingerprint();

        if (fingerprint != lastFingerprint)
        {
            lastFingerprint = std::move (fingerprint);
            updateBounds();
            repaint();
        }
    }

    //==============================================================================
    juce::Colour ClipView::colourFor (const te::Clip& c)
    {
        const bool hasOwnColour = c.state.hasProperty (Session::userColourProp);
        const auto trackColour = c.getTrack() != nullptr ? c.getTrack()->getColour() : juce::Colour();
        const auto base = hasOwnColour ? c.getColour()
                        : ! trackColour.isTransparent() ? trackColour
                        : dynamic_cast<const te::WaveAudioClip*> (&c) != nullptr ? Theme::clipAudio : Theme::clipMidi;

        return base.withSaturation (juce::jlimit (0.35f, 0.75f, base.getSaturation()))
                   .withBrightness (juce::jlimit (0.62f, 0.82f, base.getBrightness()));
    }

    void ClipView::paint (juce::Graphics& g)
    {
        if (! isStillInSong())
            return;

        const bool isSelected = session.isClipSelected (*clip);
        const bool isMuted = clip->isMuted();
        auto r = getLocalBounds();

        // A clip wears its own colour if the user picked one, else its track's (so a track's parts
        // read as one line, as in Ableton and Logic), else blue for audio and green for MIDI.
        // (Tracktion's own clip default is red for audio, so it's only used once someone sets one.)
        const bool isAudio = dynamic_cast<te::WaveAudioClip*> (clip.get()) != nullptr;
        const auto colour = isMuted ? juce::Colour (0xff474c57) : colourFor (*clip);
        const auto rf = r.toFloat();
        constexpr auto radius = (float) Theme::smallRadius;

        // Body: a dark tint of the colour, a touch lighter at the top
        {
            const auto bodyTop = Theme::laneBackground.interpolatedWith (colour, isMuted ? 0.15f : 0.30f);
            const auto bodyBottom = Theme::laneBackground.interpolatedWith (colour, isMuted ? 0.10f : 0.20f);
            g.setGradientFill (juce::ColourGradient::vertical (bodyTop, rf.getY(), bodyBottom, rf.getBottom()));
            g.fillRoundedRectangle (rf, radius);
        }

        const auto headerHeight = r.getHeight() >= 40 ? 16 : 13;
        auto titleBar = r.removeFromTop (headerHeight);

        {
            juce::Graphics::ScopedSaveState clipped (g);
            g.reduceClipRegion (titleBar);
            g.setColour (colour.withAlpha (isMuted ? 0.7f : 0.92f));
            g.fillRoundedRectangle (rf, radius);
        }

        // Dark text on light colours, white on dark ones (contrast first)
        const auto ink = colour.getPerceivedBrightness() > 0.62f ? Theme::ink : juce::Colours::white;
        g.setColour (ink.withAlpha (isMuted ? 0.6f : 1.0f));
        g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::semibold));
        auto titleText = titleBar.reduced (5, 0);
        const bool roomForText = titleText.getWidth() >= 14;   // zoomed far out: the colour alone

        if (const auto takes = session.getNumTakes (*clip); takes > 1 && roomForText && titleText.getWidth() > 90)
        {
            // Which take plays (T for the next one)
            const auto badge = "Take " + juce::String (session.getCurrentTake (*clip) + 1) + "/" + juce::String (takes);
            const auto badgeWidth = juce::jmin (titleText.getWidth() / 2, 64);
            g.drawText (badge, titleText.removeFromRight (badgeWidth), juce::Justification::centredRight, true);
        }

        // A small glyph before the name: a wave for audio, notes for MIDI
        if (roomForText && titleText.getWidth() > 40)
        {
            Icons::draw (g, isAudio ? Icon::speaker : Icon::note, titleText.removeFromLeft (11).toFloat().withSizeKeepingCentre (10.0f, 10.0f),
                         ink.withAlpha (isMuted ? 0.5f : 0.85f), 1.6f);
            titleText.removeFromLeft (4);
        }

        if (roomForText)
            g.drawText ((isMuted ? "[muted] " : "") + clip->getName(), titleText, juce::Justification::centredLeft, true);

        // Audio and MIDI look different at a glance: audio has a zero line and a filled wave,
        // MIDI faint rows like a piano roll and solid note bars
        if (isAudio && r.getHeight() > 12)
        {
            g.setColour (colour.withAlpha (0.25f));   // the zero line, so quiet passages still read as audio
            g.fillRect (r.reduced (1, 0).withSizeKeepingCentre (r.getWidth() - 2, 1));
        }
        else if (! isAudio && r.getHeight() > 24)
        {
            g.setColour (juce::Colours::black.withAlpha (0.12f));

            for (int y = r.getY() + 6; y < r.getBottom() - 2; y += 6)
                g.fillRect (r.getX() + 1, y, r.getWidth() - 2, 1);
        }

        const auto ink2 = colour.interpolatedWith (juce::Colours::white, isAudio ? 0.45f : 0.6f).withAlpha (isMuted ? 0.4f : 0.95f);

        g.setColour (ink2);

        if (isAudio)   // lighter at the peaks, deeper at the centre: the wave has body
        {
            juce::ColourGradient wave (ink2.brighter (0.15f), 0.0f, (float) r.getY(), ink2.brighter (0.15f), 0.0f, (float) r.getBottom(), false);
            wave.addColour (0.5, colour.interpolatedWith (juce::Colours::white, 0.2f).withAlpha (isMuted ? 0.4f : 0.95f));
            g.setGradientFill (wave);
        }

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip.get()))
            drawWaveform (g, *wave, r.reduced (1));
        else if (auto* midi = dynamic_cast<te::MidiClip*> (clip.get()))
            drawNotes (g, *midi, r.reduced (1, 2));

        // Fades: the faded part shaded, the curve, and a handle at each end of the header to drag
        if (dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr)
        {
            const auto fadeArea = getLocalBounds().withTrimmedTop (headerHeight).toFloat();
            const auto inX = fadeInX(), outX = fadeOutX();
            g.setColour (juce::Colours::black.withAlpha (0.28f));

            if (inX > 1.0f)
            {
                juce::Path shade;
                shade.startNewSubPath (fadeArea.getX(), fadeArea.getY());
                shade.lineTo (fadeArea.getX(), fadeArea.getBottom());
                shade.quadraticTo (fadeArea.getX() + (inX - fadeArea.getX()) * 0.5f, fadeArea.getBottom() - fadeArea.getHeight() * 0.1f, inX, fadeArea.getY());
                shade.closeSubPath();
                g.fillPath (shade);
            }

            if (outX < fadeArea.getRight() - 1.0f)
            {
                juce::Path shade;
                shade.startNewSubPath (fadeArea.getRight(), fadeArea.getY());
                shade.lineTo (fadeArea.getRight(), fadeArea.getBottom());
                shade.quadraticTo (outX + (fadeArea.getRight() - outX) * 0.5f, fadeArea.getBottom() - fadeArea.getHeight() * 0.1f, outX, fadeArea.getY());
                shade.closeSubPath();
                g.fillPath (shade);
            }

            if (hovering || isSelected || mode == DragMode::fadeIn || mode == DragMode::fadeOut)
            {
                g.setColour (ink.withAlpha (0.9f));

                for (auto x : { inX, outX })
                    g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ juce::jlimit (4.0f, (float) getWidth() - 4.0f, x), (float) headerHeight }));
            }

            // What's been done to it, in the corner: gain, reversed, transposed, follows the tempo
            juce::StringArray tags;

            if (const auto gain = session.getClipGainDb (*clip); std::abs (gain) >= 0.05f)
                tags.add ((gain > 0 ? "+" : "") + juce::String (gain, 1) + " dB");

            if (session.isClipReversed (*clip))           tags.add ("Rev");
            if (const auto st = juce::roundToInt (session.getClipTranspose (*clip)); st != 0)
                tags.add ((st > 0 ? "+" : "") + juce::String (st) + " st");
            if (session.isClipFollowingTempo (*clip))     tags.add ("Tempo");

            if (! tags.isEmpty() && getHeight() >= 34 && getWidth() > 60)
            {
                g.setColour (Theme::clipText.withAlpha (0.8f));
                g.setFont (Theme::font (Theme::captionFontSize, true));
                g.drawText (tags.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))),
                            getLocalBounds().reduced (5, 3).removeFromBottom (12), juce::Justification::bottomLeft, true);
            }
        }

        if (isSelected)
        {
            // Selected: lit up. A white edge, a soft inner glow, and a brighter tint over the body
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (rf.withTrimmedTop ((float) headerHeight), radius);
            g.setColour (Theme::selection.withAlpha (0.35f));
            g.drawRoundedRectangle (rf.reduced (2.5f), radius, 2.0f);
            g.setColour (Theme::selection);
            g.drawRoundedRectangle (rf.reduced (1.0f), radius, 1.5f);
        }
        else
        {
            g.setColour (colour.withAlpha (hovering ? 0.95f : 0.6f));
            g.drawRoundedRectangle (rf.reduced (0.5f), radius, 1.0f);
        }
    }

    float ClipView::fadeInX() const
    {
        const auto seconds = previewFadeIn >= 0.0 ? previewFadeIn : session.getClipFadeIn (*clip);
        return (float) juce::jlimit (0.0, (double) getWidth(), seconds * view.pixelsPerSecond);
    }

    float ClipView::fadeOutX() const
    {
        const auto seconds = previewFadeOut >= 0.0 ? previewFadeOut : session.getClipFadeOut (*clip);
        return (float) juce::jlimit (0.0, (double) getWidth(), getWidth() - seconds * view.pixelsPerSecond);
    }

    ClipView::DragMode ClipView::modeForPoint (juce::Point<int> p) const
    {
        // The fade handles sit on the line under the header: grab within a few pixels of one
        const auto headerHeight = getHeight() >= 40 ? 16 : 13;

        if (dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr && std::abs (p.y - headerHeight) <= 6 && getWidth() >= 24)
        {
            if (std::abs ((float) p.x - juce::jlimit (4.0f, (float) getWidth() - 4.0f, fadeInX())) <= 6.0f)   return DragMode::fadeIn;
            if (std::abs ((float) p.x - juce::jlimit (4.0f, (float) getWidth() - 4.0f, fadeOutX())) <= 6.0f)  return DragMode::fadeOut;
        }

        return modeForPosition (p.x);
    }

    void ClipView::drawWaveform (juce::Graphics& g, te::WaveAudioClip& wave, juce::Rectangle<int> area)
    {
        if (thumbnail == nullptr || area.isEmpty())
            return;

        // Which part of the audio file this clip plays (no time-stretch in Phase 2)
        const auto position = wave.getPosition();
        const auto fileStart = te::TimePosition() + position.getOffset();
        thumbnail->drawChannels (g, area, { fileStart, fileStart + position.getLength() },
                                 juce::jlimit (0.1f, 4.0f, juce::Decibels::decibelsToGain (wave.getGainDB())));   // louder clip, bigger wave
    }

    void ClipView::drawNotes (juce::Graphics& g, te::MidiClip& midi, juce::Rectangle<int> area)
    {
        const auto& notes = midi.getSequence().getNotes();

        if (notes.isEmpty() || area.isEmpty())
            return;

        // Fit the used pitch range into the clip's height
        int lowest = 127, highest = 0;

        for (auto* n : notes)
        {
            lowest = juce::jmin (lowest, n->getNoteNumber());
            highest = juce::jmax (highest, n->getNoteNumber());
        }

        const auto range = juce::jmax (12, highest - lowest + 1);
        const auto noteHeight = juce::jmax (1.0f, (float) area.getHeight() / (float) range);
        const auto clipStartX = view.timeToX (midi.getPosition().getStart());

        for (auto* n : notes)
        {
            // Content beats, so a clip trimmed at the start (offset > 0) draws its notes where they play
            const auto start = midi.getTimeOfContentBeat (n->getStartBeat());
            const auto end   = midi.getTimeOfContentBeat (n->getEndBeat());
            const auto x1 = (float) (view.timeToX (start) - clipStartX);
            const auto x2 = (float) (view.timeToX (end) - clipStartX);
            const auto y  = (float) area.getBottom() - (float) (n->getNoteNumber() - lowest + 1) * noteHeight;
            const auto note = juce::Rectangle<float> (x1, y, juce::jmax (1.5f, x2 - x1 - 1.0f), juce::jmax (1.5f, noteHeight - 1.0f));

            if (note.getWidth() >= 4.0f && note.getHeight() >= 3.0f)
                g.fillRoundedRectangle (note, juce::jmin (1.5f, note.getHeight() * 0.4f));
            else
                g.fillRect (note);
        }
    }

    //==============================================================================
    ClipView::DragMode ClipView::modeForPosition (int x) const
    {
        const auto edge = juce::jmin (maxEdgeWidth, getWidth() / 4);

        if (x < edge)                return DragMode::trimStart;
        if (x >= getWidth() - edge)  return DragMode::trimEnd;
        return DragMode::move;
    }

    te::TimePosition ClipView::snapped (te::TimePosition time) const
    {
        time = juce::jmax (te::TimePosition(), time);
        return view.snapEnabled ? session.snapToGrid (time, view.getGridBeats (session.getBpm())) : time;
    }

    void ClipView::mouseMove (const juce::MouseEvent& e)
    {
        if (view.tool != TimelineViewState::Tool::select && ! e.mods.isCtrlDown())
        {
            setMouseCursor (view.tool == TimelineViewState::Tool::split ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::CrosshairCursor);
            setTooltip (view.tool == TimelineViewState::Tool::split ? "Split tool: click to cut the clip here (hold Ctrl to move clips; Alt+1 for the select tool)"
                                                                     : "Erase tool: click to delete the clip (hold Ctrl to move clips; Alt+1 for the select tool)");
            return;
        }

        const auto m = modeForPoint (e.getPosition());
        setMouseCursor (m == DragMode::move ? juce::MouseCursor::NormalCursor
                        : m == DragMode::fadeIn || m == DragMode::fadeOut ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::LeftRightResizeCursor);
        setTooltip (m == DragMode::fadeIn || m == DragMode::fadeOut ? "Drag to fade (F: short fades on the selected clips)" : juce::String());
    }

    void ClipView::mouseDown (const juce::MouseEvent& e)
    {
        mode = DragMode::none;
        dragStarted = false;

        if (! isStillInSong())   // deleted, view not rebuilt yet
            return;

        view.focusedTrack = trackIndex;
        wasSelectedOnMouseDown = session.isClipSelected (*clip);

        if (e.mods.isPopupMenu())
        {
            if (! wasSelectedOnMouseDown)
                session.selectClip (clip.get());

            host.showClipMenu (*clip);
            return;
        }

        // The split and erase tools act on a click (Ctrl held: the select tool for a moment)
        if (view.tool != TimelineViewState::Tool::select && ! e.mods.isCtrlDown())
        {
            session.selectClip (clip.get());

            if (view.tool == TimelineViewState::Tool::split)
            {
                const auto inParent = e.getEventRelativeTo (getParentComponent()).getPosition();

                if (! session.splitClipsAt (snapped (view.xToTime (inParent.x))))
                    session.showHint (session.getLastError());
            }
            else if (! session.deleteSelectedClips())
            {
                session.showHint (session.getLastError());
            }

            return;   // this view may be gone now: touch nothing else
        }

        if (e.mods.isShiftDown())
        {
            session.toggleClipSelected (*clip);   // no drag from a Shift+click
            return;
        }

        if (! wasSelectedOnMouseDown)
            session.selectClip (clip.get());

        mode = modeForPoint (e.getPosition());
        previewFadeIn = previewFadeOut = -1.0;
        dragStartTime = clip->getPosition().getStart();
        trimPreviewTime = mode == DragMode::trimEnd ? clip->getPosition().getEnd() : dragStartTime;
        shift = {};
    }

    void ClipView::mouseDrag (const juce::MouseEvent& e)
    {
        if (mode == DragMode::none || ! isStillInSong())
            return;

        if (! dragStarted && e.getDistanceFromDragStart() < dragThreshold)
            return;   // a click, not a drag

        dragStarted = true;
        auto* parent = getParentComponent();

        if (parent == nullptr)
            return;

        const auto inParent = e.getEventRelativeTo (parent).getPosition();

        if (mode == DragMode::fadeIn || mode == DragMode::fadeOut)
        {
            const auto length = clip->getPosition().getLength().inSeconds();
            const auto x = juce::jlimit (0, getWidth(), e.x);

            if (mode == DragMode::fadeIn)
                previewFadeIn = juce::jlimit (0.0, length - session.getClipFadeOut (*clip), x / view.pixelsPerSecond);
            else
                previewFadeOut = juce::jlimit (0.0, length - session.getClipFadeIn (*clip), (getWidth() - x) / view.pixelsPerSecond);

            const auto seconds = mode == DragMode::fadeIn ? previewFadeIn : previewFadeOut;
            session.showHint ((mode == DragMode::fadeIn ? "Fade in " : "Fade out ")
                              + (seconds < 1.0 ? juce::String (juce::roundToInt (seconds * 1000)) + " ms" : juce::String (seconds, 2) + " s"));
            repaint();
            return;
        }

        if (mode == DragMode::move)
        {
            const auto newStart = snapped (dragStartTime + te::TimeDuration::fromSeconds (e.getDistanceFromDragStartX() / view.pixelsPerSecond));
            const auto targetTrack = juce::jlimit (0, juce::jmax (0, host.numTracks() - 1), view.trackAt (inParent.y));
            shift = host.previewSelectionShift (newStart - dragStartTime, targetTrack - trackIndex);
            setMouseCursor (e.mods.isCommandDown() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor);
            return;
        }

        // Trimming: preview the new edge directly
        previewing = true;
        trimPreviewTime = snapped (view.xToTime (inParent.x));
        const auto position = clip->getPosition();
        const auto x1 = view.timeToX (mode == DragMode::trimStart ? trimPreviewTime : position.getStart());
        const auto x2 = view.timeToX (mode == DragMode::trimEnd ? trimPreviewTime : position.getEnd());
        setBounds (juce::jmin (x1, x2 - 2), getY(), juce::jmax (2, x2 - x1), getHeight());
    }

    void ClipView::mouseUp (const juce::MouseEvent& e)
    {
        const auto finishedMode = std::exchange (mode, DragMode::none);

        if (finishedMode == DragMode::none || ! isStillInSong())
            return;

        if (! dragStarted)
        {
            // A plain click on one of several selected clips makes it the only selection
            if (wasSelectedOnMouseDown && session.getSelectedClips().size() > 1)
                session.selectClip (clip.get());

            return;
        }

        dragStarted = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);

        if (finishedMode == DragMode::fadeIn || finishedMode == DragMode::fadeOut)
        {
            const auto fadeIn = previewFadeIn >= 0.0 ? previewFadeIn : session.getClipFadeIn (*clip);
            const auto fadeOut = previewFadeOut >= 0.0 ? previewFadeOut : session.getClipFadeOut (*clip);
            previewFadeIn = previewFadeOut = -1.0;
            session.setClipFades (*clip, fadeIn, fadeOut);
            repaint();
            return;
        }

        if (finishedMode == DragMode::move)
        {
            host.endSelectionPreview();
            const auto [delta, trackDelta] = shift;

            if (delta != te::TimeDuration() || trackDelta != 0)   // dropped where it started: nothing to do
            {
                if (e.mods.isCommandDown())
                    session.copySelectedClipsBy (delta, trackDelta);
                else
                    session.moveSelectedClips (delta, trackDelta);

                host.clipsMayHaveChangedTrack();   // the lanes rebuild soon (not now: we're inside this view's mouseUp)
            }

            return;
        }

        if (finishedMode == DragMode::trimStart)
            session.trimClipStart (*clip, trimPreviewTime);
        else
            session.trimClipEnd (*clip, trimPreviewTime);

        endPreview();
    }

    void ClipView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu() && isStillInSong())
            host.editClip (*clip);
    }
}
