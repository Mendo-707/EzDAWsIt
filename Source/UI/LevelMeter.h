/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    A level meter: two bars (left, right), green to amber at -6 dB, red at 0 dB,
    a held-peak tick, and a clip light that stays on until you reset it.
    Draws what Session's MeterBank keeps; never measures anything itself.

      Click           reset this meter (clip light, held peak, highest level)
      Shift+click     reset every meter (also Ctrl+Shift+M)
      Right-click     reset, reset all, peak hold time; on the main meter also
                      what the number shows (short-term, momentary, integrated LUFS)
      Hover           exact values

    Vertical for track headers (and later the mixer), horizontal for the main
    meter in the transport (with a loudness number).
*/

#pragma once

#include "../Engine/Session.h"

namespace ez
{
    class LevelMeter : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
    {
    public:
        /** trackIndex -1 = the main output. */
        LevelMeter (Session&, int trackIndex, bool horizontal);

        enum class Loudness { shortTerm, momentary, integrated };

        /** Asked for by the "reset all" menu item (the app's command, so the shortcut shows). */
        std::function<void()> onResetAll;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        /** What the main meter's number shows (shared by every main meter). */
        static Loudness loudnessShown;

    private:
        Session& session;
        const int trackIndex;
        const bool horizontal;
        MeterBank::MainReading shown;

        bool isMain() const noexcept    { return trackIndex < 0; }
        MeterBank::MainReading read() const;
        void timerCallback() override;
        void reset();
        void showMenu();
        juce::String describe() const;
        void drawBar (juce::Graphics&, juce::Rectangle<float>, float levelDb, float holdDb) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
    };
}
