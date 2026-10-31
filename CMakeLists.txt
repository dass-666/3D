cmake_minimum_required(VERSION 3.22)

project(SpatialEchoStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SES_BUILD_TESTS "Build automated tests" ON)

include(FetchContent)

# Exact versions pinned: changing them is a deliberate, reviewed decision.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin (B0) -----------------------------------------------------------
juce_add_plugin(SpatialEchoStudio
    COMPANY_NAME "Spatial Echo Studio"
    BUNDLE_ID "com.spatialechostudio.plugin"
    PLUGIN_MANUFACTURER_CODE Sech   # PROVISIONAL: do not change after first public release
    PLUGIN_CODE Ses1                # PROVISIONAL: do not change after first public release
    FORMATS VST3
    PRODUCT_NAME "Spatial Echo Studio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# All project sources live in one INTERFACE target. The plugin and the tests
# each compile it (with the JUCE modules) independently, which avoids duplicate
# JUCE symbols when the tests link it. The model only uses juce_core.
add_library(ses_shared INTERFACE)
target_sources(ses_shared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/VoicePresets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/Serialization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/DemoScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Parameters.cpp)
target_include_directories(ses_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ses_shared INTERFACE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ses_shared INTERFACE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

target_link_libraries(SpatialEchoStudio PRIVATE
    ses_shared
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# --- Tests -----------------------------------------------------------------
if(SES_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
