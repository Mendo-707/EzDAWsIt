/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "Diagnostics.h"
#include "AppPaths.h"

namespace ez
{
    namespace
    {
        void crashHandler (void*)
        {
            juce::Logger::writeToLog ("*** CRASH ***\n" + juce::SystemStats::getStackBacktrace());
        }

        constexpr int maxLogFiles = 10;

        void deleteOldLogs (const juce::File& folder)
        {
            auto logs = folder.findChildFiles (juce::File::findFiles, false, "*.txt");

            if (logs.size() <= maxLogFiles)
                return;

            std::sort (logs.begin(), logs.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getLastModificationTime() < b.getLastModificationTime(); });

            for (int i = 0; i < logs.size() - maxLogFiles; ++i)
                logs.getReference (i).deleteFile();
        }
    }

    Diagnostics::Diagnostics (const juce::String& appName, const juce::String& appVersion)
    {
        AppPaths::createFolders();
        deleteOldLogs (AppPaths::getLogsFolder());

        auto file = AppPaths::getLogsFolder()
                        .getChildFile ("EzDAWsIt_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S") + ".txt");

        logger = std::make_unique<juce::FileLogger> (file, appName + " " + appVersion
                                                              + " | " + juce::SystemStats::getOperatingSystemName()
                                                              + " | " + juce::Time::getCurrentTime().toString (true, true));
        juce::Logger::setCurrentLogger (logger.get());
        juce::SystemStats::setApplicationCrashHandler (crashHandler);
    }

    Diagnostics::~Diagnostics()
    {
        juce::Logger::writeToLog ("Clean shutdown");
        juce::Logger::setCurrentLogger (nullptr);
    }

    juce::File Diagnostics::getLogFile() const
    {
        return logger != nullptr ? logger->getLogFile() : juce::File();
    }
}
