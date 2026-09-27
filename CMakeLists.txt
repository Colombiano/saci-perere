cmake_minimum_required(VERSION 3.20)
project(saci VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SACI_WITH_LUA "Habilita configuracao via scripts Lua (sol2)" OFF)

add_executable(saci
    src/main.cpp
    src/proc.cpp
    src/subtitle.cpp
    src/stream.cpp
    src/translate.cpp
    src/tts.cpp
    src/sync.cpp
    src/muxer.cpp
    src/reactor.cpp
    src/config.cpp
    src/bw_probe.cpp
    src/orchestrator.cpp
)
target_include_directories(saci PRIVATE include)
target_compile_features(saci PRIVATE cxx_std_20)
target_compile_options(saci PRIVATE -Wall -Wextra)

# Lua (opcional): scripts por-site/per-canal carregados em runtime.
# sol2 vem vendored em vendor/sol (single header, MIT) — sem download externo.
if(SACI_WITH_LUA)
    find_package(Lua REQUIRED)
    set(SOL2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
    if(EXISTS "${SOL2_INCLUDE_DIR}/sol/sol.hpp")
        target_compile_definitions(saci PRIVATE SACI_WITH_LUA)
        target_include_directories(saci PRIVATE ${LUA_INCLUDE_DIR})
        # sol2 e third-party: warnings dele nao sao nossos (SYSTEM)
        target_include_directories(saci SYSTEM PRIVATE ${SOL2_INCLUDE_DIR})
        # falso positivo conhecido GCC+sol2 (string literal em get_or)
        target_compile_options(saci PRIVATE -Wno-array-bounds)
        target_link_libraries(saci PRIVATE ${LUA_LIBRARIES})
    else()
        message(WARNING "vendor/sol/sol.hpp ausente; compilando sem Lua")
    endif()
endif()

install(TARGETS saci DESTINATION bin)
