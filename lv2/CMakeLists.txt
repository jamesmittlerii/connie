if(NOT CONNIE_BUILD_LV2)
  return()
endif()

include(GNUInstallDirs)

if(WIN32)
  set(default_lv2_bundle_dir "$ENV{USERPROFILE}/.lv2/connie.lv2")
  set(CONNIE_LV2_BINARY_NAME "connie.dll")
else()
  set(default_lv2_bundle_dir "${CMAKE_INSTALL_FULL_LIBDIR}/lv2/connie.lv2")
  set(CONNIE_LV2_BINARY_NAME "connie.so")
endif()

set(CONNIE_LV2_BUNDLE_DIR "${default_lv2_bundle_dir}" CACHE PATH "LV2 bundle install directory")

configure_file(
  "${CMAKE_SOURCE_DIR}/lv2/connie.lv2/manifest.ttl.in"
  "${CMAKE_BINARY_DIR}/connie.lv2/manifest.ttl"
  @ONLY
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/connie.lv2")

add_library(connie-lv2 MODULE
  source/connie_lv2.c
)

set_target_properties(connie-lv2 PROPERTIES
  OUTPUT_NAME "connie"
  PREFIX ""
)

if(WIN32)
  set_target_properties(connie-lv2 PROPERTIES SUFFIX ".dll")
endif()

target_link_libraries(connie-lv2 PRIVATE connie_dsp)

target_include_directories(connie-lv2 PRIVATE
  "${CMAKE_SOURCE_DIR}/lv2/include"
  "${CMAKE_SOURCE_DIR}"
)

if(WIN32)
  target_compile_definitions(connie-lv2 PRIVATE _USE_MATH_DEFINES)
endif()

add_executable(headless-dsp-test
  "${CMAKE_SOURCE_DIR}/tests/headless_dsp_test.c"
)

target_link_libraries(headless-dsp-test PRIVATE connie_dsp)

add_custom_target(test-headless
  COMMAND headless-dsp-test
  DEPENDS headless-dsp-test
  COMMENT "Running headless DSP regression test"
)

add_custom_command(TARGET connie-lv2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:connie-lv2>
          "${CMAKE_BINARY_DIR}/connie.lv2/${CONNIE_LV2_BINARY_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/lv2/connie.lv2/connie.ttl"
          "${CMAKE_BINARY_DIR}/connie.lv2/connie.ttl"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/connie.lv2/manifest.ttl"
          "${CMAKE_BINARY_DIR}/connie.lv2/manifest.ttl"
)

install(TARGETS connie-lv2
  LIBRARY DESTINATION "${CONNIE_LV2_BUNDLE_DIR}"
  RUNTIME DESTINATION "${CONNIE_LV2_BUNDLE_DIR}"
)

install(FILES
  "${CMAKE_SOURCE_DIR}/lv2/connie.lv2/connie.ttl"
  "${CMAKE_BINARY_DIR}/connie.lv2/manifest.ttl"
  DESTINATION "${CONNIE_LV2_BUNDLE_DIR}"
)
