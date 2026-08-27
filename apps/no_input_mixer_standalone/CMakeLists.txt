if(NOT APPLE)
  return()
endif()

set(S3G_NO_INPUT_MIXER_APP_ICON
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/no_input_mixer.icns")
set_source_files_properties(${S3G_NO_INPUT_MIXER_APP_ICON} PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources"
)

add_executable(s3g_no_input_mixer_app MACOSX_BUNDLE
  s3g_no_input_mixer_app.mm
  s3g_no_input_mixer_standalone_engine.cpp
  embed_no_input_mixer.mm
  embed_nim_gesture.mm
  ${S3G_NO_INPUT_MIXER_APP_ICON}
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
  MACOSX_BUNDLE_ICON_FILE "no_input_mixer.icns"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

add_custom_command(TARGET s3g_no_input_mixer_app POST_BUILD
  COMMAND /usr/bin/codesign --force --deep --sign -
    "$<TARGET_BUNDLE_DIR:s3g_no_input_mixer_app>"
  COMMENT "Ad-hoc signing the No Input Mixer standalone bundle"
  VERBATIM
)

add_executable(s3g_no_input_mixer_standalone_smoke
  ../../tests/no_input_mixer_standalone_smoke.cpp
  s3g_no_input_mixer_standalone_engine.cpp
  embed_no_input_mixer.mm
  embed_nim_gesture.mm
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

add_executable(s3g_no_input_mixer_coreaudio_diagnostic
  s3g_no_input_mixer_coreaudio_diagnostic.cpp
  s3g_no_input_mixer_standalone_engine.cpp
  embed_no_input_mixer.mm
  embed_nim_gesture.mm
)

target_include_directories(s3g_no_input_mixer_coreaudio_diagnostic PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../standalone/host
  ${CMAKE_CURRENT_SOURCE_DIR}/../../standalone/audio
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/common
  ${S3G_CLAP_INCLUDE_DIR}
)

target_link_libraries(s3g_no_input_mixer_coreaudio_diagnostic PRIVATE
  s3g_dsp
  s3g_standalone_host
  "-framework Cocoa"
)

# This audit path is intentionally device-enumeration only. A live AUHAL run
# cannot be placed in an automatic build target because the diagnostic requires
# an explicit device UID, duration, and --confirm-live-audio acknowledgement.
add_custom_target(audit_no_input_mixer_coreaudio_devices
  COMMAND $<TARGET_FILE:s3g_no_input_mixer_coreaudio_diagnostic>
    --list-devices
  DEPENDS s3g_no_input_mixer_coreaudio_diagnostic
  COMMENT "Listing Core Audio outputs without opening a device"
)
