cmake_minimum_required(VERSION 3.25.0)

set(connie_vst3_sources
    source/connie_plugin.cpp
    ${CMAKE_SOURCE_DIR}/external/vst3sdk/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)

set(target connie)

smtg_add_vst3plugin(${target} PACKAGE_NAME "Connie" SOURCES_LIST ${connie_vst3_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)

target_include_directories(${target}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${target}
    PRIVATE
        connie_dsp
        sdk
)

if(SMTG_WIN)
    target_sources(${target} PRIVATE resource/connie.rc)
endif()
