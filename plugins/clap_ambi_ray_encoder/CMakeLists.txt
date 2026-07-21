add_library(s3g_ambi_ray_encoder_clap MODULE
  s3g_ambi_ray_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_ray_encoder_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_ray_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_ambi_ray_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_ray_encoder_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_ray_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_ray_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_ray_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-ray-encoder"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Ray Encoder"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.2.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.0"
  )
endif()
