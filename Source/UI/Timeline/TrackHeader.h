/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Left-hand strip for one track, laid out as the design's track header (docs/ui/mockup-generator.py,
    track_header):

      | colour | (icon) Name  device ........ [BUS] [FX 2] |  meter
      |  bar   | [M][S][R] ..................... [A]      |
      |        | volume ======o------  -4.0 dB   (pan) C  |

    A short track (under 80 px) folds rows 2 and 3 into one. Right-click for the track menu (rename,
    duplicate, delete, move, colour). Everything goes through Session.
*/

#pragma once

#include "../../Engine/Session.h"
#include "TimelineViewState.h"
#include "../LevelMeter.h"
#include "../Widgets.h"

namespace ez
{
    class TrackHeader : public juce::Component
    {
    public:
        TrackHeader (Session&, TimelineViewState&, int trackIndex);
        ~TrackHeader() override;   // ends a fader gesture cut short by a rebuild

        /** Re-reads every control from the song (called when the song changes). */
        void refresh();

        bool isRenaming() const         { return name.isBeingEdited(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        Session& session;
        TimelineViewState& view;
        const int trackIndex;
        bool wasFocused = false;
        bool volumeDragging = false, panDragging = false;
        juce::Colour lastColour;
        Session::TrackKind kind = Session::TrackKind::normal;
        juce::Rectangle<int> kindChip, iconArea, deviceArea, dbArea, panLabelArea;
        Icon typeIcon = Icon::mic;
        juce::String deviceText, lastInputID, lastInputName;
        bool inputNameKnown = false;
        juce::String laidOutName;   // the name resized() last measured

        juce::Label name;
        juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, fxButton { "FX" }, autoButton { "A" };
        juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        LevelMeter meter;   // the track's level after volume and pan

        void showFxMenu();
        void showTrackMenu();
        void showProblem (const juce::String& message);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
    };
}
