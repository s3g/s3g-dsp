add_library(s3g_psd_raw_field_clap MODULE
  s3g_psd_raw_field_clap.cpp
)

set(S3G_FAULT_VERSION "0.3.0")

target_link_libraries(s3g_psd_raw_field_clap PRIVATE s3g_dsp)
target_include_directories(s3g_psd_raw_field_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_psd_raw_field_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_psd_raw_field_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_psd_raw_field_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_fault"
)

if(APPLE)
  set_target_properties(s3g_psd_raw_field_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.fault"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Fault"
    MACOSX_BUNDLE_BUNDLE_VERSION "${S3G_FAULT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${S3G_FAULT_VERSION}"
  )
endif()
