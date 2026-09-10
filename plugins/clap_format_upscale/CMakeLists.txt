add_library(s3g_format_upscale_clap MODULE
  s3g_format_upscale_clap.cpp)

target_link_libraries(s3g_format_upscale_clap PRIVATE s3g_dsp)
if(S3G_MONITORING_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_format_upscale_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_upmix_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
  target_sources(s3g_format_upscale_clap PRIVATE
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_vstgui_auxiliary_window.cpp")
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/plugins/common/s3g_vstgui_auxiliary_window.cpp"
      PROPERTIES LANGUAGE OBJCXX)
  endif()
endif()
if(WIN32)
  set_target_properties(s3g_format_upscale_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_format_upscale_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_format_upscale_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_format_upscale_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_format_upscale_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_format_upscale")

if(APPLE)
  set_target_properties(s3g_format_upscale_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.format-upscale-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Matrix Upmix 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()
