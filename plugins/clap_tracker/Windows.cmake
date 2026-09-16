if(NOT S3G_VSTGUI_AVAILABLE)
  message(FATAL_ERROR "Windows Tracker requires S3G_ENABLE_PORTABLE_CLAP_GUI")
endif()
add_library(s3g_tracker_clap MODULE s3g_tracker_windows_clap.cpp)
target_link_libraries(s3g_tracker_clap PRIVATE s3g_tracker_core user32 comctl32)
target_include_directories(s3g_tracker_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/tracker/src/editor)
s3g_enable_vstgui_gui(s3g_tracker_clap
  "${CMAKE_CURRENT_SOURCE_DIR}/s3g_tracker_windows_editor.cpp"
  S3G_TRACKER_WINDOWS_VSTGUI)
target_sources(s3g_tracker_clap PRIVATE
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_vstgui_drawing.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_main_menus.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_tool_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_song_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_warp_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_geometry_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_authoring_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_phrase_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_assemble_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reshape_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_reference_page.cpp
  ${CMAKE_SOURCE_DIR}/tracker/src/editor/s3g_tracker_shell_view.cpp)
target_compile_definitions(s3g_tracker_clap PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(s3g_tracker_clap PRIVATE /utf-8)
endif()
set_target_properties(s3g_tracker_clap PROPERTIES
  PREFIX "" OUTPUT_NAME "s3g_tracker" SUFFIX ".clap")
foreach(font IBMPlexMono-Regular.ttf IBMPlexMono-Medium.ttf IBMPlexMono-SemiBold.ttf OFL.txt)
  add_custom_command(TARGET s3g_tracker_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tracker/resources/fonts/${font}"
      "$<TARGET_FILE_DIR:s3g_tracker_clap>/Resources/Fonts/${font}"
    VERBATIM)
endforeach()
add_custom_command(TARGET s3g_tracker_clap POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:s3g_tracker_clap>/LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/tracker/LICENSE" "$<TARGET_FILE_DIR:s3g_tracker_clap>/TRACKER-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:s3g_tracker_clap>/THIRD_PARTY_NOTICES.md"
  VERBATIM)

if(BUILD_TESTING)
  add_executable(s3g_tracker_windows_clipboard
    ${CMAKE_SOURCE_DIR}/tests/tracker_windows_clipboard.cpp)
  target_include_directories(s3g_tracker_windows_clipboard PRIVATE ${vstgui_SOURCE_DIR})
  target_compile_features(s3g_tracker_windows_clipboard PRIVATE cxx_std_17)
  target_link_libraries(s3g_tracker_windows_clipboard PRIVATE vstgui)
  add_test(NAME s3g_tracker_windows_clipboard COMMAND s3g_tracker_windows_clipboard)
  set_tests_properties(s3g_tracker_windows_clipboard PROPERTIES
    LABELS "non_nim;tracker;windows;clipboard" TIMEOUT 30)
  add_executable(s3g_tracker_windows_clap_smoke
    ${CMAKE_SOURCE_DIR}/tests/tracker_windows_clap_smoke.cpp)
  target_include_directories(s3g_tracker_windows_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(s3g_tracker_windows_clap_smoke PRIVATE
    s3g_tracker_core user32 shell32 comctl32)
  if(MSVC)
    # Multiple complete project fixtures exceed the default 1 MiB test stack.
    target_link_options(s3g_tracker_windows_clap_smoke PRIVATE /STACK:8388608)
  endif()
  add_dependencies(s3g_tracker_windows_clap_smoke s3g_tracker_clap s3g_tracker_windows_clipboard)
  add_test(NAME s3g_tracker_windows_clap_smoke
    COMMAND s3g_tracker_windows_clap_smoke $<TARGET_FILE:s3g_tracker_clap>)
  set_tests_properties(s3g_tracker_windows_clap_smoke PROPERTIES
    LABELS "non_nim;tracker;windows;gui" TIMEOUT 120)

  # Test actual factory code with the same 1 MiB stack budget as native REAPER.
  # The GUI checker above needs a larger stack for its own project fixtures.
  add_executable(s3g_tracker_windows_factory_stack
    ${CMAKE_SOURCE_DIR}/tests/tracker_windows_factory_stack.cpp)
  target_include_directories(s3g_tracker_windows_factory_stack PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(s3g_tracker_windows_factory_stack PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    target_link_options(s3g_tracker_windows_factory_stack PRIVATE /STACK:1048576)
  elseif(MINGW)
    target_link_options(s3g_tracker_windows_factory_stack PRIVATE -municode -Wl,--stack,1048576)
  endif()
  add_dependencies(s3g_tracker_windows_factory_stack s3g_tracker_clap)
  add_dependencies(s3g_tracker_windows_clap_smoke s3g_tracker_windows_factory_stack)
  add_test(NAME s3g_tracker_windows_factory_stack
    COMMAND s3g_tracker_windows_factory_stack $<TARGET_FILE:s3g_tracker_clap>)
  set_tests_properties(s3g_tracker_windows_factory_stack PROPERTIES
    LABELS "non_nim;tracker;windows" TIMEOUT 30)

endif()
