/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "UiMode.h"

namespace ez::UiMode
{
    namespace
    {
        const juce::String settingName ("ezProView");

        struct State
        {
            te::Engine* engine = nullptr;
            bool pro = true;
            juce::ChangeBroadcaster broadcaster;
        };

        State& state()
        {
            static State s;
            return s;
        }
    }

    void initialise (te::Engine& engine)
    {
        state().engine = &engine;
        state().pro = engine.getPropertyStorage().getPropertiesFile().getBoolValue (settingName, true);
    }

    bool isPro()
    {
        return state().pro;
    }

    void setPro (bool pro)
    {
        if (state().pro == pro)
            return;

        state().pro = pro;

        if (state().engine != nullptr)
            state().engine->getPropertyStorage().getPropertiesFile().setValue (settingName, pro);

        state().broadcaster.sendSynchronousChangeMessage();
    }

    juce::ChangeBroadcaster& getBroadcaster()
    {
        return state().broadcaster;
    }
}
