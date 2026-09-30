# Shared tool helpers (argument parsing, offline note rendering) and the
# deterministic synthetic training set.
add_library(pcs_tools_common STATIC
    common/Render.cpp
    testgen/TrainingSet.cpp
)
target_include_directories(pcs_tools_common PUBLIC common testgen)
target_link_libraries(pcs_tools_common PUBLIC pcs::engine)

# Also needed by the plugin build, which trains its factory model with them.
if(PCS_BUILD_TOOLS OR PCS_BUILD_PLUGIN)
    add_executable(pcs-testgen testgen/main.cpp)
    target_link_libraries(pcs-testgen PRIVATE pcs_tools_common)

    add_executable(pcs-train train/main.cpp)
    target_link_libraries(pcs-train PRIVATE pcs_tools_common)
endif()

if(PCS_BUILD_TOOLS)
    add_executable(pcs-render render/main.cpp)
    target_link_libraries(pcs-render PRIVATE pcs_tools_common)

    add_executable(pcs-examples examples/main.cpp)
    target_link_libraries(pcs-examples PRIVATE pcs_tools_common)

    add_executable(pcs-inspect inspect/main.cpp)
    target_link_libraries(pcs-inspect PRIVATE pcs_tools_common)

    add_executable(pcs-bench bench/main.cpp)
    target_link_libraries(pcs-bench PRIVATE pcs_tools_common)
endif()
