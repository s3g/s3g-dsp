add_library(s3g_drum_echo_clap MODULE
  s3g_drum_echo_clap.cpp
)

target_link_libraries(s3g_drum_echo_clap PRIVATE s3g_dsp)
target_include_directories(s3g_drum_echo_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

set_target_properties(s3g_drum_echo_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_drum_echo"
)

if(APPLE)
  set_source_files_properties(s3g_drum_echo_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_drum_echo_clap PRIVATE "-framework Cocoa")
  set_target_properties(s3g_drum_echo_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.drum-echo"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Drum Echo"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )
endif()
