cmake_minimum_required(VERSION 3.22)

project(EzDAWsIt VERSION 1.0.0)

# EzDAWsIt is a Windows app: build.bat builds it, and the installer ships it
if (NOT WIN32)
    message(FATAL_ERROR "EzDAWsIt builds for Windows only (run build.bat).")
endif()

# Static runtime: one .exe that runs on any Windows PC without extra installs
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(EZ_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/deps")
set(ASIO_SDK_DIR "" CACHE PATH "Folder of the Steinberg ASIO SDK (contains common/iasiodrv.h)")

# JUCE and Tracktion Engine are downloaded into deps/ by build.bat (pinned commits)
add_subdirectory("${EZ_DEPS}/JUCE" "${CMAKE_BINARY_DIR}/juce")
add_subdirectory("${EZ_DEPS}/tracktion_engine/modules" "${CMAKE_BINARY_DIR}/tracktion")

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

juce_add_gui_app(EzDAWsIt
    PRODUCT_NAME "EzDAWsIt"
    COMPANY_NAME "EzDAW"
    COMPANY_COPYRIGHT "Copyright (C) 2026 EzDAW. Free software under the GNU GPL v3."
    VERSION "${PROJECT_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(EzDAWsIt)

target_compile_features(EzDAWsIt PRIVATE cxx_std_20)

# --- Sources, by layer (see AGENTS.md) ---------------------------------------
set(EZ_SOURCES
    # App
    Source/Main.cpp
    # Core: no audio, no UI
    Source/Core/Common.h
    Source/Core/AppPaths.h          Source/Core/AppPaths.cpp
    Source/Core/Version.h
    Source/Core/Diagnostics.h       Source/Core/Diagnostics.cpp
    # Engine: audio + song model, no UI
    Source/Engine/AudioEngine.h     Source/Engine/AudioEngine.cpp
    Source/Engine/Session.h         Source/Engine/Session.cpp
    Source/Engine/SessionMix.cpp
    Source/Engine/SessionAutomation.cpp
    Source/Engine/SessionEffects.cpp
    Source/Engine/SessionExport.cpp Source/Engine/SessionExportJobs.h
    Source/Engine/SessionMaster.cpp Source/Engine/LoudnessScan.h
    Source/Engine/SampleLibrary.h Source/Engine/SampleLibrary.cpp
    Source/Engine/MidiLearn.h     Source/Engine/MidiLearn.cpp
    Source/Engine/SessionAudioEdit.cpp
    Source/Engine/SessionPresets.cpp
    Source/Engine/Dsp.h
    Source/Engine/Effects.h         Source/Engine/Effects.cpp
    Source/Engine/PluginPresets.h   Source/Engine/PluginPresets.cpp
    Source/Engine/VocalChain.h      Source/Engine/VocalChain.cpp
    Source/Engine/Mastering.h Source/Engine/Mastering.cpp
    Source/Engine/DrumKits.h        Source/Engine/DrumKits.cpp
    Source/Engine/DrumMachinePlugin.h Source/Engine/DrumMachinePlugin.cpp
    Source/Engine/Meters.h          Source/Engine/Meters.cpp
    # UI
    Source/UI/CommandIDs.h
    Source/UI/Theme.h               Source/UI/Theme.cpp
    Source/UI/Dialogs.h             Source/UI/Dialogs.cpp
    Source/UI/RoutingView.h         Source/UI/RoutingView.cpp
    Source/UI/Widgets.h             Source/UI/Widgets.cpp
    Source/UI/LevelMeter.h          Source/UI/LevelMeter.cpp
    Source/UI/TrackMenus.h          Source/UI/TrackMenus.cpp
    Source/UI/TunerView.h           Source/UI/TunerView.cpp
    Source/UI/RecordingSetup.h      Source/UI/RecordingSetup.cpp
    Source/UI/MidiSetup.h           Source/UI/MidiSetup.cpp
    Source/UI/HelpViews.h           Source/UI/HelpViews.cpp
    Source/UI/UiMode.h              Source/UI/UiMode.cpp
    Source/UI/MixerView.h           Source/UI/MixerView.cpp
    Source/UI/SetupView.h           Source/UI/SetupView.cpp
    Source/UI/Shell/AppBar.h        Source/UI/Shell/AppBar.cpp
    Source/UI/Shell/TransportBar.h  Source/UI/Shell/TransportBar.cpp
    Source/UI/Shell/BrowserPanel.h  Source/UI/Shell/BrowserPanel.cpp
    Source/UI/Shell/DetailPanel.h   Source/UI/Shell/DetailPanel.cpp
    Source/UI/Shell/DevicesView.h   Source/UI/Shell/DevicesView.cpp
    Source/UI/Shell/VocalChainCard.h Source/UI/Shell/VocalChainCard.cpp
    Source/UI/Shell/PianoRollView.h Source/UI/Shell/PianoRollView.cpp
    Source/UI/Shell/DrumMachineView.h Source/UI/Shell/DrumMachineView.cpp
    Source/UI/Shell/EditorTarget.h
    Source/UI/Shell/StatusBar.h     Source/UI/Shell/StatusBar.cpp
    Source/UI/MainComponent.h       Source/UI/MainComponent.cpp
    Source/UI/ParamEditor.h         Source/UI/ParamEditor.cpp
    Source/UI/PluginLayouts.h     Source/UI/PluginLayouts.cpp
    Source/UI/PluginDisplays.h    Source/UI/PluginDisplays.cpp
    Source/UI/PluginLook.h        Source/UI/PluginLook.cpp
    Source/UI/InterfaceScale.h      Source/UI/InterfaceScale.cpp
    Source/UI/ExportView.h          Source/UI/ExportView.cpp
    Source/UI/MasterView.h          Source/UI/MasterView.cpp
    Source/UI/PluginWindows.h       Source/UI/TracktionExampleBridge.cpp
    Source/UI/Timeline/TimelineViewState.h
    Source/UI/Timeline/Ruler.h      Source/UI/Timeline/Ruler.cpp
    Source/UI/Timeline/TempoLane.h  Source/UI/Timeline/TempoLane.cpp
    Source/UI/Timeline/SectionsLane.h Source/UI/Timeline/SectionsLane.cpp
    Source/UI/Timeline/TrackHeader.h Source/UI/Timeline/TrackHeader.cpp
    Source/UI/Timeline/ClipView.h   Source/UI/Timeline/ClipView.cpp
    Source/UI/Timeline/AutomationLane.h Source/UI/Timeline/AutomationLane.cpp
    Source/UI/Timeline/TimelineView.h Source/UI/Timeline/TimelineView.cpp
    # Tests (run with: EzDAWsIt.exe --run-tests results.txt)
    Source/Tests/TestRunner.h       Source/Tests/TestRunner.cpp
    Source/Tests/Screenshots.h      Source/Tests/Screenshots.cpp
    Source/Tests/CoreTests.cpp)

target_sources(EzDAWsIt PRIVATE ${EZ_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${EZ_SOURCES})

# Tracktion's example UI helpers are visible ONLY to the bridge file
set_source_files_properties(Source/UI/TracktionExampleBridge.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${EZ_DEPS}/tracktion_engine/examples/common")

target_compile_definitions(EzDAWsIt PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EzDAWsIt,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EzDAWsIt,JUCE_VERSION>"
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

if (ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    message(STATUS "EzDAWsIt: ASIO enabled from ${ASIO_SDK_DIR}")
    target_include_directories(EzDAWsIt PRIVATE "${ASIO_SDK_DIR}/common")
    target_compile_definitions(EzDAWsIt PRIVATE JUCE_ASIO=1)
else()
    message(STATUS "EzDAWsIt: ASIO SDK not found, using WASAPI/DirectSound only")
endif()

target_link_libraries(EzDAWsIt PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

if (MSVC)
    # Release builds keep a .pdb next to the exe so crash logs show function names (no speed cost)
    target_compile_options(EzDAWsIt PRIVATE /bigobj /MP /utf-8 $<$<CONFIG:Release>:/Zi>)
    target_link_options(EzDAWsIt PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    # Highest warning level for OUR code only (JUCE/Tracktion module code is not ours)
    set_source_files_properties(${EZ_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
endif()
