/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    What part of the song the timeline shows (scroll + zoom) and how it snaps.
    Pure view state: never saved in the song, never undoable.
*/

#pragma once

#include "../../Core/Common.h"

namespace ez
{
    struct TimelineViewState
    {
        static constexpr double minPixelsPerSecond = 2.0, maxPixelsPerSecond = 2000.0;
        static constexpr int minGridSpacingPixels = 16;
        static constexpr int defaultTrackHeight = 84, minTrackHeight = 44, maxTrackHeight = 240;
        static constexpr int automationLaneHeight = 48;

        double viewStartSeconds = 0.0;
        double pixelsPerSecond = 60.0;
        bool snapEnabled = true;
        int trackHeight = defaultTrackHeight;
        int focusedTrack = 0;              // where Paste goes; the last track clicked

        /** How many automation lanes show under each track (the timeline sets it when it lays out).
            A track's row is its clip lane (trackHeight) and then its automation lanes. */
        std::vector<int> lanesPerTrack;

        int lanesOf (int trackIndex) const          { return juce::isPositiveAndBelow (trackIndex, (int) lanesPerTrack.size()) ? lanesPerTrack[(size_t) trackIndex] : 0; }
        int rowHeight (int trackIndex) const        { return trackHeight + lanesOf (trackIndex) * automationLaneHeight; }

        int topOfTrack (int trackIndex) const
        {
            int y = 0;

            for (int i = 0; i < trackIndex; ++i)
                y += rowHeight (i);

            return y;
        }

        int topOfLane (int trackIndex, int lane) const  { return topOfTrack (trackIndex) + trackHeight + lane * automationLaneHeight; }

        /** Track under a y position, NOT clamped (can be past the last track: rows there are trackHeight). */
        int trackAt (int y) const
        {
            if (y < 0)
                return -1;

            int top = 0;

            for (int i = 0;; ++i)
            {
                top += juce::jmax (1, rowHeight (i));

                if (y < top)
                    return i;
            }
        }

        /** Grid size in seconds at a tempo (constant tempo in Phase 2). */
        double getGridSeconds (double bpm) const    { return getGridBeats (bpm) * 60.0 / juce::jmax (1.0, bpm); }

        int timeToX (te::TimePosition t) const
        {
            return juce::roundToInt ((t.inSeconds() - viewStartSeconds) * pixelsPerSecond);
        }

        te::TimePosition xToTime (int x) const
        {
            return te::TimePosition::fromSeconds (juce::jmax (0.0, viewStartSeconds + x / pixelsPerSecond));
        }

        /** The finest grid (in beats) whose lines stay at least minGridSpacingPixels apart. */
        double getGridBeats (double bpm) const
        {
            const auto pixelsPerBeat = pixelsPerSecond * 60.0 / juce::jmax (1.0, bpm);

            for (auto beats : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 })
                if (beats * pixelsPerBeat >= minGridSpacingPixels)
                    return beats;

            return 128.0;
        }

        void setTrackHeight (int newHeight)         { trackHeight = juce::jlimit (minTrackHeight, maxTrackHeight, newHeight); }

        /** Zooms by `factor`, keeping the time at pixel `anchorX` where it is. */
        void zoom (double factor, int anchorX)
        {
            const auto anchorTime = viewStartSeconds + anchorX / pixelsPerSecond;
            pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, pixelsPerSecond * factor);
            viewStartSeconds = juce::jmax (0.0, anchorTime - anchorX / pixelsPerSecond);
        }
    };
}
