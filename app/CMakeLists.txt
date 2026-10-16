juce_add_gui_app(DAW
    PRODUCT_NAME "DAW"
    COMPANY_NAME "DAW Project"
    BUNDLE_ID "com.dawproject.daw"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DAW needs audio input access to record.")

target_sources(DAW PRIVATE
    Source/Main.cpp
    Source/DemoProject.cpp
    Source/MainComponent.cpp
    Source/ui/AppMenu.cpp
    Source/ui/BrowserPanel.cpp
    Source/ui/DetailPanel.cpp
    Source/ui/Dialogs.cpp
    Source/ui/SessionView.cpp
    Source/ui/ThemeLookAndFeel.cpp
    Source/ui/TopBar.cpp
    Source/ui/VersionWindow.cpp
    # Model, settings, engine and Prisma (compiled into the DAW as its built-in instrument), see the top-level CMakeLists.
    ${DAW_APP_SHARED_SOURCES})

target_compile_definitions(DAW PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    DAW_RT_CHECKS=$<BOOL:${DAW_RT_CHECKS}>
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DAW,JUCE_VERSION>")

target_link_libraries(DAW
    PRIVATE
        daw_core
        DawResources
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
