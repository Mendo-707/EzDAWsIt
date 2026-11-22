/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Low-latency monitoring, and the delay you hear when playing live.

    Tracktion keeps every track in time by delaying all of them as much as the slowest chain (plugin
    delay compensation). A track playing live goes through that too: one 20 ms pitch corrector on a
    vocal makes the guitar you're playing 20 ms late. Low-latency monitoring switches such effects
    off while it's on, except on armed tracks, where you're playing.

    Skipping an effect is not an edit. It's done on the plugin's state without the undo manager and
    marked (latencyBypassProp), so: turning the mode off puts back exactly what was on; fingerprints
    (unsaved changes, autosave) ignore it; and a song saved while effects were skipped opens with
    them on again.
*/

#include "Session.h"
#include "AudioEngine.h"

namespace ez
{
    namespace
    {
        const juce::Identifier latencyBypassProp ("ezLatencyBypass");
        const char* lowLatencySettingKey = "ezLowLatencyMonitoring";
    }

    bool Session::isSkippedForLowLatency (const te::Plugin& plugin)
    {
        return plugin.state.hasProperty (latencyBypassProp);
    }

    void Session::clearLatencyMarks (juce::ValueTree& tree)
    {
        if (tree.hasProperty (latencyBypassProp))
        {
            tree.setProperty (te::IDs::enabled, true, nullptr);
            tree.removeProperty (latencyBypassProp, nullptr);
        }

        for (auto child : tree)
            clearLatencyMarks (child);
    }

    void Session::setLowLatencyMonitoring (bool shouldBeOn)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (lowLatency == shouldBeOn)
            return;

        lowLatency = shouldBeOn;
        audioEngine.getEngine().getPropertyStorage().getPropertiesFile().setValue (lowLatencySettingKey, shouldBeOn);
        log (juce::String ("Low-latency monitoring ") + (shouldBeOn ? "on" : "off"));
        applyLowLatency();
        notifyStateChanged();
    }

    void Session::applyLowLatency()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (edit == nullptr || applyingLowLatency || isRecording())   // never rebuild the graph mid-take
            return;

        const juce::ScopedValueSetter<bool> guard (applyingLowLatency, true);
        bool changed = false;

        const auto apply = [this, &changed] (te::Plugin& plugin, bool onArmedTrack)
        {
            const auto marked = isSkippedForLowLatency (plugin);
            const auto slow = plugin.getLatencySeconds() * 1000.0 > lowLatencyLimitMs;
            const auto skip = lowLatency && slow && ! onArmedTrack && (marked || plugin.isEnabled());

            if (skip && ! marked)
            {
                plugin.state.setProperty (te::IDs::enabled, false, nullptr);
                plugin.state.setProperty (latencyBypassProp, true, nullptr);
                changed = true;
            }
            else if (! skip && marked)
            {
                plugin.state.setProperty (te::IDs::enabled, true, nullptr);
                plugin.state.removeProperty (latencyBypassProp, nullptr);
                changed = true;
            }
        };

        for (int t = 0; t < getNumAudioTracks(); ++t)
            for (auto* plugin : getEffects (t))
                apply (*plugin, isTrackArmed (t));

        for (auto* plugin : edit->getMasterPluginList().getPlugins())
            if (! plugin->isSynth())
                apply (*plugin, false);

        if (changed)
            edit->restartPlayback();
    }

    Session::MonitoringDelay Session::getMonitoringDelay() const
    {
        MonitoringDelay d;

        if (auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice())
        {
            const auto rate = device->getCurrentSampleRate();

            if (rate > 0)
            {
                d.hasDevice = true;
                d.driver = device->getTypeName();
                d.lowLatencyDriver = d.driver == "ASIO";
                d.bufferSamples = device->getCurrentBufferSizeSamples();
                d.bufferMs = 1000.0 * d.bufferSamples / rate;
                d.inputMs = 1000.0 * device->getInputLatencyInSamples() / rate;
                d.outputMs = 1000.0 * device->getOutputLatencyInSamples() / rate;
            }
        }

        if (edit == nullptr)
            return d;

        // Every track waits for the slowest chain; the master's own delay comes on top
        struct Source { juce::String text; double ms; };
        std::vector<Source> sources;
        double slowestTrack = 0.0, master = 0.0;

        for (int t = 0; t < getNumAudioTracks(); ++t)
        {
            double chain = 0.0;

            if (auto* track = getTrack (t))
            {
                for (auto* plugin : track->pluginList)
                {
                    if (! plugin->isEnabled())
                        continue;

                    const auto ms = plugin->getLatencySeconds() * 1000.0;
                    chain += ms;

                    if (ms >= 0.5)
                        sources.push_back ({ plugin->getName() + " on " + track->getName() + ": " + juce::String (ms, 1) + " ms", ms });
                }
            }

            slowestTrack = juce::jmax (slowestTrack, chain);
        }

        for (auto* plugin : edit->getMasterPluginList().getPlugins())
        {
            if (! plugin->isEnabled())
                continue;

            const auto ms = plugin->getLatencySeconds() * 1000.0;
            master += ms;

            if (ms >= 0.5)
                sources.push_back ({ plugin->getName() + " on the master: " + juce::String (ms, 1) + " ms", ms });
        }

        std::stable_sort (sources.begin(), sources.end(), [] (const Source& a, const Source& b) { return a.ms > b.ms; });

        for (const auto& s : sources)
            d.effects.add (s.text);

        d.effectsMs = slowestTrack + master;
        return d;
    }
}
