/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    The Vocal Chain's card in the Devices panel (Engine/VocalChain.h):

      Header      On/bypass, the preset (pick one: all macros and switches at once),
                  Chain (show what's inside), Open (every control as knobs), move, Remove
      Macros      Clean, Tune, Punch, Bright, Space, Width: drag (Shift: fine), wheel,
                  double-click: the preset's value; right-click: 0 / 50 / 100 %, the preset's
                  value, show its automation lane. Each says what it does underneath.
      Inside      (Chain on: shown instead of the macros) the ten modules in order: click one to
                  switch it off or on; Key: what Tune pulls to (or take the song's key); Phone tone
*/

#pragma once

#include "../../Engine/Session.h"
#include "../Widgets.h"

namespace ez
{
    class VocalChainCard : public juce::Component, private juce::Timer
    {
    public:
        /** `act` runs a Session call only if the card's effect is still where it was (and reports failures). */
        VocalChainCard (Session&, int trackIndex, int effectIndex, Session::DeviceInfo,
                        std::function<void (std::function<bool()>, const juce::String& title)> act,
                        int numEffects);
        ~VocalChainCard() override;

        static constexpr int preferredWidth = 420;   // fits the Devices panel at the 900 px minimum window

        IconButton bypassButton { "Bypass", Icon::power, {}, IconButton::Style::ghost };
        IconButton openButton   { "Open", Icon::none, "Open" };
        IconButton removeButton { "Remove", Icon::close, {}, IconButton::Style::ghost };
        IconButton leftButton   { "Move earlier", Icon::chevronLeft, {}, IconButton::Style::ghost };
        IconButton rightButton  { "Move later", Icon::chevronRight, {}, IconButton::Style::ghost };

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        class Macro;
        class ModuleChip;

        Session& session;
        const int trackIndex, effectIndex;
        const Session::DeviceInfo info;
        std::function<void (std::function<bool()>, const juce::String&)> act;

        juce::ComboBox presetBox;
        IconButton chainButton { "Show chain", Icon::chain, "Chain" };
        juce::OwnedArray<Macro> macros;
        juce::OwnedArray<ModuleChip> modules;
        juce::TextButton keyButton, phoneButton { "Phone tone" };
        juce::Rectangle<int> insideArea;
        static inline bool showInside = false;   // remembered while the app runs, for every chain card

        void refresh();
        bool stillHere() const;   // the effect slot still holds this very chain
        void showKeyMenu();
        void timerCallback() override   { if (isShowing()) refresh(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainCard)
    };
}
