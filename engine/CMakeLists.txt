# The PCA synth engine: plain C++, no framework dependency, so it can be built,
# tested and used for offline renders anywhere.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(pcs_engine STATIC
    src/Fft.cpp
    src/Harmonic.cpp
    src/Model.cpp
    src/Pca.cpp
    src/Synth.cpp
    src/Wav.cpp
)
add_library(pcs::engine ALIAS pcs_engine)

target_include_directories(pcs_engine PUBLIC include)
target_link_libraries(pcs_engine PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(pcs_engine PUBLIC
    PCS_VERSION_STRING="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(pcs_engine PRIVATE /W4)
else()
    target_compile_options(pcs_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
