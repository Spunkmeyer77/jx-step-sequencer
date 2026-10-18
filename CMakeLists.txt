cmake_minimum_required(VERSION 3.22)

project(JXStepSequencer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Universal binary (Apple Silicon + Intel)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(JX_COPY_PLUGIN "Copy the plugins into the system plug-in folders after building" ON)

# Shared settings for every plugin target
function(jx_setup target)
    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# VST3 (macOS, Windows, Linux): an instrument with MIDI out, so Ableton Live can load it
juce_add_plugin(JXStepSequencer
    COMPANY_NAME "Ticco Ross"
    BUNDLE_ID "com.ticcoross.jxstepsequencer"
    PLUGIN_MANUFACTURER_CODE Tcrs
    PLUGIN_CODE JxSq
    PRODUCT_NAME "JX Step Sequencer"
    DESCRIPTION "Polyphonic chord step sequencer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Generator
    COPY_PLUGIN_AFTER_BUILD ${JX_COPY_PLUGIN}
    FORMATS VST3)
jx_setup(JXStepSequencer)

# Audio Unit (macOS only): a MIDI FX, for the MIDI FX slot in Logic Pro / MainStage
if(APPLE)
    juce_add_plugin(JXStepSequencerAU
        COMPANY_NAME "Ticco Ross"
        BUNDLE_ID "com.ticcoross.jxstepsequencer.au"
        PLUGIN_MANUFACTURER_CODE Tcrs
        PLUGIN_CODE JxSq
        PRODUCT_NAME "JX Step Sequencer"
        DESCRIPTION "Polyphonic chord step sequencer"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD ${JX_COPY_PLUGIN}
        FORMATS AU)
    jx_setup(JXStepSequencerAU)
endif()
