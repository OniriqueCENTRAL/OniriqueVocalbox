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
    Source/ModulePanels.cpp
    Source/ModuleScreens.cpp
    Source/MixAnalysis.cpp
    Source/StartScreen.cpp
    Source/Widgets.cpp)

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
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1)

# Mac : sans cette option, Core Graphics fusionne toutes les petites zones animées (étoiles, logo, mètres,
# analyseur) en une seule grande zone et redessine presque toute la fenêtre à chaque image. Avec elle, JUCE
# passe par un rendu Metal qui ne redessine que les zones demandées.
if(APPLE)
    target_compile_definitions(OniriqueVocalBox PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

# CRÉER LE MIX : séparation de la voix d'un morceau de référence (demucs.cpp + Eigen, MIT / MPL2), compilée à part
file(GLOB ONIRIQUE_DEMUCS_SOURCES ThirdParty/demucs/*.cpp)
add_library(onirique_demucs STATIC ${ONIRIQUE_DEMUCS_SOURCES} ThirdParty/separation/onirique_separate.cpp ThirdParty/separation/onirique_model_res.cpp)
target_include_directories(onirique_demucs PUBLIC ThirdParty/separation PRIVATE ThirdParty/demucs ThirdParty/eigen)
set_target_properties(onirique_demucs PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
if(MSVC)
    target_compile_options(onirique_demucs PRIVATE /O2 /Oi /fp:fast /bigobj /EHsc /W0)
else()
    target_compile_options(onirique_demucs PRIVATE -O3 -ffast-math -w)
endif()

# le modèle (84 Mo, non versionné) : Resources/models/, copié dans le plugin (Contents/Resources)
set(ONIRIQUE_DEMUCS_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/models/ggml-model-htdemucs_ft_vocals-4s-f16.bin")
if(EXISTS "${ONIRIQUE_DEMUCS_MODEL}")
    if(WIN32)
        # Windows : le modèle dans le plugin lui-même (le VST3 se livre en un seul fichier), écrit sur le disque au 1er usage
        file(TO_NATIVE_PATH "${ONIRIQUE_DEMUCS_MODEL}" ONIRIQUE_DEMUCS_MODEL_NATIVE)
        string(REPLACE "\\" "\\\\" ONIRIQUE_DEMUCS_MODEL_RC "${ONIRIQUE_DEMUCS_MODEL_NATIVE}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/demucs_model.rc" "DEMUCS_MODEL RCDATA \"${ONIRIQUE_DEMUCS_MODEL_RC}\"
")
        foreach(fmt VST3 Standalone)
            if(TARGET OniriqueVocalBox_${fmt})
                target_sources(OniriqueVocalBox_${fmt} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/demucs_model.rc")
            endif()
        endforeach()
    endif()
    foreach(fmt VST3 AU)
        if(TARGET OniriqueVocalBox_${fmt})
            add_custom_command(TARGET OniriqueVocalBox_${fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:OniriqueVocalBox_${fmt}>/../Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONIRIQUE_DEMUCS_MODEL}" "$<TARGET_FILE_DIR:OniriqueVocalBox_${fmt}>/../Resources/")
        endif()
    endforeach()
    if(TARGET OniriqueVocalBox_Standalone)
        add_custom_command(TARGET OniriqueVocalBox_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONIRIQUE_DEMUCS_MODEL}" "$<TARGET_FILE_DIR:OniriqueVocalBox_Standalone>/")
    endif()
else()
    message(WARNING "Modèle de séparation absent (${ONIRIQUE_DEMUCS_MODEL}) : CRÉER LE MIX prendra la voix au centre du mix")
endif()

target_link_libraries(OniriqueVocalBox
    PRIVATE
        onirique_demucs
        juce::juce_audio_utils
        juce::juce_dsp
        OniriqueAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
