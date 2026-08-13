add_library(s3g_low_frequency_synth_clap MODULE
  s3g_low_frequency_synth_clap.cpp
)

target_link_libraries(s3g_low_frequency_synth_clap PRIVATE s3g_dsp)
target_include_directories(s3g_low_frequency_synth_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

set_target_properties(s3g_low_frequency_synth_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_low_frequency_synth"
)

if(APPLE)
  set_source_files_properties(s3g_low_frequency_synth_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_low_frequency_synth_clap PRIVATE
    "-framework Cocoa")
  set_target_properties(s3g_low_frequency_synth_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.low-frequency-synth"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Low Frequency Synth"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.3.2"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.3.2"
  )
endif()
