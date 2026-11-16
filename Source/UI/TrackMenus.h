/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Menus shared by every view of a track (arrange header, mixer strip), so a
    track offers the same actions wherever you right-click it.
*/

#pragma once

#include "../Engine/Session.h"

namespace ez::TrackMenus
{
    struct NamedColour { const char* name; juce::Colour colour; };
    const std::vector<NamedColour>& getTrackColours();

    struct Callbacks
    {
        std::function<void()> rename;                 // start renaming in place
        std::function<void (int newIndex)> moved;     // the track moved: follow it
        std::function<void (const juce::String&)> problem;
        bool horizontal = false;                      // "Move left/right" (mixer) instead of up/down
        std::function<void (int trackIndex)> showTrack;   // "Go to the return": highlight (and scroll to) a track
    };

    /** The app's "open the Tuner on this track" (Ctrl+U), for the "Tuner..." item. Set once by the main window. */
    void setTunerOpener (std::function<void (int trackIndex)>);

    /** Rename, duplicate, move, colour, output, sends, new bus/return, delete. */
    void showTrackMenu (Session&, int trackIndex, Callbacks);

    /** A small box asking for a name ("Save as track preset"); onName runs only with a non-empty name. */
    void askForName (const juce::String& title, const juce::String& message, const juce::String& suggestion,
                     std::function<void (const juce::String&)> onName);

    /** Saving and applying presets for a track: an "Effect chains" submenu and the two saves. IDs base + 0..999. */
    /** Returns the chains as listed, for performPresetItem (the list may change while the menu is open). */
    juce::Array<Session::PresetInfo> addPresetItems (juce::PopupMenu&, Session&, int trackIndex, int base);
    bool performPresetItem (Session&, int trackIndex, int result, int base, const juce::Array<Session::PresetInfo>& chains,
                            std::function<void (const juce::String&)> problem);

    /** Asks for a preset name; if one of yours already has it, asks before replacing it. */
    void askForPresetName (Session&, Session::PresetKind, const juce::String& title, const juce::String& message,
                           const juce::String& suggestion, std::function<void (const juce::String&)> onName);

    /** What a send can do (right-click on a send knob, and each send in the track menu). Item IDs are base + 1..9. */
    juce::PopupMenu makeSendMenu (Session&, int trackIndex, int bus, int base, const juce::String& header = {});
    /** Runs an item of makeSendMenu (result - base). False + Session::getLastError() if refused. */
    bool performSendAction (Session&, int trackIndex, int bus, int action, const Callbacks&);

    /** Effects grouped by category (Dynamics, EQ & Filter, Space...), then a VST3 submenu (or a hint to scan).
        Item IDs are base + 1 + the choice's index in `choices`. */
    void addEffectChoices (juce::PopupMenu&, const juce::Array<Session::PluginChoice>& choices, int base);

    /** "+ FX return" choices: Reverb (Ctrl+Shift+R), Delay, Empty. Returns the new track or -1. */
    juce::PopupMenu makeAddReturnMenu (int base);
    int performAddReturn (Session&, int action);

    /** For one insert slot: open, bypass, move, remove; an empty slot adds an effect. */
    void showInsertMenu (Session&, int trackIndex, int effectIndex, juce::Component& target,
                         std::function<void (const juce::String&)> problem, std::function<void()> changed);
}
