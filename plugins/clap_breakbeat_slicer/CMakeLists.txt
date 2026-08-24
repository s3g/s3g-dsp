add_library(s3g_breakbeat_slicer_clap MODULE
  s3g_breakbeat_slicer_clap.cpp
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Medium.ttf
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-SemiBold.ttf)

find_package(Threads REQUIRED)
target_link_libraries(s3g_breakbeat_slicer_clap PRIVATE
  s3g_dsp
  Threads::Threads)
target_include_directories(s3g_breakbeat_slicer_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_features(s3g_breakbeat_slicer_clap PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_breakbeat_slicer_clap PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

set_target_properties(s3g_breakbeat_slicer_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_slicer")

if(APPLE)
  set_source_files_properties(s3g_breakbeat_slicer_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(s3g_breakbeat_slicer_clap PRIVATE -fobjc-arc)
  target_link_libraries(s3g_breakbeat_slicer_clap PRIVATE
    "-framework Cocoa"
    "-framework AVFoundation"
    "-framework CoreText"
    "-framework CoreFoundation")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Medium.ttf
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-SemiBold.ttf
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Fonts")
  set_target_properties(s3g_breakbeat_slicer_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.breakbeat-slicer"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sample Slicer 16"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.9.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.9.0")
  # REAPER fingerprints the outer .clap directory when deciding whether a
  # cached factory needs rescanning. Incremental bundle links update Contents
  # but otherwise leave the container timestamp unchanged.
  add_custom_command(TARGET s3g_breakbeat_slicer_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch
      "$<TARGET_BUNDLE_DIR:s3g_breakbeat_slicer_clap>"
    VERBATIM)
endif()
