/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "StatusBar.h"
#include "../Theme.h"
#include "../../Engine/AudioEngine.h"

namespace ez
{
    StatusBar::StatusBar (AudioEngine& e, Session& s) : audioEngine (e), session (s)
    {
        startTimerHz (4);
    }

    juce::String StatusBar::describeState() const
    {
        if (! session.hasSong())
            return "No song open. Ctrl+N starts one.";

        if (const auto hint = session.getHint(); hint.isNotEmpty())
            return hint;   // why something didn't happen (an automated fader, say)

        if (session.isRecording())
            return "Recording. Space stops.";

        const auto selected = session.getSelectedClips();

        if (selected.size() == 1)
        {
            auto* clip = selected.getFirst();
            const auto trackName = clip->getClipTrack() != nullptr ? clip->getClipTrack()->getName() : juce::String();
            if (dynamic_cast<te::AudioClipBase*> (clip) != nullptr)
                return "Selected: " + clip->getName() + " on " + trackName + ". F fades, Ctrl+Shift+N normalises, Alt+Up/Down transposes, "
                       "Ctrl+J follows the tempo, Ctrl+E splits.";

            return "Selected: " + clip->getName() + " on " + trackName + ". Ctrl+D duplicates, arrows nudge, Ctrl+E splits.";
        }

        if (selected.size() > 1)
            return juce::String (selected.size()) + " clips selected. Ctrl+C copies, Delete removes, drag moves them all.";

        return session.isPlaying() ? "Playing. Space stops." : "Ready. Space plays, R records.";
    }

    void StatusBar::timerCallback()
    {
        auto newMessage = describeState();
        auto newCpu = "CPU " + juce::String (juce::roundToInt (audioEngine.getDeviceManager().getCpuUsage() * 100.0)) + "%";
        auto newDevice = device;

        if (tick++ % 4 == 0)   // the device rarely changes: once a second is plenty
            newDevice = audioEngine.describeCurrentDevice() + "  >  " + audioEngine.getMainOutputName();

        if (newMessage != message || newCpu != cpu || newDevice != device)
        {
            message = std::move (newMessage);
            cpu = std::move (newCpu);
            device = std::move (newDevice);
            repaint();
        }
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::statusBar);
        g.setColour (Theme::laneSeparator);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        auto r = getLocalBounds().reduced (10, 0);
        g.setFont (Theme::font (Theme::statusFontSize));

        const auto cpuWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "CPU 100%") + 4;
        // Amber when the computer is working hard, red when the sound may start to crackle
        const auto load = audioEngine.getDeviceManager().getCpuUsage();
        g.setColour (load > 0.9 ? Theme::danger : load > 0.7 ? juce::Colour (0xfff2b84c) : Theme::text);
        g.drawText (cpu, r.removeFromRight (cpuWidth), juce::Justification::centredRight, false);
        r.removeFromRight (16);

        // The device text shrinks (with "...") before the message does
        const auto deviceWidth = juce::jmin (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), device) + 4,
                                             r.getWidth() / 2);
        g.setColour (Theme::textDim);
        g.drawText (device, r.removeFromRight (deviceWidth), juce::Justification::centredRight, true);
        r.removeFromRight (16);

        g.setColour (Theme::text);
        g.drawText (message, r, juce::Justification::centredLeft, true);
    }
}
