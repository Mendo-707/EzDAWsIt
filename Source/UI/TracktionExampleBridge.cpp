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

            /** Tracktion's offline jobs (freezing a track) run through here. The default does nothing,
                so freezing silently rendered nothing ("has nothing to freeze"). Runs the job on its own
                thread behind a progress window, which keeps the song from being edited mid-render. */
            void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
            {
                struct Progress : public juce::ThreadWithProgressWindow
                {
                    explicit Progress (te::ThreadPoolJobWithProgress& t)
                        : juce::ThreadWithProgressWindow (t.getJobName(), true, false, 30000), job (t) {}

                    void run() override
                    {
                        while (! threadShouldExit())
                        {
                            if (job.runJob() == juce::ThreadPoolJob::jobHasFinished)
                                break;

                            setProgress (job.getCurrentTaskProgress());
                        }
                    }

                    te::ThreadPoolJobWithProgress& job;
                };

                // The progress window takes keyboard focus: hand it back, or every shortcut is dead afterwards
                juce::Component::SafePointer<juce::Component> previous (juce::TopLevelWindow::getActiveTopLevelWindow());

                {
                    Progress progress (task);
                    progress.runThread();
                }

                if (previous != nullptr && previous->isShowing())
                    previous->toFront (true);
            }
        };
    }

    std::unique_ptr<te::UIBehaviour> createUIBehaviour()
    {
        return std::make_unique<EzUIBehaviour>();
    }
}
