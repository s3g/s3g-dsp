add_library(s3g_spectral_spray_clap MODULE
  s3g_spectral_spray_clap.cpp
)

target_link_libraries(s3g_spectral_spray_clap PRIVATE s3g_dsp)
target_include_directories(s3g_spectral_spray_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_spectral_spray_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_spectral_spray_clap PRIVATE "-framework Cocoa" "-framework Accelerate")
endif()

set_target_properties(s3g_spectral_spray_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_spectral_spray"
)

if(APPLE)
  set_target_properties(s3g_spectral_spray_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.spectral-spray"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Spectral Spray 2ch"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
