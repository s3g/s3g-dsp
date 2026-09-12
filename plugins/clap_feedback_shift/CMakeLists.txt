add_library(s3g_feedback_shift_clap MODULE
  s3g_feedback_shift_clap.cpp
)

target_link_libraries(s3g_feedback_shift_clap PRIVATE s3g_dsp)
if(S3G_FEEDBACK_PROCESSORS_VSTGUI_AVAILABLE)
 s3g_enable_vstgui_gui(s3g_feedback_shift_clap
  "${PROJECT_SOURCE_DIR}/plugins/common/s3g_feedback_processor_shift_canvas.inc"
  S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
 set_target_properties(s3g_feedback_shift_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_feedback_shift_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

set_target_properties(s3g_feedback_shift_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_feedback_shift"
)

if(APPLE)
  set_source_files_properties(s3g_feedback_shift_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_feedback_shift_clap PRIVATE "-framework Cocoa")
  set_target_properties(s3g_feedback_shift_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.feedback-shift"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Processor Feedback Shift 8"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.18.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.18.1"
  )
endif()
