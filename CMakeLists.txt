cmake_minimum_required(VERSION 3.22)

if(APPLE)
    # One universal binary for Apple Silicon and Intel Macs.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(DAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DAW_BUILD_TESTS "Build core unit tests" ON)
option(DAW_BUILD_TOOLS "Build the offline renderer" ON)
option(DAW_BUILD_JUCE "Build the JUCE app and plugins (needs JUCE)" ON)
# Replaces the global operator new/delete and counts heap use on the audio thread (docs/spec/00-rules.md, section 2).
# Always on in the engine tests; for the app it is a debugging aid.
option(DAW_RT_CHECKS "Count heap allocations on the audio thread" OFF)

if(DAW_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(DAW_BUILD_TOOLS)
    add_executable(prisma_render tools/prisma_render.cpp)
    target_link_libraries(prisma_render PRIVATE daw_core)
endif()

if(DAW_BUILD_JUCE)
    # JUCE is fetched automatically. To use a local checkout instead:
    #   cmake -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE ...
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    # Fonts (Onest and JetBrains Mono, SIL OFL 1.1) and theme files, embedded as BinaryData.
    juce_add_binary_data(DawResources SOURCES
        app/Resources/fonts/Onest-Regular.ttf
        app/Resources/fonts/Onest-Medium.ttf
        app/Resources/fonts/Onest-SemiBold.ttf
        app/Resources/fonts/JetBrainsMono-Regular.ttf
        app/Resources/fonts/JetBrainsMono-Bold.ttf
        app/Resources/themes/prism.json
        app/Resources/themes/neon.json
        app/Resources/themes/studio.json
        app/Resources/themes/paper.json)

    # Sources shared by the app and the engine tests: model, settings, engine, theme, built-in Prisma.
    set(DAW_APP_SHARED_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/Settings.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/ModelRefs.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/ProjectModel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/ProjectActions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/ClipEdit.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/ArrangementEdit.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/Automation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/model/TrackTransfer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/project/ProjectFile.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/project/RecoveryCheck.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/project/ProjectSession.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/project/MidiImport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/library/UserLibrary.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/browser/BrowserCatalog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/browser/BrowserSources.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/browser/BrowserActions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/Theme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/UiCommon.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/Controls.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/Chrome.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/EditCommands.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ComputerKeys.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/TransportMenus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementLanes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/Waveform.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/InputMenus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/DrumPads.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ExportWindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/Spectrum.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/AudioClipEditor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/PianoRoll.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/PianoRollGrid.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/PianoRollLanes.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/PianoRollProps.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/QuantiseWindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/EffectViews.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/DetailPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementRuler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementHeaders.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementMinimap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ArrangementPlayhead.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/TopBar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/ScalePicker.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/AudioEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/Metronome.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/SessionRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/ArrangementRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/EngineGraph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/RtCheck.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/PreviewPlayer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/AudioFilePool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/AudioPreview.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/RecordStream.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/BuiltInEffectProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/DrumKitProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/OfflineRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/engine/AudioRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/project/AudioImport.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/prisma/Source/PrismaProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/prisma/Source/PrismaEditor.cpp)

    add_subdirectory(plugins/prisma)
    add_subdirectory(app)

    if(DAW_BUILD_TESTS)
        juce_add_console_app(daw_engine_tests PRODUCT_NAME "daw_engine_tests")
        target_sources(daw_engine_tests PRIVATE
            tests/EngineTests.cpp
            tests/ModelTests.cpp
            tests/GraphTests.cpp
            tests/HostingTests.cpp
            tests/UiTests.cpp
            tests/ProjectTests.cpp
            tests/BrowserTests.cpp
            tests/EditorTests.cpp
            tests/RecordingTests.cpp
            tests/ArrangementTests.cpp
            tests/ArrangementUiTests.cpp
            tests/ArrangementRecordingTests.cpp
            tests/AudioTests.cpp
            tests/AudioUiTests.cpp
            tests/SessionAudioUiTests.cpp
            tests/AudioRecordingTests.cpp
            tests/EffectUiTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app/Source/ui/SessionView.cpp
            ${DAW_APP_SHARED_SOURCES})
        target_compile_definitions(daw_engine_tests PRIVATE
            JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
            DAW_RT_CHECKS=1 JUCE_MODAL_LOOPS_PERMITTED=1
            # the VST3 built from plugins/prisma in this same build, hosted by the hosting test
            DAW_TEST_VST3_DIR="${CMAKE_BINARY_DIR}/plugins/prisma/Prisma_artefacts/$<CONFIG>/VST3")
        add_dependencies(daw_engine_tests Prisma_VST3)
        target_link_libraries(daw_engine_tests PRIVATE daw_core DawResources juce::juce_audio_utils
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME daw_engine_tests COMMAND daw_engine_tests)
    endif()
endif()
