cmake_minimum_required(VERSION 3.22)

# Must be set before project(): the plugin runs on macOS 11 and later (as Cymbalis).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Tympanis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TYMPANIS_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU)" ON)

# The shared job pool (a copy of Cymbalis's shared/dhpool, third_party/dhpool/SOURCE.md; its
# self-test dhpool_selftest comes with it), then the engine and console tools (no JUCE).
add_subdirectory(third_party/dhpool)
add_subdirectory(plugin/dsp)
add_subdirectory(tools/tympanis_render)

if(NOT TYMPANIS_BUILD_PLUGIN)
    return()
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(TYMPANIS_FORMATS VST3 AU)
else()
    set(TYMPANIS_FORMATS VST3)   # Linux is only used for local compile checks
endif()

juce_add_plugin(Tympanis
    COMPANY_NAME                "DHammers"
    BUNDLE_ID                   "com.dhammers.tympanis"
    PLUGIN_MANUFACTURER_CODE    Dham
    PLUGIN_CODE                 Tymp
    PRODUCT_NAME                "Tympanis"
    FORMATS                     ${TYMPANIS_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

# The baked kit (tools/bake_kit.py -> resources/bake/*.bake.json), gzipped into the binary.
set(TYMPANIS_BAKE_KEYS kit kick1 kick2 snare1 snare2 snare3 snare4 tom1 tom2 tom3 tom4 tom5 tom6 gong octo1 octo2 octo3 octo4)
set(TYMPANIS_RESOURCES)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bake")
foreach(key ${TYMPANIS_BAKE_KEYS})
    set(json "${CMAKE_CURRENT_SOURCE_DIR}/resources/bake/${key}.bake.json")
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/bake/${key}.bake.json.gz")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${json}")
    file(ARCHIVE_CREATE OUTPUT "${gz}" PATHS "${json}" FORMAT raw COMPRESSION GZip)
    list(APPEND TYMPANIS_RESOURCES "${gz}")
endforeach()

# The editor's artwork and fonts (docs/UI.md).
list(APPEND TYMPANIS_RESOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/TYMPANIS_Background.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/Carlito-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/PinyonScript-Regular.ttf"
)

juce_add_binary_data(TympanisBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES ${TYMPANIS_RESOURCES}
)
set_target_properties(TympanisBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Tympanis PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/ui/UiAssets.cpp
    plugin/ui/DrumModel.cpp
    plugin/ui/WireRaster.cpp
    plugin/ui/WireView.cpp
)
target_include_directories(Tympanis PRIVATE plugin)

target_compile_definitions(Tympanis PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # No "Made with JUCE" splash on load (Kris, 2026-11-25; as Cymbalis). The JUCE licence that
    # allows it is Kris's to settle (docs/BUILDS.md).
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Rosetta 2 cannot run AVX: keep the x86_64 slice at SSE4.1 (as Cymbalis).
if(APPLE)
    target_compile_options(Tympanis PRIVATE -Xarch_x86_64 -msse4.1)
endif()

target_link_libraries(Tympanis
    PRIVATE
        TympanisBinaryData
        tympanis_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Console host for the processor checks, the CPU figure and renders through processBlock.
juce_add_console_app(TympanisHostTest PRODUCT_NAME "tympanis_hosttest")
target_sources(TympanisHostTest PRIVATE tools/tympanis_hosttest/main.cpp)
target_include_directories(TympanisHostTest PRIVATE plugin)
target_compile_definitions(TympanisHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TympanisHostTest
    PRIVATE
        Tympanis
        tympanis_engine
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Renders the editor's pages to PNG files (docs/UI.md), for checking the UI.
juce_add_console_app(TympanisSnapshot PRODUCT_NAME "tympanis_snapshot")
target_sources(TympanisSnapshot PRIVATE tools/tympanis_snapshot/main.cpp)
target_include_directories(TympanisSnapshot PRIVATE plugin)
target_compile_definitions(TympanisSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TympanisSnapshot
    PRIVATE
        Tympanis
        tympanis_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
