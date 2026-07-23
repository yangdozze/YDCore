# ==============================================================================
#  YD Core — polyphonic workstation synthesizer
#  (c) 2026 Yangdozze. Licensed under AGPL-3.0 (see LICENSE).
# ==============================================================================
cmake_minimum_required(VERSION 3.22)
project(YDCore VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ------------------------------------------------------------------------------
# JUCE: use a local checkout in libs/JUCE when present (dev sandbox),
# otherwise fetch the pinned release (CI).
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(STATUS "YD Core: using local JUCE at libs/JUCE")
    add_subdirectory(libs/JUCE)
else()
    message(STATUS "YD Core: fetching JUCE 8.0.15")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Factory presets embedded into the binary (also shipped as portable JSON).
# If the Presets folder is empty (fresh checkout), generate them from the
# canonical generator script.
# ------------------------------------------------------------------------------
file(GLOB YDCORE_PRESET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Presets/*.json")
if(NOT YDCORE_PRESET_FILES)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    message(STATUS "YD Core: generating factory presets via tools/gen_presets.py")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_presets.py"
        RESULT_VARIABLE YDCORE_GEN_RESULT)
    if(NOT YDCORE_GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "YD Core: preset generation failed (${YDCORE_GEN_RESULT})")
    endif()
endif()
file(GLOB YDCORE_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/*.json")
list(LENGTH YDCORE_PRESET_FILES YDCORE_PRESET_COUNT)
message(STATUS "YD Core: embedding ${YDCORE_PRESET_COUNT} factory presets")
juce_add_binary_data(YDCorePresetData SOURCES ${YDCORE_PRESET_FILES})
set_target_properties(YDCorePresetData PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# Shared source list (compiled into the plugin and into the headless test app)
# ------------------------------------------------------------------------------
set(YDCORE_SOURCES
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine/Voice.cpp
    Source/Engine/SynthEngine.cpp
    Source/Engine/Arpeggiator.cpp
    Source/Effects/EffectsChain.cpp
    Source/Presets/PresetManager.cpp
    Source/UI/LookAndFeelYD.cpp)

# ------------------------------------------------------------------------------
# Plugin targets: VST3 + Standalone
# ------------------------------------------------------------------------------
juce_add_plugin(YDCore
    PRODUCT_NAME                "YD Core"
    COMPANY_NAME                "Yangdozze"
    BUNDLE_ID                   com.yangdozze.ydcore
    PLUGIN_MANUFACTURER_CODE    Ydzz
    PLUGIN_CODE                 YdC1
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Synth
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(YDCore PRIVATE ${YDCORE_SOURCES})
target_include_directories(YDCore PRIVATE Source)

target_compile_definitions(YDCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_JACK=0)

target_link_libraries(YDCore
    PRIVATE
        YDCorePresetData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Headless smoke-test app (no audio device needed; renders offline)
# ------------------------------------------------------------------------------
option(YDCORE_BUILD_TESTS "Build the YD Core headless verification app" ON)
if(YDCORE_BUILD_TESTS)
    juce_add_console_app(YDCoreTests PRODUCT_NAME "YDCoreTests")
    target_sources(YDCoreTests PRIVATE Tests/HeadlessTests.cpp ${YDCORE_SOURCES})
    target_include_directories(YDCoreTests PRIVATE Source)
    target_compile_definitions(YDCoreTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_JACK=0
        YDCORE_HEADLESS_TESTS=1)
    target_link_libraries(YDCoreTests PRIVATE
        YDCorePresetData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
