# libmp3lame only (no frontend, no decoder, no SIMD), static, for MP3 export (docs/spec/04-export.md, task 4.3).
file(GLOB DAW_LAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libmp3lame/*.c)
add_library(mp3lame STATIC ${DAW_LAME_SOURCES})
target_include_directories(mp3lame
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/config ${CMAKE_CURRENT_SOURCE_DIR}/libmp3lame)
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
set_target_properties(mp3lame PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
# Upstream code: its own warnings are not ours to fix.
if(MSVC)
    target_compile_options(mp3lame PRIVATE /w)
else()
    target_compile_options(mp3lame PRIVATE -w)
endif()
