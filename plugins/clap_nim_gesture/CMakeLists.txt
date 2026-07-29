add_library(s3g_nim_gesture_clap MODULE
  s3g_nim_gesture_clap.cpp
)

target_link_libraries(s3g_nim_gesture_clap PRIVATE s3g_dsp)
target_include_directories(s3g_nim_gesture_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

set_target_properties(s3g_nim_gesture_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_nim_gesture"
)

if(APPLE)
  set_source_files_properties(s3g_nim_gesture_clap.cpp PROPERTIES
    LANGUAGE OBJCXX
  )
  target_link_libraries(s3g_nim_gesture_clap PRIVATE "-framework Cocoa")
  set_target_properties(s3g_nim_gesture_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.nim-gesture"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Utility NIM Gesture"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )
endif()
