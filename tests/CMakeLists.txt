FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(pcs_tests
    HarmonicTests.cpp
    ModelTests.cpp
    ModulationTests.cpp
    PcaTests.cpp
    SynthTests.cpp
    TrainingSetTests.cpp
)
target_link_libraries(pcs_tests PRIVATE pcs_tools_common Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(pcs_tests)
