add_library(s3g_delay_field_clap MODULE
  s3g_delay_field_clap.cpp
)

target_link_libraries(s3g_delay_field_clap PRIVATE s3g_dsp)
target_include_directories(s3g_delay_field_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_delay_field_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_delay_field_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_delay_field_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_delay_field"
)

if(APPLE)
  set_target_properties(s3g_delay_field_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.delay-field"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Effect Delay Field 16"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
