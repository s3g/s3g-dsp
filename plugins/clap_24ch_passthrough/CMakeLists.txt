add_library(s3g_24ch_passthrough_clap MODULE
  s3g_24ch_passthrough_clap.cpp
)

target_link_libraries(s3g_24ch_passthrough_clap PRIVATE s3g_dsp)
target_include_directories(s3g_24ch_passthrough_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

set_target_properties(s3g_24ch_passthrough_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_24ch_passthrough_test"
)

if(APPLE)
  set_target_properties(s3g_24ch_passthrough_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.24ch-passthrough-test"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g 24ch Passthrough Test"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
