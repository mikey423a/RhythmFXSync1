cmake_minimum_required(VERSION 3.22)
project(RhythmFXSync VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Default: audio EFFECT (insert on a guitar track). Configure with -DRFX_AS_EFFECT=OFF
# to build the instrument version (MIDI synth + drums, no audio input) instead.
option(RFX_COPY_AFTER_BUILD "Install the VST3 into the system folder after building" ON)
option(RFX_NEW_GUI "Enable the new editor widgets (pattern tools, orbit view, morph pad, perform page, spectrum). Not yet compiled - see HANDOFF.md section 12" OFF)
option(RFX_AS_EFFECT "Build as an audio effect instead of an instrument" ON)
if(RFX_AS_EFFECT)
    set(RFX_IS_SYNTH FALSE)
else()
    set(RFX_IS_SYNTH TRUE)
endif()

juce_add_plugin(RhythmFXSync
    COMPANY_NAME            "YourName"
    PLUGIN_MANUFACTURER_CODE Ynam
    PLUGIN_CODE             Rfxs
    IS_SYNTH                ${RFX_IS_SYNTH}
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "RhythmFX Sync"
    COPY_PLUGIN_AFTER_BUILD ${RFX_COPY_AFTER_BUILD})

target_sources(RhythmFXSync PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(RhythmFXSync PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(RFX_NEW_GUI)
    target_compile_definitions(RhythmFXSync PUBLIC RFX_NEW_GUI=1)
    target_link_libraries(RhythmFXSync PRIVATE juce::juce_dsp)
endif()

target_link_libraries(RhythmFXSync
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
