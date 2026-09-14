if(WIN32)
  include(${CMAKE_CURRENT_SOURCE_DIR}/Windows.cmake)
  return()
endif()

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
    if(S3G_ENABLE_TRACKER_PORTABLE_SHELL_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_SHELL=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_shell_view.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_shell_host.mm")
      if(BUILD_TESTING)
        add_executable(s3g_tracker_portable_shell_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_shell_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_shell_view.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_shell_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_shell_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_shell_tests PRIVATE
          "${S3G_CLAP_INCLUDE_DIR}"
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_shell_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_shell_tests PRIVATE
          S3G_TRACKER_TEST_FONT_DIR="${CMAKE_SOURCE_DIR}/tracker/resources/fonts"
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_shell_tests COMMAND s3g_tracker_portable_shell_tests)
        set_tests_properties(s3g_tracker_portable_shell_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    if(S3G_ENABLE_TRACKER_PORTABLE_AUTHORING_PAGES_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_AUTHORING_PAGES=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_authoring_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_phrase_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_assemble_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reshape_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_authoring_page_host.mm")
      if(BUILD_TESTING)
        # Compile the original Cocoa pages independently, without opt-in macros.
        add_executable(s3g_tracker_portable_authoring_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_authoring_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_phrase_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_assemble_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reshape_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_authoring_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_authoring_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_phrase_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_assemble_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reshape_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_authoring_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_authoring_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_authoring_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_authoring_tests PRIVATE
          S3G_TRACKER_TEST_FONT_DIR="${CMAKE_SOURCE_DIR}/tracker/resources/fonts"
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_authoring_tests COMMAND s3g_tracker_portable_authoring_tests)
        set_tests_properties(s3g_tracker_portable_authoring_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    s3g_enable_vstgui_gui(s3g_tracker_clap
      "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
      S3G_TRACKER_VSTGUI_PILOT)
    target_sources(s3g_tracker_clap PRIVATE
      "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reaper_keyboard.mm"
      "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm")
    if(S3G_ENABLE_TRACKER_PORTABLE_GEOMETRY_PAGES_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_GEOMETRY_PAGES=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_geometry_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_geometry_page_host.mm")

      if(BUILD_TESTING)
        # Native Cocoa remains independent in this reference comparison.
        add_executable(s3g_tracker_portable_geometry_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_geometry_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_workspace.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_phrase_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_assemble_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reshape_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_geometry_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_geometry_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_geometry_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_geometry_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_geometry_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_geometry_tests PRIVATE
          S3G_TRACKER_TEST_FONT_DIR="${CMAKE_SOURCE_DIR}/tracker/resources/fonts"
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_geometry_tests COMMAND s3g_tracker_portable_geometry_tests)
        set_tests_properties(s3g_tracker_portable_geometry_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    if(S3G_ENABLE_TRACKER_PORTABLE_REFERENCE_PAGES_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_REFERENCE_PAGES=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reference_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reference_page_host.mm")
      if(BUILD_TESTING)
        # Independent, unconverted Cocoa controllers alongside the new pages.
        add_executable(s3g_tracker_portable_reference_tests
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reaper_keyboard.mm"
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_reference_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_workspace.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_phrase_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_assemble_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reshape_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_help_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reference_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reference_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_reference_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_reference_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_reference_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_reference_tests PRIVATE
          S3G_TRACKER_TEST_FONT_DIR="${CMAKE_SOURCE_DIR}/tracker/resources/fonts"
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_reference_tests COMMAND s3g_tracker_portable_reference_tests)
        set_tests_properties(s3g_tracker_portable_reference_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    if(S3G_ENABLE_TRACKER_PORTABLE_SONG_PAGE_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_SONG_PAGE=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_song_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_song_page_host.mm")
      if(BUILD_TESTING)
        # The Cocoa controller deliberately stays native in this comparison.
        add_executable(s3g_tracker_portable_song_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_song_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_song_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_song_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_song_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_song_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_song_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_song_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_song_tests PRIVATE
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_song_tests COMMAND s3g_tracker_portable_song_tests)
        set_tests_properties(s3g_tracker_portable_song_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    if(S3G_ENABLE_TRACKER_PORTABLE_WARP_PAGE_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_WARP_PAGE=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_warp_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_page_host.mm")
      if(BUILD_TESTING)
        # Keep the original controller compiled WITHOUT the port define for
        # independent Cocoa/VSTGUI behavioral and visual parity captures.
        add_executable(s3g_tracker_portable_warp_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_warp_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_warp_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_warp_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_warp_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_warp_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_warp_tests PRIVATE
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_warp_tests COMMAND s3g_tracker_portable_warp_tests)
        set_tests_properties(s3g_tracker_portable_warp_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
    if(S3G_ENABLE_TRACKER_PORTABLE_MAIN_PAGE_ON_MACOS)
      target_compile_definitions(s3g_tracker_clap PRIVATE S3G_TRACKER_PORTABLE_MAIN_PAGE=1)
      target_sources(s3g_tracker_clap PRIVATE
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_page.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_menus.cpp"
        "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_main_page_host.mm")
      if(BUILD_TESTING)
        # The comparison target deliberately compiles the unconverted Cocoa
        # workspace without either pilot define beside the new CFrame page.
        add_executable(s3g_tracker_portable_main_tests
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_main_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/tests/portable_main_parity_tests.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_workspace.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_phrase_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_assemble_view.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_reshape_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_warp_window.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_main_page_host.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_vstgui_pilot.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos/s3g_tracker_controls.mm"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_page.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_menus.cpp"
          "${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp"
          "${CMAKE_SOURCE_DIR}/plugins/common/s3g_vstgui_foundation.cpp")
        target_link_libraries(s3g_tracker_portable_main_tests PRIVATE s3g_tracker_core vstgui
          "-framework Cocoa" "-framework CoreText")
        target_include_directories(s3g_tracker_portable_main_tests PRIVATE
          "${CMAKE_SOURCE_DIR}/plugins/common" "${vstgui_SOURCE_DIR}"
          "${CMAKE_SOURCE_DIR}/tracker/src/app/macos")
        target_compile_options(s3g_tracker_portable_main_tests PRIVATE -fobjc-arc)
        target_compile_definitions(s3g_tracker_portable_main_tests PRIVATE
          VSTGUI_ENABLE_DEPRECATED_METHODS=0 VSTGUI_ENABLE_XML_PARSER=0 VSTGUI_OPENGL_SUPPORT=0)
        add_test(NAME s3g_tracker_portable_main_tests COMMAND s3g_tracker_portable_main_tests)
        set_tests_properties(s3g_tracker_portable_main_tests PROPERTIES
          LABELS "non_nim;tracker;gui;parity" TIMEOUT 120)
      endif()
    endif()
  endif()
endif()
