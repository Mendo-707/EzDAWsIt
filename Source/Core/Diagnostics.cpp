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
            juce::Logger::writeToLog (juce::String (Diagnostics::crashLine) + "\n" + juce::SystemStats::getStackBacktrace());
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

    Diagnostics::LogSummary Diagnostics::summarise (const juce::File& log, const juce::String& appName)
    {
        LogSummary summary;
        juce::StringArray lines;
        log.readLines (lines);

        // The header is "<appName> <version> | <OS> | <time>"; tests and tools write "<appName> tests <version>"
        for (int i = 0; i < juce::jmin (6, lines.size()); ++i)
            if (lines[i].contains (" | "))
            {
                const auto words = juce::StringArray::fromTokens (lines[i].upToFirstOccurrenceOf (" | ", false, false), " ", "");
                summary.isAppRun = words.size() == 2 && words[0] == appName;
                break;
            }

        for (int i = lines.size(); --i >= 0;)
        {
            if (lines[i].trim().isEmpty())
                continue;

            summary.endedCleanly = lines[i].trim() == cleanShutdownLine;
            break;
        }

        summary.crashed = lines.contains (crashLine);
        return summary;
    }

    Diagnostics::Diagnostics (const juce::String& appName, const juce::String& appVersion)
    {
        AppPaths::createFolders();
        deleteOldLogs (AppPaths::getLogsFolder());

        // Did the app's last run end cleanly? (Checked before this run's log exists.)
        if (! appName.containsChar (' '))
        {
            auto logs = AppPaths::getLogsFolder().findChildFiles (juce::File::findFiles, false, "*.txt");
            std::sort (logs.begin(), logs.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getLastModificationTime() > b.getLastModificationTime(); });

            for (const auto& log : logs)
                if (const auto summary = summarise (log, appName); summary.isAppRun)
                {
                    if (! summary.endedCleanly)
                    {
                        uncleanPreviousLog = log;
                        previousCrashed = summary.crashed;
                    }

                    break;
                }
        }

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
        juce::Logger::writeToLog (cleanShutdownLine);
        juce::Logger::setCurrentLogger (nullptr);
    }

    juce::File Diagnostics::getLogFile() const
    {
        return logger != nullptr ? logger->getLogFile() : juce::File();
    }
}
