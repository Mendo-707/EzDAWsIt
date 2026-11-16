/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    One clip on the timeline: draws its waveform or notes, and handles the mouse:
      Click                select it (the only selection)
      Shift + click        add to / remove from the selection
      Drag the middle      move every selected clip (in time and between tracks)
      Ctrl + drag          copy instead of move
      Drag an edge         trim the clip's start or end (the audio stays in place)
      Right-click          clip menu (cut, copy, split, rename, colour...)
      Double-click         MIDI clip: edit its notes in the piano roll
    Snaps to the grid when snap is on. Every change goes through Session.
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"

namespace ez
{
    class ClipView : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        /** The colour a clip is drawn in, everywhere (timeline, piano roll): its own if the user picked
            one, else its track's, else blue for audio and green for MIDI. Evened out so every clip has
            the same weight on screen. */
        static juce::Colour colourFor (const te::Clip&);

        /** What the clip needs from the lanes it sits in. */
        struct Host
        {
            virtual ~Host() = default;
            virtual int numTracks() const = 0;

            /** Shows every selected clip shifted (a drag preview). Returns the shift actually allowed. */
            virtual std::pair<te::TimeDuration, int> previewSelectionShift (te::TimeDuration, int trackDelta) = 0;
            virtual void endSelectionPreview() = 0;
            virtual void showClipMenu (te::Clip&) = 0;
            virtual void clipsMayHaveChangedTrack() = 0;
            virtual void editClip (te::Clip&) = 0;
        };

        ClipView (Session&, TimelineViewState&, Host&, te::Clip&, int trackIndex);
        ~ClipView() override;

        te::Clip& getClip() noexcept                    { return *clip; }
        int getTrackIndex() const noexcept              { return trackIndex; }

        /** Positions this view from the clip's time and track (not while it's being dragged). */
        void updateBounds();

        /** Cheap check, called often: repositions/repaints only if the clip changed. */
        void refreshIfChanged();

        /** Drag preview, driven by whichever clip is being dragged. */
        void showPreviewShift (te::TimeDuration, int trackDelta);
        void endPreview();

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        enum class DragMode { none, move, trimStart, trimEnd, fadeIn, fadeOut };

        Session& session;
        TimelineViewState& view;
        Host& host;
        te::Clip::Ptr clip;
        int trackIndex;
        std::unique_ptr<te::SmartThumbnail> thumbnail;

        bool previewing = false;            // this view is showing a drag preview: don't follow the clip
        DragMode mode = DragMode::none;
        bool dragStarted = false;
        bool wasSelectedOnMouseDown = false;
        te::TimePosition dragStartTime, trimPreviewTime;
        std::pair<te::TimeDuration, int> shift;
        juce::String lastFingerprint;
        double previewFadeIn = -1.0, previewFadeOut = -1.0;   // while a fade handle is dragged
        bool hovering = false;
        void mouseEnter (const juce::MouseEvent&) override  { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { hovering = false; repaint(); }
        float fadeInX() const;
        float fadeOutX() const;
        DragMode modeForPoint (juce::Point<int>) const;

        juce::String makeFingerprint() const;
        bool isStillInSong() const          { return clip->getClipTrack() != nullptr; }
        DragMode modeForPosition (int x) const;
        te::TimePosition snapped (te::TimePosition) const;
        void drawWaveform (juce::Graphics&, te::WaveAudioClip&, juce::Rectangle<int>);
        void drawNotes (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipView)
    };
}
