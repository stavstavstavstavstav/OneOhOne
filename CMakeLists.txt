cmake_minimum_required(VERSION 3.22)
project(OneOhOne VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE est telecharge automatiquement a la configuration
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

juce_add_plugin(OneOhOne
    COMPANY_NAME "Gustave"
    PRODUCT_NAME "OneOhOne"
    BUNDLE_ID "com.gustave.oneohone"
    PLUGIN_MANUFACTURER_CODE Gust
    PLUGIN_CODE Ooo1
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS ${PLUGIN_FORMATS})

target_sources(OneOhOne PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_include_directories(OneOhOne PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(OneOhOne PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OneOhOne
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
