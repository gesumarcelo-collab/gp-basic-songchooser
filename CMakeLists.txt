cmake_minimum_required(VERSION 3.20)

set(PROJECT_VERSION "1.0.0")
set(PROJECT_NAME "SetlistQueue")

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT estatica: a DLL nao depende do VC++ Redistributable instalado no PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# GP SDK
# ---------------------------------------------------------------------------
set(GPSDK_DIR "${CMAKE_SOURCE_DIR}/gp-sdk" CACHE PATH "Pasta do gp-sdk oficial")

if(NOT EXISTS "${GPSDK_DIR}")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone --depth 1
                "https://github.com" "${GPSDK_DIR}"
        RESULT_VARIABLE _git_result)
    if(NOT _git_result EQUAL 0)
        message(FATAL_ERROR "Falha ao clonar o gp-sdk")
    endif()
endif()

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
    get_filename_component(_hdr_dir "${_first}" DIRECTORY)
    get_filename_component(_root "${_hdr_dir}/../.." ABSOLUTE)
    list(APPEND GPSDK_INCLUDE_DIRS "${_root}")
endforeach()
list(REMOVE_DUPLICATES GPSDK_INCLUDE_DIRS)

file(GLOB_RECURSE GPSDK_CPP_SOURCES "${GPSDK_DIR}/*.cpp")
list(FILTER GPSDK_CPP_SOURCES EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")

if(EXISTS "${GPSDK_DIR}/c/src")
    file(GLOB_RECURSE GPSDK_C_SOURCES "${GPSDK_DIR}/c/src/*.c")
else()
    file(GLOB_RECURSE GPSDK_C_SOURCES "${GPSDK_DIR}/*.c")
endif()
list(FILTER GPSDK_C_SOURCES EXCLUDE REGEX "[Ee]xample|[Ss]ample|/build/|/[Tt]est")

set(GPSDK_SOURCES ${GPSDK_CPP_SOURCES} ${GPSDK_C_SOURCES})

# ---------------------------------------------------------------------------
# Embute ui/index.html como array de bytes
# ---------------------------------------------------------------------------
set(UI_HTML "${CMAKE_SOURCE_DIR}/ui/index.html")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UI_HTML}")

file(READ "${UI_HTML}" _html_hex HEX)
string(LENGTH "${_html_hex}" _hex_len)
math(EXPR _html_size "${_hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _html_bytes "${_html_hex}")

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
file(WRITE "${GENERATED_DIR}/webcontent.h"
"#pragma once
#include <cstddef>
static const unsigned char kWebContent[] = { ${_html_bytes} 0x00 };
static const std::size_t kWebContentSize = ${_html_size};
")

# ---------------------------------------------------------------------------
# Gera arquivo .def explicito para garantir exports limpos no Windows x64
# ---------------------------------------------------------------------------
set(EXPORTS_DEF "${GENERATED_DIR}/gp_exports.def")
file(WRITE "${EXPORTS_DEF}"
"LIBRARY ${PROJECT_NAME}
EXPORTS
    GPQueryLibrary
    GPRegister
")

# ---------------------------------------------------------------------------
# A extensao (DLL)
# ---------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    src/LibMain.cpp
    src/LibMain.h
    ${GPSDK_SOURCES}
    "${EXPORTS_DEF}")

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
    _UNICODE
    BUILDING_GP_EXTENSION=1)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W3
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32 gdi32 ole32 oleaut32 shell32 shlwapi advapi32 version dwmapi)
endif()
