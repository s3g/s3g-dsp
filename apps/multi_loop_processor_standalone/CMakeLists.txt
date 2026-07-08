if(NOT APPLE)
  return()
endif()

add_executable(s3g_multi_loop_processor_app MACOSX_BUNDLE
  s3g_multi_loop_processor_app.mm
)

target_link_libraries(s3g_multi_loop_processor_app PRIVATE
  s3g_dsp
  "-framework AVFoundation"
  "-framework AudioUnit"
  "-framework Cocoa"
  "-framework CoreAudio"
  "-framework CoreMIDI"
)

target_include_directories(s3g_multi_loop_processor_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/common
)

set_target_properties(s3g_multi_loop_processor_app PROPERTIES
  OUTPUT_NAME "s3g Multi Loop Processor"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.multi-loop-processor-standalone"
  MACOSX_BUNDLE_BUNDLE_NAME "s3g Multi Loop Processor"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
