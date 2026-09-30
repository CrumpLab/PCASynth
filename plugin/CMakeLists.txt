# PCASynth: the harmonic plugin. The shared code is in plugin-common.
option(PCS_COPY_PLUGIN_AFTER_BUILD "Install the plugin into the user plug-in folders after building" OFF)

juce_add_plugin(PCASynth
    PRODUCT_NAME "PCASynth"
    COMPANY_NAME "CrumpLab"
    BUNDLE_ID "com.crumplab.pcasynth"
    PLUGIN_MANUFACTURER_CODE Crmp
    PLUGIN_CODE PcaS
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PCS_COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
)

target_sources(PCASynth PRIVATE
    src/EnvelopeView.cpp
    src/HarmonicPresets.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
)
pcs_configure_plugin(PCASynth)

# The factory model: the synthetic training set, generated and trained at
# build time by the offline tools, then compiled into the plugin.
set(PCS_FACTORY_DIR "${CMAKE_CURRENT_BINARY_DIR}/factory")
set(PCS_FACTORY_MODEL "${PCS_FACTORY_DIR}/synthetic.pcsm")
add_custom_command(
    OUTPUT "${PCS_FACTORY_MODEL}"
    COMMAND pcs-testgen "${PCS_FACTORY_DIR}/training"
    # 32 components keeps the plugin binary small (the factory sounds need no more to play).
    COMMAND pcs-train -o "${PCS_FACTORY_MODEL}" --title "Synthetic instruments" --components 32 "${PCS_FACTORY_DIR}/training"
    DEPENDS pcs-testgen pcs-train
    COMMENT "Training the factory model"
    VERBATIM
)
add_custom_target(PcsFactoryModelFile DEPENDS "${PCS_FACTORY_MODEL}")
juce_add_binary_data(PcsFactoryModel HEADER_NAME FactoryModel.h NAMESPACE FactoryModel SOURCES "${PCS_FACTORY_MODEL}")
add_dependencies(PcsFactoryModel PcsFactoryModelFile)

target_link_libraries(PCASynth
    PRIVATE
        pcs::engine
        PcsFactoryModel
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(PCS_BUILD_TESTS)
    # Headless plugin checks: MIDI in -> sound out, state round trip with an
    # embedded model, model swaps.
    add_executable(pcs_plugin_state_test tests/StateTest.cpp)
    target_link_libraries(pcs_plugin_state_test PRIVATE PCASynth pcs_tools_common)
    target_include_directories(pcs_plugin_state_test PRIVATE $<TARGET_PROPERTY:PCASynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(pcs_plugin_state_test PRIVATE $<TARGET_PROPERTY:PCASynth,COMPILE_DEFINITIONS>)
    target_compile_features(pcs_plugin_state_test PRIVATE cxx_std_20)
    add_test(NAME plugin_state COMMAND pcs_plugin_state_test)

    # Headless render of the editor to a PNG.
    add_executable(pcs-ui-snapshot tests/UiSnapshot.cpp)
    target_link_libraries(pcs-ui-snapshot PRIVATE PCASynth pcs_tools_common)
    target_include_directories(pcs-ui-snapshot PRIVATE $<TARGET_PROPERTY:PCASynth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(pcs-ui-snapshot PRIVATE $<TARGET_PROPERTY:PCASynth,COMPILE_DEFINITIONS>)
    target_compile_features(pcs-ui-snapshot PRIVATE cxx_std_20)
endif()
