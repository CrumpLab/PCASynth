# JUCE, and the code both plugins share: the processor base (SpaceProcessor),
# parameters, presets, training, inspection and most of the editor's parts.
# Each plugin compiles these files itself (JUCE code is configured per plugin).
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(PCS_PLUGIN_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE INTERNAL "")
set(PCS_PLUGIN_COMMON_SOURCES
    ${PCS_PLUGIN_COMMON_DIR}/Controls.cpp
    ${PCS_PLUGIN_COMMON_DIR}/InspectPanel.cpp
    ${PCS_PLUGIN_COMMON_DIR}/Inspector.cpp
    ${PCS_PLUGIN_COMMON_DIR}/MorphPad.cpp
    ${PCS_PLUGIN_COMMON_DIR}/ParamGrid.cpp
    ${PCS_PLUGIN_COMMON_DIR}/Params.cpp
    ${PCS_PLUGIN_COMMON_DIR}/PresetBar.cpp
    ${PCS_PLUGIN_COMMON_DIR}/Presets.cpp
    ${PCS_PLUGIN_COMMON_DIR}/SoundMap.cpp
    ${PCS_PLUGIN_COMMON_DIR}/SpaceEditor.cpp
    ${PCS_PLUGIN_COMMON_DIR}/SpaceProcessor.cpp
    ${PCS_PLUGIN_COMMON_DIR}/Theme.cpp
    ${PCS_PLUGIN_COMMON_DIR}/TrainPanel.cpp
    ${PCS_PLUGIN_COMMON_DIR}/Trainer.cpp
    CACHE INTERNAL "")

# Adds the shared code and the usual settings to a plugin target.
function(pcs_configure_plugin target)
    target_sources(${target} PRIVATE ${PCS_PLUGIN_COMMON_SOURCES})
    target_include_directories(${target} PRIVATE ${PCS_PLUGIN_COMMON_DIR})
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )
endfunction()
