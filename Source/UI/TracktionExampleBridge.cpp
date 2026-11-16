/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.

    THE ONLY FILE that includes Tracktion's example code
    (deps/tracktion_engine/examples/common). Phase 2B replaced the example
    timeline with UI/Timeline; only the plugin window code is still used.
*/

#include "PluginWindows.h"

using namespace juce;
using namespace tracktion::literals;
using namespace std::literals;

#include "Utilities.h"
#include "PluginWindow.h"

#include "ParamEditor.h"

namespace ez
{
    namespace
    {
        /** VST3s show their own editor; everything else (Tracktion's built-ins and ours) our knobs. */
        struct EzUIBehaviour : public ExtendedUIBehaviour
        {
            std::unique_ptr<Component> createPluginWindow (te::PluginWindowState& pws) override
            {
                if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
                    if (dynamic_cast<te::ExternalPlugin*> (&ws->plugin) == nullptr && ws->plugin.createEditor() == nullptr
                         && ! ws->plugin.getAutomatableParameters().isEmpty())   // nothing to show (Drum Machine, Sampler): no window
                        return ParamEditor::createWindow (ws->plugin);

                return ExtendedUIBehaviour::createPluginWindow (pws);
            }
        };
    }

    std::unique_ptr<te::UIBehaviour> createUIBehaviour()
    {
        return std::make_unique<EzUIBehaviour>();
    }
}
