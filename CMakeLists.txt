cmake_minimum_required(VERSION 3.20)

set(PROJECT_VERSION "1.0.0")
set(PROJECT_NAME "SetlistQueue")

# C e C++: o gp-sdk tem uma parte em C (gp-sdk/c/src) alem da parte C++
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT estática: a DLL não depende do VC++ Redistributable instalado no PC do show
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# GP SDK (o workflow já clona em ./gp-sdk; localmente o CMake clona se faltar)
# ---------------------------------------------------------------------------
set(GPSDK_DIR "${CMAKE_SOURCE_DIR}/gp-sdk" CACHE PATH "Pasta do gp-sdk oficial")

if(NOT EXISTS "${GPSDK_DIR}")
    find_package(Git REQUIRED)
    message(STATUS "Baixando gp-sdk em ${GPSDK_DIR} ...")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone --depth 1
                https://github.com/gigperformer/gp-sdk.git "${GPSDK_DIR}"
        RESULT_VARIABLE _git_result)
    if(NOT _git_result EQUAL 0)
        message(FATAL_ERROR "Falha ao clonar o gp-sdk")
    endif()
endif()

# ---------------------------------------------------------------------------
# Pastas de include do SDK.
# Layout esperado do gp-sdk:
#   gp-sdk/cpp/include/gigperformer/sdk/GigPerformerAPI.h (e demais headers C++)
#   gp-sdk/c/include/gigperformer/sdk/types.h, imports.h  (headers C)
# Alem de listar as pastas, descobrimos a raiz de include de cada header
# procurando no SDK, para o build nao quebrar se o layout mudar.
# ---------------------------------------------------------------------------
set(GPSDK_INCLUDE_DIRS "")

foreach(_dir "${GPSDK_DIR}/cpp/include" "${GPSDK_DIR}/c/include" "${GPSDK_DIR}/include")
    if(EXISTS "${_dir}")
        list(APPEND GPSDK_INCLUDE_DIRS "${_dir}")
    endif()
endforeach()

foreach(_hdr GigPerformerAPI.h types.h imports.h)
    file(GLOB_RECURSE _found "${GPSDK_DIR}/${_hdr}")
    list(FILTER _found EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/")
    if(NOT _found)
        message(FATAL_ERROR "${_hdr} nao encontrado em ${GPSDK_DIR}")
    endif()
    list(GET _found 0 _first)
    get_filename_component(_hdr_dir "${_first}" DIRECTORY)           # .../gigperformer/sdk
    get_filename_component(_root "${_hdr_dir}/../.." ABSOLUTE)       # raiz de include
    list(APPEND GPSDK_INCLUDE_DIRS "${_root}")
endforeach()

list(REMOVE_DUPLICATES GPSDK_INCLUDE_DIRS)
message(STATUS "GP SDK include dirs: ${GPSDK_INCLUDE_DIRS}")

# ---------------------------------------------------------------------------
# Fontes do SDK: C++ (gp-sdk/cpp/src) e C (gp-sdk/c/src)
# ---------------------------------------------------------------------------
# C++ (ignora exemplos)
file(GLOB_RECURSE GPSDK_CPP_SOURCES "${GPSDK_DIR}/*.cpp")
list(FILTER GPSDK_CPP_SOURCES EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")
if(NOT GPSDK_CPP_SOURCES)
    message(FATAL_ERROR "Nenhum .cpp do SDK encontrado em ${GPSDK_DIR}")
endif()

# C: imports.c, utils.c etc. (implementam as funcoes GP_* e hex2bin)
if(EXISTS "${GPSDK_DIR}/c/src")
    file(GLOB_RECURSE GPSDK_C_SOURCES "${GPSDK_DIR}/c/src/*.c")
else()
    file(GLOB_RECURSE GPSDK_C_SOURCES "${GPSDK_DIR}/*.c")
endif()
list(FILTER GPSDK_C_SOURCES EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")
if(NOT GPSDK_C_SOURCES)
    message(FATAL_ERROR "Nenhum .c do SDK encontrado em ${GPSDK_DIR} (esperado em c/src)")
endif()

message(STATUS "GP SDK C++ sources: ${GPSDK_CPP_SOURCES}")
message(STATUS "GP SDK C sources:   ${GPSDK_C_SOURCES}")

set(GPSDK_SOURCES ${GPSDK_CPP_SOURCES} ${GPSDK_C_SOURCES})

# ---------------------------------------------------------------------------
# Arquivo .def do SDK (se existir fora das pastas de exemplo)
# ---------------------------------------------------------------------------
set(GPSDK_DEF_FILE "")
file(GLOB_RECURSE _def_files "${GPSDK_DIR}/*.def")
list(FILTER _def_files EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")
if(_def_files)
    list(GET _def_files 0 GPSDK_DEF_FILE)
    message(STATUS "GP SDK .def encontrado: ${GPSDK_DEF_FILE}")
else()
    message(STATUS "Nenhum .def do SDK encontrado; usando /EXPORT explicito")
endif()

# ---------------------------------------------------------------------------
# Embute ui/index.html como array de bytes (sem limite de tamanho de string)
# ---------------------------------------------------------------------------
set(UI_HTML "${CMAKE_SOURCE_DIR}/ui/index.html")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UI_HTML}")

file(READ "${UI_HTML}" _html_hex HEX)
string(LENGTH "${_html_hex}" _hex_len)
math(EXPR _html_size "${_hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _html_bytes "${_html_hex}")

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${GENERATED_DIR}/webcontent.h"
"#pragma once
#include <cstddef>
static const unsigned char kWebContent[] = { ${_html_bytes} 0x00 };
static const std::size_t kWebContentSize = ${_html_size};
")

# ---------------------------------------------------------------------------
# A extensao (DLL)
# ---------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    src/LibMain.cpp
    src/LibMain.h
    ${GPSDK_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/out>")

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/libs"
    ${GPSDK_INCLUDE_DIRS}
    "${GENERATED_DIR}")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W3
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)

    # O Gig Performer procura estas duas funcoes, sem decoracao, na tabela de
    # exports da DLL: GPQueryLibrary (informacoes) e GPRegister (carregamento).
    # Se o SDK ja as exporta, o linker so emite o aviso LNK4197 (inofensivo).
    target_link_options(${PROJECT_NAME} PRIVATE
        /EXPORT:GPQueryLibrary
        /EXPORT:GPRegister)

    if(GPSDK_DEF_FILE)
        file(TO_NATIVE_PATH "${GPSDK_DEF_FILE}" _def_native)
        target_link_options(${PROJECT_NAME} PRIVATE "/DEF:${_def_native}")
    endif()
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32 gdi32 ole32 oleaut32 shell32 shlwapi advapi32 version dwmapi)
endif()
