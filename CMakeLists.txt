cmake_minimum_required(VERSION 3.22)

project(megaDSP VERSION 2026.8.1 LANGUAGES C CXX)

set(MEGADSP_VERSION "2026.08.01-beta")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
option(MEGADSP_CAPTURE_SCREENSHOTS
       "Capture documentation images from the Standalone editor" OFF)
if(MSVC)
    add_compile_definitions(NOMINMAX)
endif()

find_package(Git QUIET)
set(MEGADSP_COMMIT_HASH "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MEGADSP_GIT_RESULT
        OUTPUT_VARIABLE MEGADSP_GIT_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(LENGTH "${MEGADSP_GIT_OUTPUT}" MEGADSP_GIT_OUTPUT_LENGTH)
    if(MEGADSP_GIT_RESULT EQUAL 0
       AND MEGADSP_GIT_OUTPUT_LENGTH EQUAL 7
       AND MEGADSP_GIT_OUTPUT MATCHES "^[0-9a-fA-F]+$")
        string(TOLOWER "${MEGADSP_GIT_OUTPUT}" MEGADSP_COMMIT_HASH)
    endif()
endif()
message(STATUS
    "megaDSP build identity: ${MEGADSP_VERSION} (${MEGADSP_COMMIT_HASH})")

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

FetchContent_Declare(
    NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG 3cde95c354d5ba6da01316cad90b05cfc4855053
    GIT_SUBMODULES Dependencies/eigen Dependencies/AudioDSPTools
    SOURCE_SUBDIR _megadsp_populate_only
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DNAM_SOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/neuralampmodelercore-finalize-slim.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyNamPatch.cmake"
        COMMAND
        "${CMAKE_COMMAND}"
        "-DNAM_SOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/neuralampmodelercore-container-registration.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyNamPatch.cmake"
        COMMAND
        "${CMAKE_COMMAND}"
        "-DNAM_SOURCE_DIR=<SOURCE_DIR>/Dependencies/AudioDSPTools"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/neuralampmodelercore-resampler-portability.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyNamPatch.cmake")
FetchContent_MakeAvailable(NeuralAmpModelerCore)

# Explicit stable source list (no glob) for NAM core
set(NAM_CORE_SOURCES
    "${neuralampmodelercore_SOURCE_DIR}/NAM/activations.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/conv1d.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/container.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/convnet.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/dsp.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/get_dsp.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/linear.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/lstm.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/ring_buffer.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/util.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/wavenet/model.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/wavenet/a2_fast.cpp"
    "${neuralampmodelercore_SOURCE_DIR}/NAM/wavenet/slimmable.cpp")

juce_add_binary_data(megaDSPCabinetIRs
    NAMESPACE CabinetIRData
    SOURCES
        "assets/cabinet-irs/1960 GREEN 5757121.wav"
        "assets/cabinet-irs/1984B V30 GB.wav"
        "assets/cabinet-irs/6402 57 121.wav"
        "assets/cabinet-irs/6402 57.wav"
        "assets/cabinet-irs/65W 57121.wav"
        "assets/cabinet-irs/BLACK 57 160.wav"
        "assets/cabinet-irs/CREAM65 57121.wav"
        "assets/cabinet-irs/EV12L 57121.wav"
        "assets/cabinet-irs/GREEN.wav"
        "assets/cabinet-irs/GREEN+V30 MIX 5757121.wav"
        "assets/cabinet-irs/MESRECT V30 57421.wav"
        "assets/cabinet-irs/Mikko1.wav"
        "assets/cabinet-irs/TJ 66 Alnico Silver e906 2.wav"
        "assets/cabinet-irs/TJ 66 Alnico Silver FAT-906 1.wav"
        "assets/cabinet-irs/TJ 66 Alnico Silver R121-SM545 1.wav"
        "assets/cabinet-irs/TJ 69 G12M Green R121-545 3.wav"
        "assets/cabinet-irs/TJ 70 G12H Green Fat-57 2.wav"
        "assets/cabinet-irs/TJ C12N e906 2.wav"
        "assets/cabinet-irs/TJ C12N R121-SM545 1.wav"
        "assets/cabinet-irs/TJ M75 e906 3.wav"
        "assets/cabinet-irs/TJ M75 FAT-SM57 3.wav"
        "assets/cabinet-irs/TJ M75 R121-SM545 2.wav"
        "assets/cabinet-irs/V30.wav")

# OBJECT library ensures static-init registration objects survive linking
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core PUBLIC
    "${neuralampmodelercore_SOURCE_DIR}"
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann")
target_include_directories(nam_core SYSTEM PUBLIC
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen"
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies/AudioDSPTools/dsp")
target_compile_definitions(nam_core PUBLIC
    NAM_SAMPLE_FLOAT
    DSP_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST
    DEFAULT_BLOCK_SIZE=512)
set_target_properties(nam_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

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
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

set(MEGADSP_MODULE_SOURCES
    src/modules/AlgorithmicReverb.cpp
    src/modules/AdaptiveClipper.cpp
    src/modules/AnalogTape.cpp
    src/modules/BeatPermuter.cpp
    src/modules/Compressor.cpp
    src/modules/ConvolutionReverb.cpp
    src/modules/Delay.cpp
    src/modules/DiffusionDelay.cpp
    src/modules/DspHelpers.cpp
    src/modules/DynamicEqualizer.cpp
    src/modules/Equalizer.cpp
    src/modules/FrequencyLab.cpp
    src/modules/FormantForge.cpp
    src/modules/GateExpander.cpp
    src/modules/GuitarCabinet.cpp
    src/modules/ChordLoom.cpp
    src/modules/VolumeSwell.cpp
    src/modules/SpectralNebula.cpp
    src/modules/MidnightCurrent.cpp
    src/modules/AutoWah.cpp
    src/modules/ResonantSculpture.cpp
    src/modules/Limiter.cpp
    src/modules/LoudnessRider.cpp
    src/modules/MidSideDecoder.cpp
    src/modules/ModuleRegistry.cpp
    src/modules/MultibandCompressor.cpp
    src/modules/PitchBloom.cpp
    src/modules/PhaseCoherence.cpp
    src/modules/RandomGranulizer.cpp
    src/modules/ResonantMatrix.cpp
    src/modules/ResonanceTamer.cpp
    src/modules/RotarySpeaker.cpp
    src/modules/Saturator.cpp
    src/modules/StereoWidth.cpp
    src/modules/SpectralPrism.cpp
    src/modules/SpectralBalance.cpp
    src/modules/SpectralDelayCanvas.cpp
    src/modules/SpectralFoundation.cpp
    src/modules/SignalDecay.cpp
    src/modules/SpatialOrbit.cpp
    src/modules/StudioFlanger.cpp
    src/modules/StudioPhaser.cpp
    src/modules/Tremolo.cpp
    src/modules/TransientDesigner.cpp
    src/modules/TimeMosaic.cpp
    src/modules/VintageChorus.cpp
    src/modules/WavefoldGarden.cpp
    src/modules/HarmonicMirage.cpp
    src/modules/ChaosField.cpp
    src/modules/NeuralAmpModeler.cpp
    src/modules/StageVocal.cpp
    src/modules/VinylSimulator.cpp
    src/modules/OptoLeveler.cpp
    src/modules/FetLimiter.cpp
    src/modules/ProgramEqualizer.cpp
    src/modules/StudioPlate.cpp
    src/modules/VelvetDrive.cpp
    src/modules/WormholeConvolution.cpp
    src/modules/Bitcrusher.cpp
    src/modules/PortalPermuter.cpp
    src/modules/IRMosaic.cpp)

set(MEGADSP_MODULE_HEADERS
    src/DspModules.h
    src/modules/AlgorithmicReverb.h
    src/modules/AdaptiveClipper.h
    src/modules/AnalogTape.h
    src/modules/BeatPermuter.h
    src/modules/Compressor.h
    src/modules/ConvolutionReverb.h
    src/modules/Delay.h
    src/modules/DiffusionDelay.h
    src/modules/DspHelpers.h
    src/modules/DspModule.h
    src/modules/DspSafety.h
    src/modules/DynamicEqualizer.h
    src/modules/Equalizer.h
    src/modules/FrequencyLab.h
    src/modules/FormantForge.h
    src/modules/GateExpander.h
    src/modules/GuitarCabinet.h
    src/modules/ChordLoom.h
    src/modules/VolumeSwell.h
    src/modules/SpectralNebula.h
    src/modules/MidnightCurrent.h
    src/modules/AutoWah.h
    src/modules/ResonantSculpture.h
    src/modules/Limiter.h
    src/modules/LoudnessRider.h
    src/modules/MidSideDecoder.h
    src/modules/ModuleCapabilities.h
    src/modules/ModuleRegistry.h
    src/modules/MultibandCompressor.h
    src/modules/PitchBloom.h
    src/modules/PhaseCoherence.h
    src/modules/RandomGranulizer.h
    src/modules/ResonantMatrix.h
    src/modules/ResonanceTamer.h
    src/modules/RotarySpeaker.h
    src/modules/Saturator.h
    src/modules/StereoWidth.h
    src/modules/SpectralPrism.h
    src/modules/SpectralBalance.h
    src/modules/SpectralDelayCanvas.h
    src/modules/SpectralFoundation.h
    src/modules/SignalDecay.h
    src/modules/SpatialOrbit.h
    src/modules/StudioFlanger.h
    src/modules/StudioPhaser.h
    src/modules/Tremolo.h
    src/modules/TransientDesigner.h
    src/modules/TimeMosaic.h
    src/modules/VintageChorus.h
    src/modules/WavefoldGarden.h
    src/modules/HarmonicMirage.h
    src/modules/ChaosField.h
    src/modules/NeuralAmpModeler.h
    src/modules/StageVocal.h
    src/modules/VinylSimulator.h
    src/modules/OptoLeveler.h
    src/modules/FetLimiter.h
    src/modules/ProgramEqualizer.h
    src/modules/StudioPlate.h
    src/modules/VelvetDrive.h
    src/modules/WormholeConvolution.h
    src/modules/Bitcrusher.h
    src/modules/PortalPermuter.h
    src/modules/IRMosaic.h)

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
    src/ui/modules/AdaptiveClipperModuleView.cpp
    src/ui/modules/AnalogTapeModuleView.cpp
    src/ui/modules/BeatPermuterModuleView.cpp
    src/ui/modules/CompressorModuleView.cpp
    src/ui/modules/ConvolutionReverbModuleView.cpp
    src/ui/modules/ChaosFieldModuleView.cpp
    src/ui/modules/DelayModuleView.cpp
    src/ui/modules/DiffusionDelayModuleView.cpp
    src/ui/modules/DynamicEqualizerModuleView.cpp
    src/ui/modules/EqualizerModuleView.cpp
    src/ui/modules/FrequencyLabModuleView.cpp
    src/ui/modules/FormantForgeModuleView.cpp
    src/ui/modules/GateExpanderModuleView.cpp
    src/ui/modules/GuitarCabinetModuleView.cpp
    src/ui/modules/ChordLoomModuleView.cpp
    src/ui/modules/VolumeSwellModuleView.cpp
    src/ui/modules/SpectralNebulaModuleView.cpp
    src/ui/modules/MidnightCurrentModuleView.cpp
    src/ui/modules/AutoWahModuleView.cpp
    src/ui/modules/ResonantSculptureModuleView.cpp
    src/ui/modules/LimiterModuleView.cpp
    src/ui/modules/LoudnessRiderModuleView.cpp
    src/ui/modules/MidSideDecoderModuleView.cpp
    src/ui/modules/ModuleViewCreators.h
    src/ui/modules/MultibandCompressorModuleView.cpp
    src/ui/modules/PitchBloomModuleView.cpp
    src/ui/modules/PhaseCoherenceModuleView.cpp
    src/ui/modules/RandomGranulizerModuleView.cpp
    src/ui/modules/ResonanceTamerModuleView.cpp
    src/ui/modules/ResonantMatrixModuleView.cpp
    src/ui/modules/RotarySpeakerModuleView.cpp
    src/ui/modules/SaturatorModuleView.cpp
    src/ui/modules/StereoWidthModuleView.cpp
    src/ui/modules/SpectralPrismModuleView.cpp
    src/ui/modules/SpectralBalanceModuleView.cpp
    src/ui/modules/SpectralDelayCanvasModuleView.cpp
    src/ui/modules/SignalDecayModuleView.cpp
    src/ui/modules/SpatialOrbitModuleView.cpp
    src/ui/modules/StudioFlangerModuleView.cpp
    src/ui/modules/StudioPhaserModuleView.cpp
    src/ui/modules/TremoloModuleView.cpp
    src/ui/modules/TransientDesignerModuleView.cpp
    src/ui/modules/TimeMosaicModuleView.cpp
    src/ui/modules/VintageChorusModuleView.cpp
    src/ui/modules/WavefoldGardenModuleView.cpp
    src/ui/modules/HarmonicMirageModuleView.cpp
    src/ui/modules/NeuralAmpModelerModuleView.cpp
    src/ui/modules/StageVocalModuleView.cpp
    src/ui/modules/VinylSimulatorModuleView.cpp
    src/ui/modules/OptoLevelerModuleView.cpp
    src/ui/modules/FetLimiterModuleView.cpp
    src/ui/modules/ProgramEqualizerModuleView.cpp
    src/ui/modules/StudioPlateModuleView.cpp
    src/ui/modules/VelvetDriveModuleView.cpp
    src/ui/modules/WormholeConvolutionModuleView.cpp
    src/ui/modules/BitcrusherModuleView.cpp
    src/ui/modules/PortalPermuterModuleView.cpp
    src/ui/modules/IRMosaicModuleView.cpp)

target_sources(megaDSP PRIVATE
    ${MEGADSP_MODULE_SOURCES}
    ${MEGADSP_MODULE_HEADERS}
    ${MEGADSP_UI_SOURCES}
    src/Diagnostics.cpp
    src/Diagnostics.h
    src/BuildInfo.h
    src/EffectRack.cpp
    src/EffectRack.h
    src/Parameters.cpp
    src/Parameters.h
    src/SnapshotState.cpp
    src/SnapshotState.h
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
        JUCE_WEB_BROWSER=0
        MEGADSP_VERSION="${MEGADSP_VERSION}"
        MEGADSP_COMMIT_HASH="${MEGADSP_COMMIT_HASH}")
if(MEGADSP_CAPTURE_SCREENSHOTS)
    target_compile_definitions(megaDSP PRIVATE
        MEGADSP_CAPTURE_SCREENSHOTS=1)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    target_compile_definitions(megaDSP PRIVATE
        WDL_ALLOW_UNSIGNED_DEFAULT_CHAR=1)
endif()

target_link_libraries(megaDSP
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        megaDSPCabinetIRs
        nam_core
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
        foreach(bundle_target megaDSP_VST3 megaDSP_AU megaDSP_CLAP megaDSP_Standalone)
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
    tests/NeuralAmpModelerTests.cpp
    tests/SpectralAdaptiveModulesTests.cpp
    tests/SpectralHistoryPairTests.cpp
    tests/DynamicsTelemetryTests.cpp
    tests/DiagnosticsTests.cpp
    tests/TelemetryFoundationTests.cpp
    tests/TimeTelemetryTests.cpp
    tests/GuiLayoutTests.cpp
    tests/GuitarCabinetTests.cpp
    tests/ChordLoomTests.cpp
    tests/VolumeSwellTests.cpp
    tests/SpectralNebulaTests.cpp
    tests/MidnightCurrentTests.cpp
    tests/AutoWahTests.cpp
    tests/ResonantSculptureTests.cpp
    tests/StageVocalTests.cpp
    tests/VinylSimulatorTests.cpp
    tests/OptoLevelerTests.cpp
    tests/FetLimiterTests.cpp
    tests/ProgramEqualizerTests.cpp
    tests/StudioPlateTests.cpp
    tests/VelvetDriveTests.cpp
    tests/WormholeConvolutionTests.cpp
    tests/BitcrusherTests.cpp
    tests/PortalPermuterTests.cpp
    tests/IRMosaicTests.cpp
    tests/ModuleBrowserTests.cpp
    tests/NextTenUtilityTests.cpp
    tests/ProcessorTests.cpp
    ${MEGADSP_MODULE_SOURCES}
    ${MEGADSP_MODULE_HEADERS}
    ${MEGADSP_UI_SOURCES}
    src/Diagnostics.cpp
    src/Diagnostics.h
    src/EffectRack.cpp
    src/EffectRack.h
    src/Parameters.cpp
    src/Parameters.h
    src/SnapshotState.cpp
    src/SnapshotState.h
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
    MEGADSP_VERSION="${MEGADSP_VERSION}"
    MEGADSP_COMMIT_HASH="${MEGADSP_COMMIT_HASH}"
    JucePlugin_Name="megaDSP")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    target_compile_definitions(megaDSPTests PRIVATE
        WDL_ALLOW_UNSIGNED_DEFAULT_CHAR=1)
endif()
target_link_libraries(megaDSPTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    megaDSPCabinetIRs
    nam_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_link_options(megaDSPTests PRIVATE "/STACK:8388608")
endif()

enable_testing()
add_test(NAME megaDSPTests COMMAND megaDSPTests)
