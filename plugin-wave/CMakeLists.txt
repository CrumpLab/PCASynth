# PCAWave: the waveform plugin (PCA on the waveforms themselves). It shares
# plugin-common with PCASynth and is a separate product.
juce_add_plugin(PCAWave
    PRODUCT_NAME "PCAWave"
    COMPANY_NAME "CrumpLab"
    BUNDLE_ID "com.crumplab.pcawave"
    PLUGIN_MANUFACTURER_CODE Crmp
    PLUGIN_CODE PcaW
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PCS_COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
)

target_sources(PCAWave PRIVATE
    src/MixPanel.cpp
    src/WaveEditor.cpp
    src/WavePresets.cpp
    src/WaveProcessor.cpp
    src/WaveView.cpp
)
target_include_directories(PCAWave PRIVATE src)
pcs_configure_plugin(PCAWave)

# The factory space: the synthetic training set as waveforms, trained at build
# time. 24 kHz, 2 s and 24 components keep it (and the plugin) small.
set(PCS_WAVE_FACTORY_DIR "${CMAKE_CURRENT_BINARY_DIR}/factory")
set(PCS_WAVE_FACTORY_MODEL "${PCS_WAVE_FACTORY_DIR}/synthetic.pcsw")
add_custom_command(
    OUTPUT "${PCS_WAVE_FACTORY_MODEL}"
    COMMAND pcs-testgen "${PCS_WAVE_FACTORY_DIR}/training"
    COMMAND pcs-train --waveform -o "${PCS_WAVE_FACTORY_MODEL}" --title "Synthetic instruments" --rate 24000 --duration 2
            --components 24 "${PCS_WAVE_FACTORY_DIR}/training"
    DEPENDS pcs-testgen pcs-train
    COMMENT "Training the PCAWave factory space"
    VERBATIM
)
add_custom_target(PcsWaveFactoryFile DEPENDS "${PCS_WAVE_FACTORY_MODEL}")
juce_add_binary_data(PcsWaveFactory HEADER_NAME WaveFactory.h NAMESPACE WaveFactory SOURCES "${PCS_WAVE_FACTORY_MODEL}")
add_dependencies(PcsWaveFactory PcsWaveFactoryFile)

target_link_libraries(PCAWave
    PRIVATE
        pcs::engine
        PcsWaveFactory
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(PCS_BUILD_TESTS)
    add_executable(pcs_wave_state_test tests/WaveStateTest.cpp)
    target_link_libraries(pcs_wave_state_test PRIVATE PCAWave pcs_tools_common)
    target_include_directories(pcs_wave_state_test PRIVATE $<TARGET_PROPERTY:PCAWave,INCLUDE_DIRECTORIES>)
    target_compile_definitions(pcs_wave_state_test PRIVATE $<TARGET_PROPERTY:PCAWave,COMPILE_DEFINITIONS>)
    target_compile_features(pcs_wave_state_test PRIVATE cxx_std_20)
    add_test(NAME wave_plugin_state COMMAND pcs_wave_state_test)

    add_executable(pcs-wave-snapshot tests/WaveSnapshot.cpp)
    target_link_libraries(pcs-wave-snapshot PRIVATE PCAWave pcs_tools_common)
    target_include_directories(pcs-wave-snapshot PRIVATE $<TARGET_PROPERTY:PCAWave,INCLUDE_DIRECTORIES>)
    target_compile_definitions(pcs-wave-snapshot PRIVATE $<TARGET_PROPERTY:PCAWave,COMPILE_DEFINITIONS>)
    target_compile_features(pcs-wave-snapshot PRIVATE cxx_std_20)
endif()
