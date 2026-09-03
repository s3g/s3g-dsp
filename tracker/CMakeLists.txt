add_library(s3g_tracker_core STATIC
  src/audio/s3g_tracker_stereo_slice_sampler_node.cpp
  src/core/command.cpp
  src/core/fx_catalog.cpp
  src/core/geometry_edit.cpp
  src/core/instrument_rack.cpp
  src/core/midi_step_recorder.cpp
  src/core/pattern_bank.cpp
  src/core/atomic_project_store.cpp
  src/core/project_codec.cpp
  src/core/project_history.cpp
  src/core/runtime_pattern_plan.cpp
  src/core/sequencer.cpp
  src/core/song_playback_planner.cpp
  src/core/timing_action_resolver.cpp
  src/core/timing_playback_scheduler.cpp
  src/core/timing_warp.cpp
)
set_target_properties(s3g_tracker_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(s3g_tracker_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(s3g_tracker_core PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_tracker_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BUILD_TESTING)
  function(s3g_add_tracker_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE s3g_tracker_core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "non_nim;tracker;core")
  endfunction()

  s3g_add_tracker_test(s3g_tracker_core_tests tests/sequencer_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_command_tests tests/command_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_geometry_edit_tests tests/geometry_edit_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_instrument_rack_tests tests/instrument_rack_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_midi_step_recorder_tests tests/midi_step_recorder_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_runtime_pattern_plan_tests tests/runtime_pattern_plan_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_timing_warp_tests tests/timing_warp_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_fixed_timed_event_queue_tests tests/fixed_timed_event_queue_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_scheduled_event_timeline_tests tests/scheduled_event_timeline_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_timing_event_expansion_tests tests/timing_event_expansion_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_timing_action_resolver_tests tests/timing_action_resolver_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_timing_playback_scheduler_tests tests/timing_playback_scheduler_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_audio_contract_tests tests/audio_contract_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_stereo_slice_sampler_tests tests/stereo_slice_sampler_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_project_codec_tests tests/project_codec_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_pattern_bank_tests tests/pattern_bank_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_song_playback_planner_tests tests/song_playback_planner_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_song_timing_runtime_tests tests/song_timing_runtime_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_visual_note_hit_mailbox_tests tests/visual_note_hit_mailbox_tests.cpp)

  add_executable(s3g_tracker_workspace_layout_tests tests/workspace_layout_tests.cpp)
  target_include_directories(s3g_tracker_workspace_layout_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/macos)
  add_test(NAME s3g_tracker_workspace_layout_tests
    COMMAND s3g_tracker_workspace_layout_tests)
  set_tests_properties(s3g_tracker_workspace_layout_tests PROPERTIES
    LABELS "non_nim;tracker;gui")

  add_executable(s3g_tracker_grid_selection_tests tests/grid_selection_tests.cpp)
  target_include_directories(s3g_tracker_grid_selection_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/macos)
  add_test(NAME s3g_tracker_grid_selection_tests
    COMMAND s3g_tracker_grid_selection_tests)
  set_tests_properties(s3g_tracker_grid_selection_tests PROPERTIES
    LABELS "non_nim;tracker;gui")

  if(APPLE)
    add_executable(s3g_tracker_song_window_roundtrip_tests
      tests/song_window_roundtrip_tests.mm
      src/app/macos/s3g_song_window.mm
      src/app/macos/s3g_tracker_controls.mm)
    target_include_directories(s3g_tracker_song_window_roundtrip_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/app/macos
      ${CMAKE_SOURCE_DIR}/plugins/common
      ${CMAKE_SOURCE_DIR}/dsp)
    target_link_libraries(s3g_tracker_song_window_roundtrip_tests PRIVATE
      s3g_tracker_core "-framework Cocoa" "-framework CoreText"
      "-framework CoreFoundation")
    target_compile_options(s3g_tracker_song_window_roundtrip_tests PRIVATE
      -fobjc-arc -Wall -Wextra -Wpedantic)
    add_test(NAME s3g_tracker_song_window_roundtrip_tests
      COMMAND s3g_tracker_song_window_roundtrip_tests)
    set_tests_properties(s3g_tracker_song_window_roundtrip_tests PROPERTIES
      LABELS "non_nim;tracker;gui")

    add_executable(s3g_tracker_workspace_layout_appkit_tests
      tests/workspace_layout_appkit_tests.mm
      src/app/macos/s3g_tracker_workspace.mm
      src/app/macos/s3g_tracker_controls.mm
      src/app/macos/s3g_tracker_warp_window.mm)
    target_include_directories(s3g_tracker_workspace_layout_appkit_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/app/macos
      ${CMAKE_SOURCE_DIR}/plugins/common
      ${CMAKE_SOURCE_DIR}/dsp)
    target_link_libraries(s3g_tracker_workspace_layout_appkit_tests PRIVATE
      s3g_tracker_core "-framework Cocoa" "-framework CoreText"
      "-framework CoreFoundation")
    target_compile_options(s3g_tracker_workspace_layout_appkit_tests PRIVATE
      -fobjc-arc -Wall -Wextra -Wpedantic)
    add_test(NAME s3g_tracker_workspace_layout_appkit_tests
      COMMAND s3g_tracker_workspace_layout_appkit_tests)
    set_tests_properties(s3g_tracker_workspace_layout_appkit_tests PROPERTIES
      LABELS "non_nim;tracker;gui")
  endif()
endif()
