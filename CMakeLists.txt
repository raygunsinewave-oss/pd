cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel). Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(PureDevotion VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(PD_BUILD_TOOLS "Build the PresetForge content tool" ON)
set(PD_AAX_SDK_PATH "" CACHE PATH "Optional path to the AAX SDK (enables the AAX format)")

# ---------------------------------------------------------------- JUCE 8
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PD_FORMATS AU)
endif()
if(PD_AAX_SDK_PATH)
    juce_set_aax_sdk_path("${PD_AAX_SDK_PATH}")
    list(APPEND PD_FORMATS AAX)
endif()

juce_add_binary_data(PDBinaryData
    HEADER_NAME PDBinaryData.h
    NAMESPACE PDBinaryData
    SOURCES
        assets/fonts/RobotoCondensed-Bold.ttf
        assets/fonts/JetBrainsMono-Regular.ttf
        assets/fonts/JetBrainsMono-Bold.ttf
        assets/logo/raygun_logo.svg)

juce_add_plugin(PureDevotion
    COMPANY_NAME "Raygun"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.raygun.puredevotion
    PLUGIN_MANUFACTURER_CODE Rgun
    PLUGIN_CODE PDvo
    PRODUCT_NAME "Pure Devotion"
    DESCRIPTION "Hybrid wavetable + semi-modular performance synthesizer"
    FORMATS ${PD_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(PD_SOURCES
    src/core/Params.cpp
    src/core/ModMatrix.cpp
    src/dsp/Wavetable.cpp
    src/dsp/SampleBank.cpp
    src/dsp/Voice.cpp
    src/dsp/SynthEngine.cpp
    src/fx/FxRack.cpp
    src/patch/PatchGraph.cpp
    src/perform/Perform.cpp
    src/presets/PresetFormat.cpp
    src/presets/PresetLibrary.cpp
    src/presets/PresetGenerator.cpp
    src/PluginProcessor.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/DogVisualizer.cpp
    src/ui/OscPage.cpp
    src/ui/PatchPage.cpp
    src/ui/FxPage.cpp
    src/ui/ModPage.cpp
    src/ui/PerformPage.cpp
    src/ui/BrowserPage.cpp
    src/PluginEditor.cpp)

set(PD_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    PD_VERSION_STRING="${PROJECT_VERSION}")

set(PD_LIBS
    PDBinaryData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_opengl
    juce::juce_gui_extra)

target_sources(PureDevotion PRIVATE ${PD_SOURCES})
target_include_directories(PureDevotion PRIVATE src)
target_compile_definitions(PureDevotion PUBLIC ${PD_DEFINES})
target_link_libraries(PureDevotion PRIVATE ${PD_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- PresetForge
# Console tool: generates + QA-renders the factory presets, exports wavetables,
# samples, theme PNGs and dog sprite sheets. It compiles the same sources.
if(PD_BUILD_TOOLS)
    juce_add_console_app(PresetForge PRODUCT_NAME "PresetForge")
    target_sources(PresetForge PRIVATE ${PD_SOURCES} tools/PresetForge.cpp)
    target_include_directories(PresetForge PRIVATE src)
    target_compile_definitions(PresetForge PRIVATE ${PD_DEFINES} PD_TOOL_BUILD=1 JUCE_PLUGINHOST_VST3=1
        JucePlugin_Name="Pure Devotion" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0)
    target_link_libraries(PresetForge PRIVATE ${PD_LIBS}
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
