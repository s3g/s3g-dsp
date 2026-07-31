if(NOT APPLE)
  return()
endif()

add_executable(s3g_no_input_mixer_app MACOSX_BUNDLE
  s3g_no_input_mixer_app.mm
  s3g_no_input_mixer_standalone_engine.cpp
  embed_no_input_mixer.mm
  embed_nim_gesture.mm
  embed_mc_to_stereo_autogain.mm
  embed_mc_to_quad_autogain.mm
)

target_include_directories(s3g_no_input_mixer_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../standalone/host
  ${CMAKE_CURRENT_SOURCE_DIR}/../../standalone/audio
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/common
  ${S3G_CLAP_INCLUDE_DIR}
)

target_link_libraries(s3g_no_input_mixer_app PRIVATE
  s3g_dsp
  s3g_standalone_host
  "-framework AudioUnit"
  "-framework Cocoa"
  "-framework CoreAudio"
  "-framework CoreMIDI"
)

set_target_properties(s3g_no_input_mixer_app PROPERTIES
  OUTPUT_NAME "s3g No Input Mixer"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.no-input-mixer-standalone"
  MACOSX_BUNDLE_BUNDLE_NAME "s3g No Input Mixer"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

add_executable(s3g_no_input_mixer_standalone_smoke
  ../../tests/no_input_mixer_standalone_smoke.cpp
  s3g_no_input_mixer_standalone_engine.cpp
  embed_no_input_mixer.mm
  embed_nim_gesture.mm
  embed_mc_to_stereo_autogain.mm
  embed_mc_to_quad_autogain.mm
)

target_include_directories(s3g_no_input_mixer_standalone_smoke PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../standalone/host
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/common
  ${S3G_CLAP_INCLUDE_DIR}
)

target_link_libraries(s3g_no_input_mixer_standalone_smoke PRIVATE
  s3g_dsp
  s3g_standalone_host
  "-framework Cocoa"
)

add_custom_target(audit_no_input_mixer_standalone
  COMMAND $<TARGET_FILE:s3g_no_input_mixer_standalone_smoke>
  DEPENDS s3g_no_input_mixer_standalone_smoke
  COMMENT "Checking embedded No Input Mixer standalone routing and state"
)
