add_library(s3g_drum_floor_tom_clap MODULE
  s3g_drum_floor_tom_clap.cpp
)

target_link_libraries(s3g_drum_floor_tom_clap PRIVATE s3g_dsp)
target_include_directories(s3g_drum_floor_tom_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

set_target_properties(s3g_drum_floor_tom_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_drum_floor_tom"
)

if(S3G_DRUM_FAMILY_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_drum_floor_tom_clap
    "${CMAKE_CURRENT_SOURCE_DIR}/s3g_drum_floor_tom_vstgui.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_drum_floor_tom_clap PROPERTIES SUFFIX ".clap")
endif()

if(APPLE)
  set_source_files_properties(s3g_drum_floor_tom_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_drum_floor_tom_clap PRIVATE "-framework Cocoa")
  set_target_properties(s3g_drum_floor_tom_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.drum-floor-tom"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Drum Floor Tom 2"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )
endif()
