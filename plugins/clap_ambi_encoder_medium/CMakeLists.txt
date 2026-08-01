add_library(s3g_ambi_encoder_medium_clap MODULE
  s3g_ambi_encoder_medium_clap.cpp
)

target_link_libraries(s3g_ambi_encoder_medium_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_encoder_medium_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

if(APPLE)
  set_source_files_properties(s3g_ambi_encoder_medium_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_encoder_medium_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_encoder_medium_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_encoder_medium"
)

if(APPLE)
  set_target_properties(s3g_ambi_encoder_medium_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-encoder-medium-16"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Medium 16"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.5.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.5.0"
  )
endif()
