/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "StatusBar.h"
#include "../Theme.h"
#include "../Widgets.h"
#include "../CommandIDs.h"
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

        auto newDelay = delayText;

        if (tick++ % 4 == 0)   // the device rarely changes: once a second is plenty
        {
            newDevice = audioEngine.describeCurrentDevice() + "  >  " + audioEngine.getMainOutputName();
            const auto delay = session.getMonitoringDelay();
            newDelay = describeDelay (delay);
            delayColour = colourForDelay (delay);
            delayTip = explainDelay (delay, session.isLowLatencyMonitoring());
        }

        if (newMessage != message || newCpu != cpu || newDevice != device || newDelay != delayText)
        {
            delayText = std::move (newDelay);
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
        g.setColour (load > 0.9 ? Theme::danger : load > 0.7 ? Theme::warning : Theme::text);
        g.drawText (cpu, r.removeFromRight (cpuWidth), juce::Justification::centredRight, false);
        r.removeFromRight (16);

        // From playing to hearing: click for low-latency monitoring and the audio settings
        delayArea = {};

        if (delayText.isNotEmpty() && r.getWidth() > 420)
        {
            const auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), delayText) + 18;
            delayArea = r.removeFromRight (w);

            if (session.isLowLatencyMonitoring())
                Icons::draw (g, Icon::bolt, delayArea.toFloat().removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 12.0f), Theme::accent);

            g.setColour (delayColour);
            g.drawText (delayText, delayArea, juce::Justification::centredRight, false);
            r.removeFromRight (16);
        }

        // The device text shrinks (with "...") before the message does
        const auto deviceWidth = juce::jmin (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), device) + 4,
                                             r.getWidth() / 2);
        g.setColour (Theme::textDim);
        g.drawText (device, r.removeFromRight (deviceWidth), juce::Justification::centredRight, true);
        r.removeFromRight (16);

        g.setColour (Theme::text);
        g.drawText (message, r, juce::Justification::centredLeft, true);
    }

    //==============================================================================
    juce::String StatusBar::describeDelay (const Session::MonitoringDelay& d)
    {
        return d.hasDevice ? "Delay " + juce::String (d.totalMs(), 1) + " ms" : juce::String();
    }

    juce::Colour StatusBar::colourForDelay (const Session::MonitoringDelay& d)
    {
        const auto ms = d.totalMs();
        return ms > 25.0 ? Theme::danger : ms > 12.0 ? Theme::warning : Theme::textDim;
    }

    juce::String StatusBar::explainDelay (const Session::MonitoringDelay& d, bool lowLatencyOn)
    {
        if (! d.hasDevice)
            return {};

        juce::String s;
        s << "From playing to hearing: " << juce::String (d.totalMs(), 1) << " ms\n"
          << "Audio driver: " << d.driver << ", " << juce::String (d.inputMs, 1) << " ms in + " << juce::String (d.outputMs, 1)
          << " ms out (buffer " << d.bufferSamples << " samples, " << juce::String (d.bufferMs, 1) << " ms)\n"
          << "Effects: " << juce::String (d.effectsMs, 1) << " ms (every track waits for the slowest)";

        for (int i = 0; i < juce::jmin (4, d.effects.size()); ++i)
            s << "\n   " << d.effects[i];

        if (! d.lowLatencyDriver)
            s << "\n\n" << d.driver << " adds delay: pick ASIO in Setup > Audio device, buffer 64-128.";
        else if (d.bufferSamples > 256)
            s << "\n\nA smaller buffer (64-128) in Setup > Audio device brings this down.";

        if (d.effectsMs > Session::lowLatencyLimitMs && ! lowLatencyOn)
            s << "\n\nLow-latency monitoring (Ctrl+Alt+L) switches the delaying effects off while you play.";

        s << "\n\nClick for low-latency monitoring and the audio settings.";
        return s;
    }

    juce::String StatusBar::getTooltip()
    {
        return delayArea.contains (getMouseXYRelative()) ? delayTip : juce::String();
    }

    void StatusBar::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (delayArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void StatusBar::mouseUp (const juce::MouseEvent& e)
    {
        if (commands == nullptr || ! delayArea.contains (e.getPosition()))
            return;

        juce::PopupMenu menu;
        menu.addCommandItem (commands, CommandIDs::toggleLowLatency);
        menu.addCommandItem (commands, CommandIDs::audioSetup, "Audio device settings...");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (delayArea)));
    }
}
