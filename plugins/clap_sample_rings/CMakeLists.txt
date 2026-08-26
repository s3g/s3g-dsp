add_library(s3g_sample_rings_clap MODULE
  s3g_sample_rings_clap.cpp
)

target_link_libraries(s3g_sample_rings_clap PRIVATE s3g_dsp)
target_include_directories(s3g_sample_rings_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_features(s3g_sample_rings_clap PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_sample_rings_clap PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(APPLE)
  set_source_files_properties(s3g_sample_rings_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_sample_rings_clap PRIVATE
    "-framework Cocoa"
    "-framework AVFoundation"
    "-framework QuartzCore")
endif()

set_target_properties(s3g_sample_rings_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_sample_rings"
)

if(APPLE)
  set_target_properties(s3g_sample_rings_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.sample-rings-8"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sample Rings 8"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
  add_custom_command(TARGET s3g_sample_rings_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch
      "$<TARGET_BUNDLE_DIR:s3g_sample_rings_clap>"
    VERBATIM)
endif()
