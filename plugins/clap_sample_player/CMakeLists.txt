add_library(s3g_sample_player_clap MODULE
  s3g_sample_player_clap.cpp)

find_package(Threads REQUIRED)
target_link_libraries(s3g_sample_player_clap PRIVATE
  s3g_dsp
  Threads::Threads)
target_include_directories(s3g_sample_player_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_features(s3g_sample_player_clap PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_sample_player_clap PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

set_target_properties(s3g_sample_player_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_sample_player")

if(APPLE)
  set_source_files_properties(s3g_sample_player_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(s3g_sample_player_clap PRIVATE -fobjc-arc)
  target_link_libraries(s3g_sample_player_clap PRIVATE
    "-framework Cocoa"
    "-framework AVFoundation")
  set_target_properties(s3g_sample_player_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.sample-player"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sample Player 2"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.2.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.0")
  add_custom_command(TARGET s3g_sample_player_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch
      "$<TARGET_BUNDLE_DIR:s3g_sample_player_clap>"
    VERBATIM)
endif()
