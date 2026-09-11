add_library(s3g_ambi_group_rotate_64_clap MODULE
  s3g_ambi_group_rotate_clap.cpp
)

add_library(s3g_ambi_group_rotate_128_clap MODULE
  s3g_ambi_group_rotate_clap.cpp
)

target_link_libraries(s3g_ambi_group_rotate_64_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_group_rotate_64_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

target_link_libraries(s3g_ambi_group_rotate_128_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_group_rotate_128_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_definitions(s3g_ambi_group_rotate_128_clap PRIVATE S3G_AMBI_GROUP_ROTATE_128=1)

if(APPLE)
  set_source_files_properties(s3g_ambi_group_rotate_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_group_rotate_64_clap PRIVATE "-framework Cocoa")
  target_link_libraries(s3g_ambi_group_rotate_128_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_group_rotate_64_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_group_rotate_64"
)

set_target_properties(s3g_ambi_group_rotate_128_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_group_rotate_128"
)

if(APPLE)
  set_target_properties(s3g_ambi_group_rotate_64_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-group-rotate-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Transform Grp Rot 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )

  set_target_properties(s3g_ambi_group_rotate_128_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-group-rotate-128"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Transform Grp Rot 128"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

foreach(s3g_ambi_target IN ITEMS s3g_ambi_group_rotate_64_clap s3g_ambi_group_rotate_128_clap)
  if(S3G_AMBI_BUS_VSTGUI_AVAILABLE)
    s3g_enable_vstgui_gui(${s3g_ambi_target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_ambi_rotate_canvas.inc"
      S3G_ENABLE_VSTGUI_CANVAS_GUI)
  endif()
  if(WIN32)
    set_target_properties(${s3g_ambi_target} PROPERTIES SUFFIX ".clap")
  endif()
endforeach()
