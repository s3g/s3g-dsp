add_library(s3g_utility_neon_midi_clap MODULE s3g_utility_neon_midi_clap.cpp)
target_link_libraries(s3g_utility_neon_midi_clap PRIVATE s3g_dsp)
target_include_directories(s3g_utility_neon_midi_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_features(s3g_utility_neon_midi_clap PRIVATE cxx_std_17)
if(S3G_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_utility_neon_midi_clap
    "${CMAKE_CURRENT_SOURCE_DIR}/s3g_utility_neon_midi_vstgui.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_utility_neon_midi_clap PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
set_target_properties(s3g_utility_neon_midi_clap PROPERTIES
  PREFIX "" OUTPUT_NAME "s3g_utility_neon_midi")
if(APPLE)
  target_link_libraries(s3g_utility_neon_midi_clap PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
  set_target_properties(s3g_utility_neon_midi_clap PROPERTIES
    BUNDLE TRUE BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.utility-neon-midi"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Utility Neon MIDI"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.2.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.1")
elseif(WIN32)
  set_target_properties(s3g_utility_neon_midi_clap PROPERTIES SUFFIX ".clap")
endif()

if(BUILD_TESTING AND UNIX)
  add_executable(s3g_utility_neon_midi_clap_smoke
    ${PROJECT_SOURCE_DIR}/tests/utility_neon_midi_clap_smoke.cpp)
  target_include_directories(s3g_utility_neon_midi_clap_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_utility_neon_midi_clap_smoke PRIVATE s3g_dsp ${CMAKE_DL_LIBS})
  if(TARGET s3g_tracker_core)
    target_link_libraries(s3g_utility_neon_midi_clap_smoke PRIVATE s3g_tracker_core)
    target_compile_definitions(s3g_utility_neon_midi_clap_smoke PRIVATE S3G_TEST_TRACKER_RECORDER=1)
  endif()
  add_test(NAME s3g_utility_neon_midi_clap_smoke
    COMMAND s3g_utility_neon_midi_clap_smoke $<TARGET_FILE:s3g_utility_neon_midi_clap>)
  set_tests_properties(s3g_utility_neon_midi_clap_smoke PROPERTIES
    LABELS "non_nim;utility;controller;midi;clap;smoke" TIMEOUT 120)
  if(TARGET s3g_tracker_clap AND TARGET s3g_sample_neon_clap)
    add_test(NAME s3g_neon_tracker_chain_smoke
      COMMAND s3g_utility_neon_midi_clap_smoke $<TARGET_FILE:s3g_utility_neon_midi_clap>
        $<TARGET_FILE:s3g_tracker_clap> $<TARGET_FILE:s3g_sample_neon_clap>)
    set_tests_properties(s3g_neon_tracker_chain_smoke PROPERTIES
      LABELS "non_nim;utility;controller;midi;clap;integration"
      ENVIRONMENT "S3G_SAMPLE_NEON_DISABLE_DIRECT_MIDI=1" TIMEOUT 120)
  endif()
  if(APPLE AND S3G_VSTGUI_AVAILABLE)
    add_test(NAME s3g_utility_neon_midi_gui_smoke
      COMMAND s3g_encoder_family_gui_smoke $<TARGET_FILE:s3g_utility_neon_midi_clap>
        org.s3g.s3g-dsp.utility-neon-midi 620 444 "s3g Utility Neon MIDI" proportional)
    set_tests_properties(s3g_utility_neon_midi_gui_smoke PROPERTIES
      LABELS "non_nim;utility;controller;midi;clap;gui" TIMEOUT 120)
  endif()
endif()
