add_library(s3g_tracker_clap MODULE
  s3g_tracker_clap.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_help_window.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reshape_window.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_window.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_workspace.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_phrase_view.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_assemble_view.mm
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_song_window.mm
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Medium.ttf
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-SemiBold.ttf
  ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/OFL.txt
)

target_link_libraries(s3g_tracker_clap PRIVATE
  s3g_dsp s3g_tracker_core)
target_include_directories(s3g_tracker_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_SOURCE_DIR}/tracker/include
  ${CMAKE_SOURCE_DIR}/tracker/src/app/macos
)

set_target_properties(s3g_tracker_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_tracker"
)

if(APPLE)
  set_source_files_properties(s3g_tracker_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(s3g_tracker_clap PRIVATE -fobjc-arc)
  target_link_libraries(s3g_tracker_clap PRIVATE
    "-framework Cocoa"
    "-framework CoreText"
    "-framework CoreFoundation"
    "-framework AudioToolbox"
    "-framework UniformTypeIdentifiers")
  set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-Medium.ttf
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/IBMPlexMono-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/tracker/resources/fonts/OFL.txt
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Fonts")
  set_target_properties(s3g_tracker_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.tracker"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Tracker"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.4.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4.0"
  )

  if(S3G_ENABLE_TRACKER_VSTGUI_PILOT_ON_MACOS)
    s3g_enable_vstgui_gui(s3g_tracker_clap
      "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
      S3G_TRACKER_VSTGUI_PILOT)
    target_sources(s3g_tracker_clap PRIVATE
      "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm")
  endif()
endif()
