set(PRISMA_FORMATS VST3 Standalone)
option(PRISMA_COPY_AFTER_BUILD "Install Prisma plugin after building" OFF)
if(APPLE)
    list(PREPEND PRISMA_FORMATS AU)
endif()

juce_add_plugin(Prisma
    PRODUCT_NAME "Prisma"
    COMPANY_NAME "DAW Project"
    BUNDLE_ID "com.dawproject.prisma"
    PLUGIN_MANUFACTURER_CODE Dawp
    PLUGIN_CODE Prsm
    FORMATS ${PRISMA_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${PRISMA_COPY_AFTER_BUILD})

target_sources(Prisma PRIVATE
    Source/PluginEntry.cpp
    Source/PrismaProcessor.cpp
    Source/PrismaEditor.cpp)

target_compile_definitions(Prisma PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Prisma
    PRIVATE
        daw_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
