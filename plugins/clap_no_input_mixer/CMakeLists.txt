add_library(s3g_no_input_mixer_clap MODULE
  s3g_no_input_mixer_clap.cpp
)

target_link_libraries(s3g_no_input_mixer_clap PRIVATE s3g_dsp)
target_include_directories(s3g_no_input_mixer_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_no_input_mixer_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_no_input_mixer_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_no_input_mixer_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_no_input_mixer"
)

if(APPLE)
  set_target_properties(s3g_no_input_mixer_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.no-input-mixer-8ch"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Processor No Input Mixer 8ch"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.6.5"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.6.5"
  )
endif()
