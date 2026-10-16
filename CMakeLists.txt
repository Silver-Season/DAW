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

    add_subdirectory(plugins/prisma)
    add_subdirectory(app)

    if(DAW_BUILD_TESTS)
        juce_add_console_app(daw_engine_tests PRODUCT_NAME "daw_engine_tests")
        target_sources(daw_engine_tests PRIVATE
            tests/EngineTests.cpp
            app/Source/AudioEngine.cpp
            plugins/prisma/Source/PrismaProcessor.cpp
            plugins/prisma/Source/PrismaEditor.cpp)
        target_compile_definitions(daw_engine_tests PRIVATE
            JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(daw_engine_tests PRIVATE daw_core juce::juce_audio_utils juce::juce_recommended_config_flags)
        add_test(NAME daw_engine_tests COMMAND daw_engine_tests)
    endif()
endif()
