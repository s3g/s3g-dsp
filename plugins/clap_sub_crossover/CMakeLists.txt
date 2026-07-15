add_library(s3g_sub_crossover_clap MODULE
  s3g_sub_crossover_clap.cpp
)

target_link_libraries(s3g_sub_crossover_clap PRIVATE s3g_dsp)
target_include_directories(s3g_sub_crossover_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_sub_crossover_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_sub_crossover_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_sub_crossover_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_sub_crossover"
)

if(APPLE)
  set_target_properties(s3g_sub_crossover_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.sub-crossover"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sub Crossover"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
