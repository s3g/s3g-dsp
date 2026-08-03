# Opt-in macOS-only allocation/deallocation interposition for the realtime
# CLAP audit. Include this file from the top-level CMakeLists; its targets do
# not exist unless S3G_ENABLE_REALTIME_ALLOC_PROBE is explicitly enabled.

option(S3G_ENABLE_REALTIME_ALLOC_PROBE
  "Build the injected macOS realtime malloc/free probe" OFF)

if(NOT S3G_ENABLE_REALTIME_ALLOC_PROBE)
  return()
endif()

if(NOT APPLE)
  message(FATAL_ERROR
    "S3G_ENABLE_REALTIME_ALLOC_PROBE is supported only on macOS")
endif()

add_library(s3g_realtime_alloc_probe SHARED
  ${CMAKE_CURRENT_LIST_DIR}/../tests/realtime_alloc_probe_macos.cpp)
target_include_directories(s3g_realtime_alloc_probe PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../tests)
target_compile_features(s3g_realtime_alloc_probe PRIVATE cxx_std_17)
set_target_properties(s3g_realtime_alloc_probe PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES)

add_executable(s3g_realtime_alloc_probe_selftest
  ${CMAKE_CURRENT_LIST_DIR}/../tests/realtime_alloc_probe_selftest.cpp)
target_include_directories(s3g_realtime_alloc_probe_selftest PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../tests)
target_compile_features(s3g_realtime_alloc_probe_selftest PRIVATE cxx_std_17)
target_link_libraries(s3g_realtime_alloc_probe_selftest PRIVATE
  ${CMAKE_DL_LIBS})

add_custom_target(run_realtime_alloc_probe_selftest
  COMMAND ${CMAKE_COMMAND} -E env
    "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:s3g_realtime_alloc_probe>"
    $<TARGET_FILE:s3g_realtime_alloc_probe_selftest>
  DEPENDS
    s3g_realtime_alloc_probe
    s3g_realtime_alloc_probe_selftest
  COMMENT "Testing opt-in realtime allocation interposition"
  VERBATIM)

# This module is included before the optional CLAP targets are declared. Defer
# the end-to-end positive control until the root directory has been evaluated,
# then add it only when the realtime audit itself exists.
function(s3g_add_realtime_alloc_probe_positive_control)
  if(NOT TARGET s3g_clap_realtime_audit)
    message(STATUS
      "Realtime allocation CLAP positive control omitted (CLAP build disabled)")
    return()
  endif()

  set(test_directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests")
  add_library(s3g_realtime_alloc_positive_control_clap MODULE
    "${test_directory}/realtime_alloc_positive_control_clap.cpp")
  target_include_directories(s3g_realtime_alloc_positive_control_clap PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_compile_features(s3g_realtime_alloc_positive_control_clap PRIVATE
    cxx_std_17)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_realtime_alloc_positive_control_clap PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(s3g_realtime_alloc_positive_control_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_realtime_alloc_positive_control"
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER
      "org.s3g.test.realtime-alloc-positive-control"
    MACOSX_BUNDLE_BUNDLE_NAME
      "s3g Realtime Allocation Positive Control"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1")

  set(report
    "${CMAKE_CURRENT_BINARY_DIR}/realtime_alloc_positive_control.json")
  add_custom_target(run_realtime_alloc_probe_clap_positive_control
    COMMAND ${CMAKE_COMMAND} -E rm -f "${report}"
    COMMAND ${CMAKE_COMMAND} -E env
      "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:s3g_realtime_alloc_probe>"
      $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000
      --blocks 64
      --warmup 4
      --iterations 11
      --event-burst 1
      --allocation-probe
      --json "${report}"
      $<TARGET_FILE:s3g_realtime_alloc_positive_control_clap>
      org.s3g.test.realtime-alloc-positive-control
    COMMAND ${CMAKE_COMMAND} "-DREPORT=${report}" -P
      "${test_directory}/check_realtime_alloc_positive_control.cmake"
    DEPENDS
      s3g_clap_realtime_audit
      s3g_realtime_alloc_probe
      s3g_realtime_alloc_positive_control_clap
      "${test_directory}/check_realtime_alloc_positive_control.cmake"
    BYPRODUCTS "${report}"
    COMMENT "Testing realtime allocation audit with a known-bad CLAP"
    VERBATIM)

  add_custom_target(run_realtime_alloc_probe_tests)
  add_dependencies(run_realtime_alloc_probe_tests
    run_realtime_alloc_probe_selftest
    run_realtime_alloc_probe_clap_positive_control)
endfunction()

cmake_language(DEFER CALL s3g_add_realtime_alloc_probe_positive_control)
