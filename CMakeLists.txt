cmake_minimum_required(VERSION 3.22)

# --- Réglages à personnaliser -------------------------------------------------
set(LOOPROLL_PRODUCT_NAME "Fragment")         # nom du plugin dans le DAW, et nom du fichier .vst3
set(LOOPROLL_COMPANY_NAME "Phragma")          # nom affiché comme éditeur dans le DAW
set(LOOPROLL_BUNDLE_ID    "com.phragma.fragment")
# -----------------------------------------------------------------------------
# Le plugin s'appelait LoopRoll jusqu'à la v0.4 : ce nom reste celui du projet CMake,
# des cibles et des classes. Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE :
# ce sont eux (et pas le nom) qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(LoopRoll VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(LOOPROLL_COPY_AFTER_BUILD "Copier le plugin dans le dossier VST3 du système après la compilation" OFF)
option(LOOPROLL_BUILD_TESTS "Compiler les tests" OFF)

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin ------------------------------------------------------------------
juce_add_plugin(LoopRoll
    PRODUCT_NAME                "${LOOPROLL_PRODUCT_NAME}"
    COMPANY_NAME                "${LOOPROLL_COMPANY_NAME}"
    BUNDLE_ID                   "${LOOPROLL_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Lprl
    FORMATS                     VST3
    VST3_CATEGORIES             Fx Delay
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${LOOPROLL_COPY_AFTER_BUILD})

target_sources(LoopRoll PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Police de l'interface (Chakra Petch, licence OFL : voir Assets/ChakraPetch-OFL.txt), embarquée dans le plugin
juce_add_binary_data(LoopRollAssets SOURCES
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-Bold.ttf)
set_target_properties(LoopRollAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(LoopRoll PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    LOOPROLL_VERSION="${PROJECT_VERSION}")

target_link_libraries(LoopRoll
    PRIVATE
        juce::juce_audio_processors
        LoopRollAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(LOOPROLL_BUILD_TESTS)
    enable_testing()

    # Moteur seul, sans JUCE
    add_executable(LoopRollEngineTests Tests/EngineTests.cpp)
    target_include_directories(LoopRollEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND LoopRollEngineTests)

    # Plugin complet (paramètres, synchro tempo, état, interface)
    juce_add_console_app(LoopRollPluginTests PRODUCT_NAME "LoopRollPluginTests")
    target_sources(LoopRollPluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(LoopRollPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        LOOPROLL_VERSION="${PROJECT_VERSION}")
    target_link_libraries(LoopRollPluginTests PRIVATE
        juce::juce_audio_processors
        LoopRollAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND LoopRollPluginTests)
endif()
