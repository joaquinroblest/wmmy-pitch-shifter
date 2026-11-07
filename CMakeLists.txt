cmake_minimum_required(VERSION 3.24)

project(WMMYPitchShifter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(juce)

# Rubber Band (GPL v2 o posterior). Se compila como un solo archivo fuente.
FetchContent_Declare(
    rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(rubberband)

# stftPitchShift (MIT), motor del modo "En vivo". Solo se usan sus cabeceras,
# por eso SOURCE_SUBDIR apunta a una carpeta sin CMakeLists (no se compila su CLI).
FetchContent_Declare(
    stftpitchshift
    GIT_REPOSITORY https://github.com/jurihock/stftPitchShift.git
    GIT_TAG 70c96db087458f8631edbc1bb4068d42ea5b52cc
    SOURCE_SUBDIR solo-cabeceras)
FetchContent_MakeAvailable(stftpitchshift)

juce_add_plugin(WMMYPitchShifter
    COMPANY_NAME "WMMY"
    PRODUCT_NAME "WMMY Pitch Shifter"
    BUNDLE_ID "com.wmmy.pitchshifter"
    PLUGIN_MANUFACTURER_CODE Wmmy
    PLUGIN_CODE Wmps
    FORMATS AU VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Pitch
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(WMMYPitchShifter PRIVATE
    Source/PluginProcessor.cpp
    ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)

target_include_directories(WMMYPitchShifter PRIVATE
    ${rubberband_SOURCE_DIR}
    ${stftpitchshift_SOURCE_DIR}/cpp)

target_compile_definitions(WMMYPitchShifter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(WMMYPitchShifter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
