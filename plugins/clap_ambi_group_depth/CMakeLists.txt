add_library(s3g_ambi_depth_16_clap MODULE
  s3g_ambi_group_depth_clap.cpp
)

add_library(s3g_ambi_group_depth_64_clap MODULE
  s3g_ambi_group_depth_clap.cpp
)

add_library(s3g_ambi_group_depth_128_clap MODULE
  s3g_ambi_group_depth_clap.cpp
)

target_link_libraries(s3g_ambi_depth_16_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_depth_16_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_definitions(s3g_ambi_depth_16_clap PRIVATE S3G_AMBI_DEPTH_16=1)

target_link_libraries(s3g_ambi_group_depth_64_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_group_depth_64_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

target_link_libraries(s3g_ambi_group_depth_128_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_group_depth_128_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_definitions(s3g_ambi_group_depth_128_clap PRIVATE S3G_AMBI_GROUP_DEPTH_128=1)

if(APPLE)
  set_source_files_properties(s3g_ambi_group_depth_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_depth_16_clap PRIVATE "-framework Cocoa")
  target_link_libraries(s3g_ambi_group_depth_64_clap PRIVATE "-framework Cocoa")
  target_link_libraries(s3g_ambi_group_depth_128_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_depth_16_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_depth_16"
)

set_target_properties(s3g_ambi_group_depth_64_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_group_depth_64"
)

set_target_properties(s3g_ambi_group_depth_128_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_group_depth_128"
)

if(APPLE)
  set_target_properties(s3g_ambi_depth_16_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-depth-16"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Depth 16"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )

  set_target_properties(s3g_ambi_group_depth_64_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-group-depth-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Group Depth 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )

  set_target_properties(s3g_ambi_group_depth_128_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-group-depth-128"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Group Depth 128"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
