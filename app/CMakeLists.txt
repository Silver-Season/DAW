juce_add_gui_app(DAW
    PRODUCT_NAME "DAW"
    COMPANY_NAME "DAW Project"
    BUNDLE_ID "com.dawproject.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DAW needs audio input access to record.")

target_sources(DAW PRIVATE
    Source/Main.cpp
    Source/AudioEngine.cpp
    Source/MainComponent.cpp
    # Prisma is compiled into the DAW as its built-in instrument.
    ../plugins/prisma/Source/PrismaProcessor.cpp
    ../plugins/prisma/Source/PrismaEditor.cpp)

target_compile_definitions(DAW PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DAW,JUCE_VERSION>")

target_link_libraries(DAW
    PRIVATE
        daw_core
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
