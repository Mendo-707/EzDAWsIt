/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "WindowChrome.h"
#include "Theme.h"
#include <set>

namespace ez::WindowChrome
{
    namespace
    {
        juce::uint32 colourRef (juce::Colour c)
        {
            return (juce::uint32) c.getRed() | ((juce::uint32) c.getGreen() << 8) | ((juce::uint32) c.getBlue() << 16);
        }

       #if JUCE_WINDOWS
        // Loaded by name, so there's nothing to link and older Windows simply says no
        using SetAttribute = long (__stdcall*) (void* window, unsigned long attribute, const void* value, unsigned long size);

        SetAttribute dwmSetAttribute()
        {
            static juce::DynamicLibrary dwm ("dwmapi.dll");
            static auto* fn = (SetAttribute) dwm.getFunction ("DwmSetWindowAttribute");
            return fn;
        }

        enum : unsigned long
        {
            useImmersiveDarkMode = 20,   // Windows 10 2004 and later: dark caption buttons and frame
            borderColour = 34,           // Windows 11 22000 and later
            captionColour = 35,
            textColour = 36
        };
       #endif

        /** Tints each new top-level window: checked when the focus moves and twice a second (cheap: a few windows). */
        class Watcher : private juce::Timer,
                        private juce::FocusChangeListener,
                        public juce::DeletedAtShutdown
        {
        public:
            Watcher()
            {
                juce::Desktop::getInstance().addFocusChangeListener (this);
                startTimer (500);
                tintNewWindows();
            }

            ~Watcher() override
            {
                juce::Desktop::getInstance().removeFocusChangeListener (this);
            }

        private:
            std::set<void*> tinted;

            void tintNewWindows()
            {
                auto& desktop = juce::Desktop::getInstance();

                // Forget closed windows first: a new window may reuse a closed one's handle
                for (auto it = tinted.begin(); it != tinted.end();)
                {
                    bool open = false;

                    for (int i = 0; i < desktop.getNumComponents() && ! open; ++i)
                        if (auto* c = desktop.getComponent (i))
                            if (auto* peer = c->getPeer())
                                open = peer->getNativeHandle() == *it;

                    it = open ? std::next (it) : tinted.erase (it);
                }

                // Only windows with a system title bar: menus, tooltips and our custom-titled windows keep their own look
                for (int i = 0; i < desktop.getNumComponents(); ++i)
                    if (auto* c = desktop.getComponent (i))
                        if (auto* peer = c->getPeer(); peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowHasTitleBar) != 0
                                                          && tinted.insert (peer->getNativeHandle()).second)
                            apply (*c);
            }

            void timerCallback() override                               { tintNewWindows(); }
            void globalFocusChanged (juce::Component*) override         { tintNewWindows(); }
        };
    }

    Colours colours()
    {
        return { colourRef (Theme::bar), colourRef (Theme::text), colourRef (Theme::hairline) };
    }

    bool apply (juce::Component& window)
    {
       #if JUCE_WINDOWS
        auto* peer = window.getPeer();
        auto* set = dwmSetAttribute();

        if (peer == nullptr || set == nullptr)
            return false;

        auto* handle = peer->getNativeHandle();
        const int dark = 1;
        const auto c = colours();
        const bool ok = set (handle, useImmersiveDarkMode, &dark, sizeof (dark)) == 0;

        // Windows 11: the caption the same colour as our app bar (Windows 10 ignores these and keeps dark mode)
        set (handle, captionColour, &c.caption, sizeof (c.caption));
        set (handle, textColour, &c.text, sizeof (c.text));
        set (handle, borderColour, &c.border, sizeof (c.border));
        return ok;
       #else
        juce::ignoreUnused (window);
        return false;
       #endif
    }

    void install()
    {
        static Watcher* watcher = nullptr;

        if (watcher == nullptr)
            watcher = new Watcher();   // DeletedAtShutdown deletes it
    }
}
