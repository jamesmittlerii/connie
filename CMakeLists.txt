cmake_minimum_required(VERSION 3.20)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CONNIE_VERSION_RAW)
    string(STRIP "${CONNIE_VERSION_RAW}" CONNIE_VERSION)
else()
    set(CONNIE_VERSION "0.0.0")
endif()

project(connie
    VERSION ${CONNIE_VERSION}
    DESCRIPTION "Vox Continental organ simulator"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CONNIE_BUILD_LV2 "Build LV2 plugin" ON)
option(CONNIE_BUILD_VST3 "Build VST3 plugin" ON)

set(CONNIE_DSP_SOURCES
    connie_dsp.c
    connie_params.c
    reverb.c
)

set(CONNIE_DSP_HEADERS
    connie.h
    connie_dsp.h
    connie_params.h
    connie_tg.h
    connie_tuning.h
    reverb.h
    scales.h
)

add_library(connie_dsp STATIC ${CONNIE_DSP_SOURCES} ${CONNIE_DSP_HEADERS})
target_include_directories(connie_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
    target_link_libraries(connie_dsp PUBLIC m)
endif()

if(MSVC)
    target_compile_options(connie_dsp PRIVATE /W3)
else()
    target_compile_options(connie_dsp PRIVATE -Wall -O3)
endif()

if(CONNIE_BUILD_LV2)
    add_subdirectory(lv2)
endif()

if(CONNIE_BUILD_VST3 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk/CMakeLists.txt")
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/vst3sdk)
    add_subdirectory(vst3)
endif()
