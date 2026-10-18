# Framework-free core: DSP, Prisma synth engine, clips and transport. Builds and tests without JUCE.
add_library(daw_core STATIC
    src/FFT.cpp
    src/Wavetable.cpp
    src/PrismaParams.cpp
    src/PrismaSynth.cpp
    src/Clip.cpp
    src/Arrangement.cpp
    src/AudioClip.cpp
    src/Recording.cpp
    src/Capture.cpp
    src/Scales.cpp
    src/DemoContent.cpp
    src/VersionHistory.cpp
    src/SincInterpolator.cpp
    src/AudioAnalysis.cpp
    src/TimeStretch.cpp
    src/Onsets.cpp
    src/gen/Patterns.cpp
    src/gen/Patches.cpp
    src/drums/DrumKit.cpp
    src/drums/DrumSynth.cpp)

# Built-in effects: one file per effect, plus the shared base and the registry.
file(GLOB DAW_FX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/fx/*.cpp)
target_sources(daw_core PRIVATE ${DAW_FX_SOURCES})

target_include_directories(daw_core PUBLIC include)
target_compile_features(daw_core PUBLIC cxx_std_20)
set_target_properties(daw_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Rubber Band stays an implementation detail of TimeStretch.cpp; its headers are not part of core's interface.
target_link_libraries(daw_core PRIVATE rubberband)

if(MSVC)
    target_compile_options(daw_core PRIVATE /W4)
else()
    target_compile_options(daw_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(DAW_BUILD_TESTS)
    add_executable(daw_core_tests tests/core_tests.cpp)
    target_link_libraries(daw_core_tests PRIVATE daw_core)
    add_test(NAME daw_core_tests COMMAND daw_core_tests)

    add_executable(daw_gen_tests tests/gen_tests.cpp)
    target_link_libraries(daw_gen_tests PRIVATE daw_core)
    add_test(NAME daw_gen_tests COMMAND daw_gen_tests)
    if(NOT MSVC)
        target_compile_options(daw_gen_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    add_executable(daw_drum_tests tests/drum_tests.cpp)
    target_link_libraries(daw_drum_tests PRIVATE daw_core)
    add_test(NAME daw_drum_tests COMMAND daw_drum_tests)
    if(NOT MSVC)
        target_compile_options(daw_drum_tests PRIVATE -Wall -Wextra)
    endif()

    add_executable(daw_core_audio_tests tests/audio_tests.cpp)
    target_link_libraries(daw_core_audio_tests PRIVATE daw_core)
    add_test(NAME daw_core_audio_tests COMMAND daw_core_audio_tests)
    if(NOT MSVC)
        target_compile_options(daw_core_audio_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    add_executable(daw_stretch_tests tests/stretch_tests.cpp)
    target_link_libraries(daw_stretch_tests PRIVATE daw_core)
    add_test(NAME daw_stretch_tests COMMAND daw_stretch_tests)
    if(NOT MSVC)
        target_compile_options(daw_stretch_tests PRIVATE -Wall -Wextra)
    endif()

    add_executable(daw_analysis_tests tests/analysis_tests.cpp)
    target_link_libraries(daw_analysis_tests PRIVATE daw_core)
    add_test(NAME daw_analysis_tests COMMAND daw_analysis_tests)
    if(NOT MSVC)
        target_compile_options(daw_analysis_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    # Effect tests: one file per effect registers its cases (FX_TEST), the main runs them all.
    file(GLOB DAW_FX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/fx/*.cpp)
    add_executable(daw_fx_tests ${DAW_FX_TEST_SOURCES})
    target_link_libraries(daw_fx_tests PRIVATE daw_core)
    add_test(NAME daw_fx_tests COMMAND daw_fx_tests)
    if(NOT MSVC)
        target_compile_options(daw_fx_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()
