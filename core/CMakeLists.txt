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
    src/gen/Patterns.cpp
    src/gen/Patches.cpp)

target_include_directories(daw_core PUBLIC include)
target_compile_features(daw_core PUBLIC cxx_std_20)
set_target_properties(daw_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

    add_executable(daw_core_audio_tests tests/audio_tests.cpp)
    target_link_libraries(daw_core_audio_tests PRIVATE daw_core)
    add_test(NAME daw_core_audio_tests COMMAND daw_core_audio_tests)
    if(NOT MSVC)
        target_compile_options(daw_core_audio_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()
