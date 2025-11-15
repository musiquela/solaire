# Solaire - Spectral Harmonizer Plugin
# CMake configuration based on official JUCE examples
# Source: https://github.com/juce-framework/juce/blob/master/examples/CMake/AudioPlugin/CMakeLists.txt

cmake_minimum_required(VERSION 3.22)

project(SOLAIRE VERSION 1.0.0)

# Fix for macOS Sequoia / Xcode 16 compatibility with JUCE 7
# Source: https://forum.juce.com/t/fyi-juceaide-doesnt-compile-under-xcode-16-macos-sequoia/62836/17
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.13")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.13")

# Fetch JUCE from GitHub
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Define the plugin
juce_add_plugin(Solaire
    COMPANY_NAME "Solaire Audio"
    PLUGIN_MANUFACTURER_CODE Solo
    PLUGIN_CODE Pnhm
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Solaire"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Modulation)

# Add source files
target_sources(Solaire
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/SolaireEngine.cpp
        Source/SolaireEngine.h)

# Link JUCE modules
target_link_libraries(Solaire
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Set compile definitions
target_compile_definitions(Solaire
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)
