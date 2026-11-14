cmake_minimum_required(VERSION 3.22)

project(CoopianoPlugin VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
    set(COOP_PLUGIN_FORMATS VST3 AU Standalone)
else()
    set(COOP_PLUGIN_FORMATS VST3 Standalone)
endif()

# Allow pointing to an existing local JUCE checkout via -DCOOP_JUCE_DIR=/path/to/JUCE
set(COOP_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")

if(COOP_JUCE_DIR AND EXISTS "${COOP_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local JUCE at: ${COOP_JUCE_DIR}")
    add_subdirectory("${COOP_JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    message(STATUS "Fetching JUCE 8.0.4 via FetchContent...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(CoopianoPlugin
    COMPANY_NAME                "0eq00"
    COMPANY_WEBSITE             "https://github.com/0eq00"
    BUNDLE_ID                   "0eq00.coopiano.plugin"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    Coop
    PLUGIN_CODE                 Cpno
    FORMATS                     ${COOP_PLUGIN_FORMATS}
    PRODUCT_NAME                "Coopiano"
)

juce_generate_juce_header(CoopianoPlugin)

target_sources(CoopianoPlugin
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(CoopianoPlugin
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/coopiano"
)

target_compile_definitions(CoopianoPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_MIDI_EFFECT=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(CoopianoPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
