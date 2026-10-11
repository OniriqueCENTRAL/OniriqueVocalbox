cmake_minimum_required(VERSION 3.22)

# Mac : binaire universel (Intel + Apple Silicon), à définir avant project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(OniriqueVocalBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# AU (Logic, GarageBand) uniquement sur Mac
set(ONIRIQUE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ONIRIQUE_FORMATS AU)
endif()

juce_add_plugin(OniriqueVocalBox
    COMPANY_NAME                Onirique
    PLUGIN_MANUFACTURER_CODE    Onir
    PLUGIN_CODE                 Ovbx
    FORMATS                     ${ONIRIQUE_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    PRODUCT_NAME                "ONIRIQUE VOCALBOX"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Fx
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueVocalBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# logo du site (sigil blanc), intégré au binaire
juce_add_binary_data(OniriqueAssets SOURCES Resources/logo.png)

target_include_directories(OniriqueVocalBox PRIVATE Source)

# Licence ONIRIQUE : vérification du fichier écrit par ONIRIQUE CENTRAL, pastille et page d'activation.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueVocalBox)

target_compile_definitions(OniriqueVocalBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OniriqueVocalBox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        OniriqueAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
