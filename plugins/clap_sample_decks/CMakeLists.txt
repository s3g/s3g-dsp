add_library(s3g_sample_decks_clap MODULE s3g_sample_decks_clap.cpp)
find_package(Threads REQUIRED)
target_link_libraries(s3g_sample_decks_clap PRIVATE s3g_dsp Threads::Threads)
target_include_directories(s3g_sample_decks_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_features(s3g_sample_decks_clap PRIVATE cxx_std_17)
if(S3G_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_sample_decks_clap
    "${CMAKE_CURRENT_SOURCE_DIR}/s3g_sample_decks_vstgui.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  s3g_enable_windows_sample_decode(s3g_sample_decks_clap)
  if(APPLE)
    target_sources(s3g_sample_decks_clap PRIVATE ../common/s3g_vstgui_readable_text_edit.mm)
    # The shared scoped AppKit field editor uses explicit retain/release.
    set_source_files_properties(../common/s3g_vstgui_readable_text_edit.mm
      PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_sample_decks_clap PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(s3g_sample_decks_clap PROPERTIES PREFIX "" OUTPUT_NAME "s3g_sample_decks")
if(APPLE)
  set_source_files_properties(s3g_sample_decks_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(s3g_sample_decks_clap PRIVATE -fobjc-arc)
  target_link_libraries(s3g_sample_decks_clap PRIVATE "-framework Cocoa" "-framework AVFoundation" "-framework Accelerate")
  set_target_properties(s3g_sample_decks_clap PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.sample-decks"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sample Decks 32"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0" MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")
elseif(WIN32)
  set_target_properties(s3g_sample_decks_clap PROPERTIES SUFFIX ".clap")
endif()

if(BUILD_TESTING AND APPLE)
  add_executable(s3g_sample_decks_smoke ../../tests/sample_decks_smoke.mm)
  target_include_directories(s3g_sample_decks_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_options(s3g_sample_decks_smoke PRIVATE -fobjc-arc)
  target_link_libraries(s3g_sample_decks_smoke PRIVATE s3g_dsp Threads::Threads
    "-framework Cocoa" "-framework AVFoundation" "-framework Accelerate")
  add_test(NAME s3g_sample_decks_smoke COMMAND s3g_sample_decks_smoke $<TARGET_FILE:s3g_sample_decks_clap>)
  set_tests_properties(s3g_sample_decks_smoke PROPERTIES LABELS "non_nim;sample;controller;clap;smoke" TIMEOUT 120)
  set_tests_properties(s3g_sample_decks_smoke PROPERTIES FIXTURES_SETUP sample_decks_gui_source
    ENVIRONMENT "S3G_DECKS_TEST_STATE=${CMAKE_CURRENT_BINARY_DIR}/sample-decks-gui-state.bin")
  if(TARGET s3g_encoder_family_gui_smoke)
    add_test(NAME s3g_sample_decks_gui_smoke COMMAND s3g_encoder_family_gui_smoke
      $<TARGET_FILE:s3g_sample_decks_clap> org.s3g.s3g-dsp.sample-decks 1440 810 "s3g Sample Decks 32" proportional)
    set_tests_properties(s3g_sample_decks_gui_smoke PROPERTIES LABELS "non_nim;sample;controller;clap;gui" TIMEOUT 120)
    set_tests_properties(s3g_sample_decks_gui_smoke PROPERTIES FIXTURES_REQUIRED sample_decks_gui_source
      ENVIRONMENT "S3G_DECKS_GUI_STATE=${CMAKE_CURRENT_BINARY_DIR}/sample-decks-gui-state.bin")
  endif()
endif()
