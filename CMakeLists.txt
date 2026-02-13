cmake_minimum_required(VERSION 3.24)

project(MultiChainer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(JUCE)

option(MULTICHAINER_ENABLE_AAX "Build AAX target if JUCE has AAX support configured" OFF)

set(MULTICHAINER_PLUGIN_FORMATS AU VST3)
if(MULTICHAINER_ENABLE_AAX)
    list(APPEND MULTICHAINER_PLUGIN_FORMATS AAX)
endif()

juce_add_plugin(MultiChainer
    COMPANY_NAME "MultiChainer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE McHN
    PLUGIN_CODE MtCh
    FORMATS ${MULTICHAINER_PLUGIN_FORMATS}
    PRODUCT_NAME "MultiChainer"
    PLUGIN_CHANNEL_CONFIGURATIONS "{2,2}"
)

juce_generate_juce_header(MultiChainer)

juce_add_binary_data(MultiChainerAssets
    SOURCES
        Assets/index.html
        Assets/styles.css
        Assets/app.js
)

target_sources(MultiChainer
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/dsp/LinearPhaseCrossover.h
        Source/dsp/LinearPhaseCrossover.cpp
        Source/dsp/MultibandDucker.h
        Source/dsp/MultibandDucker.cpp
        Source/dsp/EnvelopeFollower.h
        Source/dsp/EnvelopeFollower.cpp
        Source/dsp/MidiTrigger.h
        Source/dsp/MidiTrigger.cpp
        Source/dsp/FFTAnalyzer.h
        Source/dsp/FFTAnalyzer.cpp
        Source/ui/WebUIBridge.h
        Source/ui/WebUIBridge.cpp
)

target_include_directories(MultiChainer PRIVATE Source)

target_compile_definitions(MultiChainer
    PRIVATE
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_USE_VDSP_FRAMEWORK=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        MULTICHAINER_ENABLE_DEBUG_LOG=0
)

target_link_libraries(MultiChainer
    PRIVATE
        MultiChainerAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)
