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
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Effect Spectral Spray 2"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
if(S3G_MEMORY_EFFECTS_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_spectral_spray_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_memory_effect_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_spectral_spray_clap PROPERTIES SUFFIX ".clap")
endif()
