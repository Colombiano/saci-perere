cmake_minimum_required(VERSION 3.20)
project(saci VERSION 0.1.0 LANGUAGES CXX)

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
    src/config.cpp
    src/orchestrator.cpp
)
target_include_directories(saci PRIVATE include)
target_compile_features(saci PRIVATE cxx_std_20)
target_compile_options(saci PRIVATE -Wall -Wextra)

# Lua (opcional): scripts por-site/per-canal carregados em runtime.
if(SACI_WITH_LUA)
    find_package(Lua REQUIRED)
    find_path(SOL2_INCLUDE_DIR sol/sol.hpp)
    if(SOL2_INCLUDE_DIR)
        target_compile_definitions(saci PRIVATE SACI_WITH_LUA)
        target_include_directories(saci PRIVATE ${LUA_INCLUDE_DIR} ${SOL2_INCLUDE_DIR})
        target_link_libraries(saci PRIVATE ${LUA_LIBRARIES})
    else()
        message(WARNING "sol2 nao encontrado; compilando sem Lua")
    endif()
endif()

install(TARGETS saci DESTINATION bin)
