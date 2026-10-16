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
# AAX (Pro Tools) mis de côté pour l'instant : seulement si on le demande (cmake -DONIRIQUE_AAX=ON)
option(ONIRIQUE_AAX "Compiler aussi le format AAX" OFF)
set(ONIRIQUE_FORMATS VST3 Standalone)
if(ONIRIQUE_AAX)
    list(APPEND ONIRIQUE_FORMATS AAX)
endif()
if(APPLE)
    list(APPEND ONIRIQUE_FORMATS AU)
endif()

# AAX (Pro Tools) : SDK Avid téléchargé sur developer.avid.com. Sans lui, JUCE prend le SDK AAX qu'il embarque.
set(ONIRIQUE_AAX_SDK "E:/DEV/aax-sdk-2-8-0" CACHE PATH "Dossier du SDK AAX d'Avid")
if(EXISTS "${ONIRIQUE_AAX_SDK}/Interfaces")
    juce_set_aax_sdk_path("${ONIRIQUE_AAX_SDK}")
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
    AAX_IDENTIFIER              store.onirique.vocalbox
    AAX_CATEGORY                AAX_ePlugInCategory_Dynamics
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueVocalBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FxView.cpp)

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

# Mac : sans cette option, Core Graphics fusionne toutes les petites zones animées (étoiles, logo, mètres,
# analyseur) en une seule grande zone et redessine presque toute la fenêtre à chaque image. Avec elle, JUCE
# passe par un rendu Metal qui ne redessine que les zones demandées.
if(APPLE)
    target_compile_definitions(OniriqueVocalBox PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

target_link_libraries(OniriqueVocalBox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        OniriqueAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
