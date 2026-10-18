# Rubber Band Library v3, vendored (see SOURCE.md). Built from its single-file compilation unit:
# built-in FFT and resampler, vDSP (Accelerate) on Apple platforms, no internal threads.
add_library(rubberband STATIC single/RubberBandSingle.cpp)

# SYSTEM keeps the library's headers from raising warnings in our targets.
target_include_directories(rubberband SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Third-party code: build it quietly, our own targets keep their warnings.
if(MSVC)
    target_compile_options(rubberband PRIVATE /w)
    target_compile_definitions(rubberband PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(rubberband PRIVATE -w)
endif()

if(APPLE)
    # RubberBandSingle.cpp defines HAVE_VDSP itself on Apple; the FFT comes from Accelerate.
    target_link_libraries(rubberband PUBLIC "-framework Accelerate")
endif()
