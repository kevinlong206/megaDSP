cmake_minimum_required(VERSION 3.22)

project(megaDSP VERSION 0.23.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 16e9d4ca7b1e86c76e04584b2c08e85a764bcda8
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(clap_juce_extensions)

set(MEGADSP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MEGADSP_FORMATS AU)
endif()

juce_add_plugin(megaDSP
    COMPANY_NAME "megaDSP Audio"
    BUNDLE_ID "audio.megadsp.plugin"
    PLUGIN_MANUFACTURER_CODE KLng
    PLUGIN_CODE MgDp
    FORMATS ${MEGADSP_FORMATS}
    PRODUCT_NAME "megaDSP"
    DESCRIPTION "Modular effects processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

set(MEGADSP_MODULE_SOURCES
    src/modules/AlgorithmicReverb.cpp
    src/modules/AnalogTape.cpp
    src/modules/Compressor.cpp
    src/modules/ConvolutionReverb.cpp
    src/modules/Delay.cpp
    src/modules/DspHelpers.cpp
    src/modules/DynamicEqualizer.cpp
    src/modules/Equalizer.cpp
    src/modules/Limiter.cpp
    src/modules/MidSideDecoder.cpp
    src/modules/ModuleRegistry.cpp
    src/modules/RandomGranulizer.cpp
    src/modules/RotarySpeaker.cpp
    src/modules/Saturator.cpp
    src/modules/StereoWidth.cpp
    src/modules/Tremolo.cpp
    src/modules/VintageChorus.cpp)

set(MEGADSP_MODULE_HEADERS
    src/DspModules.h
    src/modules/AlgorithmicReverb.h
    src/modules/AnalogTape.h
    src/modules/Compressor.h
    src/modules/ConvolutionReverb.h
    src/modules/Delay.h
    src/modules/DspHelpers.h
    src/modules/DspModule.h
    src/modules/DynamicEqualizer.h
    src/modules/Equalizer.h
    src/modules/Limiter.h
    src/modules/MidSideDecoder.h
    src/modules/ModuleCapabilities.h
    src/modules/ModuleRegistry.h
    src/modules/RandomGranulizer.h
    src/modules/RotarySpeaker.h
    src/modules/Saturator.h
    src/modules/StereoWidth.h
    src/modules/Tremolo.h
    src/modules/VintageChorus.h)

set(MEGADSP_UI_SOURCES
    src/ui/EffectGraph.cpp
    src/ui/EffectGraph.h
    src/ui/GraphStyle.h
    src/ui/GuiLayout.cpp
    src/ui/GuiLayout.h
    src/ui/ModuleView.cpp
    src/ui/ModuleView.h
    src/ui/ModuleViewFactory.cpp
    src/ui/ModuleViewFactory.h
    src/ui/ModuleBrowser.cpp
    src/ui/ModuleBrowser.h
    src/ui/modules/AlgorithmicReverbModuleView.cpp
    src/ui/modules/AnalogTapeModuleView.cpp
    src/ui/modules/CompressorModuleView.cpp
    src/ui/modules/ConvolutionReverbModuleView.cpp
    src/ui/modules/DelayModuleView.cpp
    src/ui/modules/DynamicEqualizerModuleView.cpp
    src/ui/modules/EqualizerModuleView.cpp
    src/ui/modules/LimiterModuleView.cpp
    src/ui/modules/MidSideDecoderModuleView.cpp
    src/ui/modules/ModuleViewCreators.h
    src/ui/modules/RandomGranulizerModuleView.cpp
    src/ui/modules/RotarySpeakerModuleView.cpp
    src/ui/modules/SaturatorModuleView.cpp
    src/ui/modules/StereoWidthModuleView.cpp
    src/ui/modules/TremoloModuleView.cpp
    src/ui/modules/VintageChorusModuleView.cpp)

target_sources(megaDSP PRIVATE
    ${MEGADSP_MODULE_SOURCES}
    ${MEGADSP_MODULE_HEADERS}
    ${MEGADSP_UI_SOURCES}
    src/EffectRack.cpp
    src/EffectRack.h
    src/Parameters.cpp
    src/Parameters.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/VisualizationData.h)

target_compile_definitions(megaDSP
    PUBLIC
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

target_link_libraries(megaDSP
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(
    TARGET megaDSP
    CLAP_ID "audio.megadsp.plugin"
    CLAP_FEATURES audio-effect stereo mono)

if(APPLE)
    option(MEGADSP_ADHOC_SIGN "Ad-hoc sign local development plugin bundles" ON)
    if(MEGADSP_ADHOC_SIGN)
        foreach(bundle_target megaDSP_VST3 megaDSP_AU megaDSP_CLAP)
            add_custom_command(TARGET ${bundle_target} POST_BUILD
                COMMAND codesign --force --deep --sign -
                        "$<TARGET_BUNDLE_DIR:${bundle_target}>"
                COMMAND /usr/bin/touch "$<TARGET_BUNDLE_DIR:${bundle_target}>"
                COMMENT "Ad-hoc signing ${bundle_target}")
        endforeach()
    endif()
endif()

juce_add_console_app(megaDSPTests PRODUCT_NAME "megaDSP Tests")
target_sources(megaDSPTests PRIVATE
    tests/TestMain.cpp
    tests/DspModuleTests.cpp
    tests/GuiLayoutTests.cpp
    tests/ModuleBrowserTests.cpp
    tests/ProcessorTests.cpp
    ${MEGADSP_MODULE_SOURCES}
    ${MEGADSP_MODULE_HEADERS}
    ${MEGADSP_UI_SOURCES}
    src/EffectRack.cpp
    src/EffectRack.h
    src/Parameters.cpp
    src/Parameters.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/VisualizationData.h)
target_include_directories(megaDSPTests PRIVATE src)
target_compile_definitions(megaDSPTests PRIVATE
    MEGADSP_TESTS=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JucePlugin_Name="megaDSP")
target_link_libraries(megaDSPTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_link_options(megaDSPTests PRIVATE "/STACK:8388608")
endif()

enable_testing()
add_test(NAME megaDSPTests COMMAND megaDSPTests)
