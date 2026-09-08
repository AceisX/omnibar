cmake_minimum_required(VERSION 3.24)
project(OmniBar VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT statica: eseguibile singolo, nessun VC Redist (vincolo portable, §2).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Si ottimizza per dimensione, non per velocita': il budget della §3 e' sulla
# dimensione e sulla RAM, non sul throughput. Il /O2 di default va tolto,
# altrimenti il compilatore avvisa del conflitto con /O1.
string(REPLACE "/O2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(omnibar WIN32
    src/main.cpp
    src/core/common.cpp
    src/core/log.cpp
    src/core/paths.cpp
    src/omnibar.rc
)

target_include_directories(omnibar PRIVATE src)

target_compile_definitions(omnibar PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

target_compile_options(omnibar PRIVATE
    /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
    $<$<CONFIG:Release>:/O1 /Oi /Gy /Gw /GL /Zc:inline>
)

target_link_options(omnibar PRIVATE
    /MANIFEST:NO   # il manifest Per-Monitor V2 arriva da omnibar.rc
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

# Tutto dal Windows SDK: nessuna dipendenza esterna per l'host (§ tabella §4).
target_link_libraries(omnibar PRIVATE
    user32 shell32 shlwapi ole32 advapi32
)

set_target_properties(omnibar PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
)
