/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Mix view: one channel strip per track, then the Master strip. Every
    control is the same track data as the arrange track headers (nothing is a
    copy), changed through Session.

    Controls (AGENTS.md rule 0a):
      Header      Meters: Peak (RMS planned: it must not hide clips);
                  Show inputs/outputs; Narrow strips; Sends (Ctrl+Shift+E);
                  + FX return (menu: reverb Ctrl+Shift+R, delay, empty);
                  + Bus (menu: empty Ctrl+Shift+B, the highlighted track into a new one Ctrl+Shift+G)
      Strip       click: highlight the track (same as in the arrange);
                  name: double-click to rename, right-click for the track menu
                  (rename, duplicate, move left/right, colour, delete);
                  input / output choosers;
                  inserts: click opens the effect, an empty slot adds one,
                  right-click: open, bypass, move up/down, remove, add;
                  pan: drag, wheel, double-click centres;
                  fader: drag (Shift: fine), wheel, double-click 0 dB, click the
                  dB value to type one; meter: click resets, Shift+click resets all;
                  the peak number (highest since reset): click resets;
                  M / S / R;
                  sends (one knob per FX return): drag (Shift: fine), wheel, double-click
                  or Alt+click 0 dB; right-click: 0/-6/-12 dB, pre/post fader, mute,
                  remove, go to the return; click the row to pick it for [ / ];
                  "+N" when they don't all fit: every send in a menu;
                  FX return / bus strips: a chip on the name, what plays into them
                  instead of the input chooser
      Master      volume and pan of the whole mix, the main meter and loudness
      Keys        (after clicking in the mixer) Left / Right: previous / next strip;
                  Up / Down: fader +/- 1 dB (Shift: 0.1); M: mute; S: solo;
                  [ / ]: the picked send -/+ 1 dB (Shift: 0.1)
      Wheel       scrolls the strips sideways (over a fader or knob: changes it)
*/

#pragma once

#include "../Engine/Session.h"
#include "Widgets.h"
#include "Shell/EditorTarget.h"

namespace ez
{
    class MixerView : public juce::Component,
                      private Session::Listener,
                      private juce::Timer
    {
    public:
        explicit MixerView (Session&);
        ~MixerView() override;

        /** Shared with the arrange: which track is highlighted. */
        std::function<int()> getFocusedTrack;
        std::function<void (int)> setFocusedTrack;
        std::function<void (const juce::String& title, const juce::String& message)> onProblem;
        std::function<void()> showMasterView;   // the master strip's insert: the mastering chain lives there

        /** Keys with no app command (Up/Down, M, S). True if used. */
        bool handleKey (const juce::KeyPress&);

        /** Left/Right (the app's nudge commands) while the mixer has the keyboard. */
        void moveFocus (int direction);

        bool hasEditFocus() const       { return focus.hasFocus() && isShowing(); }

        void setShowSends (bool);
        bool isShowingSends() const     { return showSends; }

        /** Highlights a track and scrolls its strip into view (after adding a bus or return, say). */
        void revealTrack (int trackIndex);

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        class Strip;

    private:
        Session& session;
        ClickFocusTracker focus { *this };

        IconButton peakButton { "Peak meters", Icon::none, "Peak" };
        IconButton rmsButton { "RMS meters", Icon::none, "RMS" };
        IconButton ioButton { "Show inputs and outputs", Icon::none, "Inputs/outputs" };
        IconButton narrowButton { "Narrow strips", Icon::none, "Narrow" };
        IconButton sendsButton { "Show sends", Icon::none, "Sends" };
        IconButton addReturnButton { "Add FX return", Icon::plus, "FX return" };
        IconButton addBusButton { "Add bus", Icon::plus, "Bus" };

        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<Strip> strips;
        std::unique_ptr<Strip> masterStrip;
        juce::Array<te::EditItemID> builtFor;
        juce::Rectangle<int> headerArea;
        bool showIO = true, narrow = false, showSends = true;
        int ticks = 0, numReturns = 0;
        int pickedSendBus = -1;   // the send [ and ] change (the last one touched)

        /** What fits in a strip of this height, the same for every strip so the faders line up. */
        struct Rows { bool io = false; int slots = 0, sends = 0; };
        Rows computeRows (int stripHeight) const;
        Rows rows;   // for the current strip height (layoutStrips)
        void showAddReturnMenu();
        void showAddBusMenu();

        void report (const juce::String& message);
        void repaintStrips();
        void rebuildIfNeeded();
        void layoutStrips();
        void setMeterMode (bool rms);
        void sessionSongChanged() override       { rebuildIfNeeded(); }
        void sessionStateChanged() override      { rebuildIfNeeded(); }
        void timerCallback() override;

        friend class Strip;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
    };
}
