add_library(s3g_ambi_imprint_clap MODULE
  s3g_ambi_imprint_clap.cpp
)

target_link_libraries(s3g_ambi_imprint_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_imprint_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  file(GLOB S3G_AMBI_IMPRINT_ATLAS_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/*.s3gimprint"
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/manifest.json"
  )
  target_sources(s3g_ambi_imprint_clap PRIVATE ${S3G_AMBI_IMPRINT_ATLAS_FILES})
  set_source_files_properties(${S3G_AMBI_IMPRINT_ATLAS_FILES} PROPERTIES
    HEADER_FILE_ONLY TRUE
    MACOSX_PACKAGE_LOCATION "Resources/Imprint Atlas"
  )
  set_source_files_properties(s3g_ambi_imprint_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_imprint_clap PRIVATE "-framework Cocoa" "-framework Accelerate")
endif()

set_target_properties(s3g_ambi_imprint_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_imprint"
)

if(APPLE)
  set_target_properties(s3g_ambi_imprint_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-imprint-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Imprint 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
