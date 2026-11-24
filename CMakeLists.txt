cmake_minimum_required(VERSION 3.20)

set(PROJECT_VERSION "1.0.0")
set(PROJECT_NAME "SetlistQueue")

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX)

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

# Descobre a raiz de include a partir de .../gigperformer/sdk/GigPerformerAPI.h
file(GLOB_RECURSE _gp_api_hdr "${GPSDK_DIR}/GigPerformerAPI.h")
list(FILTER _gp_api_hdr EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/")
if(NOT _gp_api_hdr)
    message(FATAL_ERROR "GigPerformerAPI.h nao encontrado em ${GPSDK_DIR}")
endif()
list(GET _gp_api_hdr 0 _gp_api_hdr_first)
get_filename_component(_gp_sdk_hdr_dir "${_gp_api_hdr_first}" DIRECTORY)
get_filename_component(GPSDK_INCLUDE_DIR "${_gp_sdk_hdr_dir}/../.." ABSOLUTE)

# Fontes .cpp do SDK (ignora exemplos)
file(GLOB_RECURSE GPSDK_SOURCES "${GPSDK_DIR}/*.cpp")
list(FILTER GPSDK_SOURCES EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")
if(NOT GPSDK_SOURCES)
    message(FATAL_ERROR "Nenhum .cpp do SDK encontrado em ${GPSDK_DIR}")
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
    "${GPSDK_INCLUDE_DIR}"
    "${GENERATED_DIR}")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /EHsc /W3)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32 gdi32 ole32 oleaut32 shell32 shlwapi advapi32 version dwmapi)
endif()