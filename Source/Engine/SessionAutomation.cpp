/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Session: automation lanes (see Session.h).

    How it maps onto Tracktion:
      - Every plugin control is an AutomatableParameter with an AutomationCurve (time-based for
        track plugins), saved inside the plugin. Curve values are in the parameter's own range; we
        show and take 0..1 of that range.
      - A point's curve bends the segment AFTER it; Tracktion doubles it and clamps to -1..1, so we
        store half of what the UI gives (-1..1 in, -0.5..0.5 stored).
      - Read/Write are the edit's AutomationRecordManager. Writing needs gestures: a parameter only
        records between parameterChangeGestureBegin/End while the transport plays.
      - Which lanes show is our own track property (ezAutoLanes), kept out of undo like view state.
*/

#include "Session.h"

namespace ez
{
    namespace
    {
        const juce::Identifier lanesProp ("ezAutoLanes");
        constexpr double minPointGap = 0.0005;   // seconds: neighbours never swap, so a dragged point keeps its index

        float toUnit (const te::AutomatableParameter& p, float native)
        {
            const auto range = p.getValueRange();
            return range.getLength() > 0.0f ? juce::jlimit (0.0f, 1.0f, (native - range.getStart()) / range.getLength()) : 0.0f;
        }

        float toNative (const te::AutomatableParameter& p, float unit)
        {
            const auto range = p.getValueRange();
            return range.getStart() + juce::jlimit (0.0f, 1.0f, unit) * range.getLength();
        }

        bool isHiddenPlugin (te::Plugin& plugin)
        {
            return dynamic_cast<te::LevelMeterPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::AuxReturnPlugin*> (&plugin) != nullptr
                || dynamic_cast<MasterTapPlugin*> (&plugin) != nullptr;
        }
    }

    Session::ParamRef Session::ParamRef::fromString (const juce::String& s)
    {
        ParamRef ref;
        ref.plugin = te::EditItemID::fromString (s.upToFirstOccurrenceOf (":", false, false));
        ref.paramID = s.fromFirstOccurrenceOf (":", false, false);
        return ref;
    }

    //==============================================================================
    te::AutomatableParameter* Session::findParam (int trackIndex, const ParamRef& ref) const
    {
        if (! ref.isValid() || edit == nullptr)
            return nullptr;

        const auto search = [&ref] (te::AudioTrack& track) -> te::AutomatableParameter*
        {
            for (auto* plugin : track.pluginList)
                if (plugin->itemID == ref.plugin)
                    return plugin->getAutomatableParameterByID (ref.paramID).get();

            return nullptr;
        };

        if (auto* track = getTrack (trackIndex))
            return search (*track);

        if (trackIndex < 0)   // anywhere (ending gestures when the track is unknown)
            for (auto* track : te::getAudioTracks (*edit))
                if (auto* p = search (*track))
                    return p;

        return nullptr;
    }

    std::vector<Session::ParamInfo> Session::getAutomatableParams (int trackIndex) const
    {
        std::vector<ParamInfo> params;
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return params;

        if (auto* volume = track->getVolumePlugin())
        {
            params.push_back ({ { volume->itemID, volume->volParam->paramID }, "Volume", "Track" });
            params.push_back ({ { volume->itemID, volume->panParam->paramID }, "Pan", "Track" });
        }

        for (auto* plugin : track->pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
            {
                auto* ret = findReturnTrack (send->busNumber.get());
                params.push_back ({ { send->itemID, send->gain->paramID },
                                    "Send to " + (ret != nullptr ? ret->getName() : "bus " + juce::String (send->busNumber.get() + 1)), "Sends" });
            }

        for (auto* plugin : track->pluginList)
        {
            if (plugin == track->getVolumePlugin() || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr || isHiddenPlugin (*plugin))
                continue;

            for (auto* p : plugin->getAutomatableParameters())
                params.push_back ({ { plugin->itemID, p->paramID }, p->getParameterName(), plugin->getName() });
        }

        return params;
    }

    Session::ParamRef Session::getVolumeParam (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;
        return volume != nullptr ? ParamRef { volume->itemID, volume->volParam->paramID } : ParamRef {};
    }

    Session::ParamRef Session::getPanParam (int trackIndex) const
    {
        auto* track = getTrack (trackIndex);
        auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;
        return volume != nullptr ? ParamRef { volume->itemID, volume->panParam->paramID } : ParamRef {};
    }

    Session::ParamRef Session::getSendParam (int trackIndex, int bus) const
    {
        auto* track = getTrack (trackIndex);
        auto* send = track != nullptr ? findSend (*track, bus) : nullptr;
        return send != nullptr ? ParamRef { send->itemID, send->gain->paramID } : ParamRef {};
    }

    juce::String Session::getParamName (int trackIndex, const ParamRef& ref) const
    {
        for (const auto& info : getAutomatableParams (trackIndex))
            if (info.ref == ref)
                return info.group == "Track" || info.group == "Sends" ? info.name : info.group + ": " + info.name;

        return {};
    }

    //==============================================================================
    std::vector<Session::AutoPoint> Session::getAutomationPoints (int trackIndex, const ParamRef& ref) const
    {
        std::vector<AutoPoint> points;

        if (auto* p = findParam (trackIndex, ref))
        {
            auto& curve = p->getCurve();

            for (int i = 0; i < curve.getNumPoints(); ++i)
                points.push_back ({ curve.getPointTime (i).inSeconds(), toUnit (*p, curve.getPointValue (i)),
                                    juce::jlimit (-1.0f, 1.0f, curve.getPointCurve (i) * 2.0f) });
        }

        return points;
    }

    bool Session::isAutomated (int trackIndex, const ParamRef& ref) const
    {
        auto* p = findParam (trackIndex, ref);
        return p != nullptr && p->hasAutomationPoints();
    }

    float Session::getParamValue (int trackIndex, const ParamRef& ref) const
    {
        auto* p = findParam (trackIndex, ref);
        return p != nullptr ? toUnit (*p, p->getCurrentValue()) : 0.0f;
    }

    float Session::getAutomationValueAt (int trackIndex, const ParamRef& ref, double seconds) const
    {
        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return 0.0f;

        if (! p->hasAutomationPoints())
            return toUnit (*p, p->getCurrentValue());

        return toUnit (*p, p->getCurve().getValueAt (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)), p->getCurrentValue()));
    }

    juce::String Session::formatParamValue (int trackIndex, const ParamRef& ref, float value) const
    {
        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return {};

        auto text = p->valueToString (toNative (*p, value));
        const auto label = p->getLabel();

        if (label.isNotEmpty() && ! text.endsWithIgnoreCase (label))
            text << " " << label;

        return text;
    }

    //==============================================================================
    int Session::addAutomationPoint (int trackIndex, const ParamRef& ref, double seconds, float value)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
        {
            fail ("That control isn't on the track any more");
            return -1;
        }

        beginUndoStep();
        auto& curve = p->getCurve();

        // The first point: also one at the start with today's value, or the whole song would jump to it
        if (curve.getNumPoints() == 0 && seconds > minPointGap)
            curve.addPoint (te::TimePosition(), p->getCurrentBaseValue(), 0.0f, &edit->getUndoManager());

        const auto index = curve.addPoint (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)),
                                           toNative (*p, value), 0.0f, &edit->getUndoManager());
        notifyStateChanged();   // the fader now follows the lane
        return index;
    }

    bool Session::moveAutomationPoint (int trackIndex, const ParamRef& ref, int index, double seconds, float value, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return fail ("That control isn't on the track any more");

        auto& curve = p->getCurve();
        const auto n = curve.getNumPoints();

        if (! juce::isPositiveAndBelow (index, n))
            return fail ("That point isn't there any more");

        // Between its neighbours: points never swap, so the caller's index stays right during a drag
        const auto earliest = index > 0 ? curve.getPointTime (index - 1).inSeconds() + minPointGap : 0.0;
        const auto latest = index < n - 1 ? curve.getPointTime (index + 1).inSeconds() - minPointGap : 1.0e7;
        const auto time = juce::jlimit (earliest, juce::jmax (earliest, latest), seconds);

        if (! continuesGesture)
            beginUndoStep();

        curve.movePoint (index, te::EditPosition (te::TimePosition::fromSeconds (time)), toNative (*p, value), std::nullopt, false, &edit->getUndoManager());
        return true;
    }

    bool Session::setAutomationCurve (int trackIndex, const ParamRef& ref, int index, float curveAmount, bool continuesGesture)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr || ! juce::isPositiveAndBelow (index, p->getCurve().getNumPoints()))
            return fail ("That point isn't there any more");

        if (! continuesGesture)
            beginUndoStep();

        p->getCurve().setCurveValue (index, juce::jlimit (-1.0f, 1.0f, curveAmount) * 0.5f, &edit->getUndoManager());

        if (! continuesGesture)
            notifyStateChanged();

        return true;
    }

    bool Session::addAutomationPoints (int trackIndex, const ParamRef& ref, const std::vector<AutoPoint>& newPoints)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return fail ("That control isn't on the track any more");

        if (newPoints.empty())
            return true;

        beginUndoStep();
        auto& curve = p->getCurve();

        for (const auto& point : newPoints)
        {
            const auto index = curve.addPoint (te::TimePosition::fromSeconds (juce::jmax (0.0, point.seconds)),
                                               toNative (*p, point.value), 0.0f, &edit->getUndoManager());

            if (juce::isPositiveAndBelow (index, curve.getNumPoints()) && point.curve != 0.0f)
                curve.setCurveValue (index, juce::jlimit (-1.0f, 1.0f, point.curve) * 0.5f, &edit->getUndoManager());
        }

        notifyStateChanged();
        return true;
    }

    bool Session::deleteAutomationPoints (int trackIndex, const ParamRef& ref, juce::Array<int> indexes)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return fail ("That control isn't on the track any more");

        indexes.sort();
        beginUndoStep();

        for (int i = indexes.size(); --i >= 0;)   // from the end, so the others keep their indexes
            if (juce::isPositiveAndBelow (indexes[i], p->getCurve().getNumPoints()) && (i == 0 || indexes[i - 1] != indexes[i]))
                p->getCurve().removePoint (indexes[i], &edit->getUndoManager());

        notifyStateChanged();
        return true;
    }

    bool Session::clearAutomation (int trackIndex, const ParamRef& ref)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return fail ("That control isn't on the track any more");

        beginUndoStep();
        p->getCurve().clear (&edit->getUndoManager());
        notifyStateChanged();
        return true;
    }

    //==============================================================================
    std::vector<Session::ParamRef> Session::getShownAutomationLanes (int trackIndex) const
    {
        std::vector<ParamRef> lanes;

        if (auto* track = getTrack (trackIndex))
            for (const auto& s : juce::StringArray::fromTokens (track->state[lanesProp].toString(), "|", {}))
                if (auto ref = ParamRef::fromString (s); findParam (trackIndex, ref) != nullptr   // its plugin may be gone
                     && std::find (lanes.begin(), lanes.end(), ref) == lanes.end())
                    lanes.push_back (ref);

        return lanes;
    }

    bool Session::setAutomationLaneShown (int trackIndex, const ParamRef& ref, bool shown)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr || (shown && findParam (trackIndex, ref) == nullptr))
            return fail ("That control isn't on the track any more");

        auto lanes = getShownAutomationLanes (trackIndex);
        const auto found = std::find (lanes.begin(), lanes.end(), ref);

        if (shown == (found != lanes.end()))
            return true;

        if (shown)
            lanes.push_back (ref);
        else
            lanes.erase (found);

        juce::StringArray tokens;

        for (const auto& r : lanes)
            tokens.add (r.toString());

        // Like scroll and zoom: saved with the song, but not an undo step
        track->state.setProperty (lanesProp, tokens.joinIntoString ("|"), nullptr);
        notifyStateChanged();
        return true;
    }

    bool Session::replaceAutomationLane (int trackIndex, int laneIndex, const ParamRef& ref)
    {
        auto lanes = getShownAutomationLanes (trackIndex);

        if (! juce::isPositiveAndBelow (laneIndex, (int) lanes.size()) || findParam (trackIndex, ref) == nullptr)
            return fail ("That control isn't on the track any more");

        lanes[(size_t) laneIndex] = ref;

        for (int j = (int) lanes.size(); --j >= 0;)   // never shown twice
            if (j != laneIndex && lanes[(size_t) j] == ref)
                lanes.erase (lanes.begin() + j);

        juce::StringArray tokens;

        for (const auto& r : lanes)
            tokens.add (r.toString());

        getTrack (trackIndex)->state.setProperty (lanesProp, tokens.joinIntoString ("|"), nullptr);
        notifyStateChanged();
        return true;
    }

    bool Session::toggleAutomationLanes (int trackIndex)
    {
        auto* track = getTrack (trackIndex);

        if (track == nullptr)
            return false;

        if (! getShownAutomationLanes (trackIndex).empty())
        {
            track->state.setProperty (lanesProp, juce::String(), nullptr);
            notifyStateChanged();
            return false;
        }

        // Show what is already automated; with nothing automated yet, the volume
        juce::StringArray tokens;

        for (const auto& info : getAutomatableParams (trackIndex))
            if (isAutomated (trackIndex, info.ref))
                tokens.add (info.ref.toString());

        if (tokens.isEmpty())
            if (auto volume = getVolumeParam (trackIndex); volume.isValid())
                tokens.add (volume.toString());

        track->state.setProperty (lanesProp, tokens.joinIntoString ("|"), nullptr);
        notifyStateChanged();
        return ! tokens.isEmpty();
    }

    //==============================================================================
    bool Session::isAutomationReadOn() const
    {
        return edit != nullptr && edit->getAutomationRecordManager().isReadingAutomation();
    }

    void Session::setAutomationRead (bool shouldRead)
    {
        if (edit != nullptr)
        {
            // Off: every control holds where the lane last put it, and you can move it by hand again
            edit->getAutomationRecordManager().setReadingAutomation (shouldRead);
            notifyStateChanged();
        }
    }

    bool Session::isAutomationWriteOn() const
    {
        return edit != nullptr && edit->getAutomationRecordManager().isWritingAutomation();
    }

    void Session::setAutomationWrite (bool shouldWrite)
    {
        if (edit != nullptr)
        {
            if (shouldWrite && ! isAutomationReadOn())
                edit->getAutomationRecordManager().setReadingAutomation (true);   // writing what you can't hear back makes no sense

            if (! shouldWrite)
                edit->getAutomationRecordManager().punchOut (false);   // finish what was being recorded first

            edit->getAutomationRecordManager().setWritingAutomation (shouldWrite);
            notifyStateChanged();
        }
    }

    void Session::beginParameterGesture (int trackIndex, const ParamRef& ref)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        auto* p = findParam (trackIndex, ref);

        if (p == nullptr)
            return;

        // A gesture left open (its fader was destroyed mid-drag) would block recording this control for good
        if (activeGestures.count (ref.toString()) > 0)
            p->parameterChangeGestureEnd();

        // Tracktion records only in a track's touch/latch/write mode; a new track is in read.
        // Touch: records while you hold the control. View-like state: no undo step
        if (auto* track = isAutomationWriteOn() ? getTrack (trackIndex) : nullptr)   // only when it can record: it is saved
            if (track->state[te::IDs::automationMode].toString() != te::toString (te::AutomationMode::touch))
                track->state.setProperty (te::IDs::automationMode, te::toString (te::AutomationMode::touch), nullptr);

        if (isAutomationWriteOn() && isPlaying())
            beginUndoStep();   // what gets recorded is one step

        activeGestures.insert (ref.toString());
        p->parameterChangeGestureBegin();
    }

    void Session::endParameterGesture (int trackIndex, const ParamRef& ref)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (activeGestures.erase (ref.toString()) == 0)
            return;

        if (auto* p = findParam (trackIndex, ref))
            p->parameterChangeGestureEnd();
    }

    void Session::endAllParameterGestures()
    {
        for (const auto& key : std::exchange (activeGestures, {}))
            if (auto* p = findParam (-1, ParamRef::fromString (key)))
                p->parameterChangeGestureEnd();
    }

    bool Session::isParamLockedByAutomation (int trackIndex, const ParamRef& ref) const
    {
        auto* p = findParam (trackIndex, ref);
        return p != nullptr && p->hasAutomationPoints() && isAutomationReadOn() && ! (isAutomationWriteOn() && isPlaying());
    }

    bool Session::refuseIfAutomated (te::AutomatableParameter* p, const juce::String& what)
    {
        if (p == nullptr || ! p->hasAutomationPoints() || ! isAutomationReadOn() || (isAutomationWriteOn() && isPlaying()))
            return false;

        showHint (what + " is automated: edit its lane, or turn on Write and move it while the song plays");
        lastError = hint;   // for callers that report it (keys, menus); not logged: a refused drag calls this per step
        return true;
    }

    //==============================================================================
    void Session::showHint (const juce::String& text)
    {
        hint = text;
        hintTime = juce::Time::getMillisecondCounter();
    }

    juce::String Session::getHint() const
    {
        return juce::Time::getMillisecondCounter() - hintTime < 4000 ? hint : juce::String();
    }
}
