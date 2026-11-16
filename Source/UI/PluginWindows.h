/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    Engine UI hooks: plugin editor windows. Implemented in TracktionExampleBridge.cpp
    (reuses Tracktion's GPLv3 example PluginWindow).
*/

#pragma once

#include "../Core/Common.h"

namespace ez
{
    std::unique_ptr<te::UIBehaviour> createUIBehaviour();
}
