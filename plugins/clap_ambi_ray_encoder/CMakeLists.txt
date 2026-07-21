add_library(s3g_ambi_ray_encoder_clap MODULE
  s3g_ambi_ray_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_ray_encoder_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_ray_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  file(GLOB S3G_AMBI_RAY_ATLAS_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/*.s3gray"
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/manifest.json"
  )
  target_sources(s3g_ambi_ray_encoder_clap PRIVATE ${S3G_AMBI_RAY_ATLAS_FILES})
  set_source_files_properties(${S3G_AMBI_RAY_ATLAS_FILES} PROPERTIES
    HEADER_FILE_ONLY TRUE
    MACOSX_PACKAGE_LOCATION "Resources/Ray Atlas"
  )
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
    MACOSX_BUNDLE_BUNDLE_VERSION "0.4.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4.0"
  )
endif()
