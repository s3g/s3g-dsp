add_library(s3g_tracker_core STATIC
  src/core/clap_editor_coordinator.cpp
  src/core/clap_midi_engine.cpp
  src/core/clap_playback_runtime.cpp
  src/core/preview_sequencer.cpp
  src/core/clap_command_controller.cpp
  src/core/clap_document_controller.cpp
  src/editor/editor_shell.cpp
  src/editor/editor_phrase.cpp
  src/editor/editor_assemble.cpp
  src/editor/editor_reshape.cpp
  src/editor/editor_geometry.cpp
  src/editor/editor_geometry_layout.cpp
  src/editor/editor_geometry_input.cpp
  src/editor/editor_geometry_paint.cpp
  src/editor/editor_geometry_canvas.cpp
  src/editor/editor_reference.cpp
  src/editor/editor_song.cpp
  src/editor/editor_warp.cpp
  src/editor/editor_grid.cpp
  src/editor/editor_grid_controller.cpp
  src/editor/editor_grid_keys.cpp
  src/editor/editor_grid_rows.cpp
  src/editor/editor_grid_painter.cpp
  src/audio/s3g_tracker_stereo_slice_sampler_node.cpp
  src/core/asset_pack.cpp
  src/core/command.cpp
  src/core/fx_catalog.cpp
  src/core/geometry_edit.cpp
  src/core/instrument_rack.cpp
  src/core/midi_step_recorder.cpp
  src/core/pattern_bank.cpp
  src/core/phrase_library.cpp
  src/core/pitch_map.cpp
  src/core/pattern_reshape.cpp
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

if(WIN32)
  target_sources(s3g_tracker_core PRIVATE src/core/atomic_file_store_windows.cpp)
else()
  target_sources(s3g_tracker_core PRIVATE src/core/atomic_file_store_posix.cpp)
endif()

add_executable(s3g_tracker_starter_pack_generator
  tools/generate_starter_pack.cpp)
target_link_libraries(s3g_tracker_starter_pack_generator
  PRIVATE s3g_tracker_core)
add_executable(s3g_tracker_high_energy_pack_generator
  tools/generate_high_energy_pack.cpp)
target_link_libraries(s3g_tracker_high_energy_pack_generator
  PRIVATE s3g_tracker_core)
set_target_properties(s3g_tracker_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

target_include_directories(s3g_tracker_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/plugins/common
)
target_compile_features(s3g_tracker_core PUBLIC cxx_std_17)
target_link_libraries(s3g_tracker_core PUBLIC s3g_dsp)
if(MSVC)
  # Model/status strings and source fixtures are UTF-8 on both platforms.
  target_compile_options(s3g_tracker_core PUBLIC /utf-8)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_tracker_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BUILD_TESTING)
  # Native Windows runners can validate the shared layer without building a
  # CLAP GUI or fetching VSTGUI. This is not a Windows plugin target.
  add_custom_target(s3g_tracker_portable_core_tests)
  function(s3g_add_tracker_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE s3g_tracker_core)
    if(MSVC)
      # These tests keep several complete project/state fixtures on the stack.
      # The default 1 MiB MSVC executable stack overflows before assertions run.
      target_link_options(${target} PRIVATE /STACK:8388608)
    endif()
    add_dependencies(s3g_tracker_portable_core_tests ${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "non_nim;tracker;core")
  endfunction()

  s3g_add_tracker_test(s3g_tracker_core_tests tests/sequencer_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_clap_controller_tests tests/clap_controller_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_clap_command_tests tests/clap_command_controller_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_file_store_tests tests/file_store_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_drawing_tests tests/editor_drawing_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_grid_tests tests/editor_grid_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_follow_tests tests/tracker_follow_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_warp_tests tests/editor_warp_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_song_tests tests/editor_song_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_reference_tests tests/editor_reference_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_geometry_tests tests/editor_geometry_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_editor_authoring_tests tests/editor_authoring_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_preview_sequencer_tests tests/preview_sequencer_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_clap_playback_runtime_tests tests/clap_playback_runtime_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_clap_midi_engine_tests tests/clap_midi_engine_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_clap_editor_coordinator_tests tests/clap_editor_coordinator_tests.cpp)
  # Header-only SDK input for adapter tests; core-only builds never fetch
  # VSTGUI, Cocoa or the full CLAP plugin dependency graph.
  set(S3G_TRACKER_CLAP_TEST_INCLUDE_DIR "" CACHE PATH
    "Optional CLAP include directory for portable Tracker adapter regressions")
  if(S3G_TRACKER_CLAP_TEST_INCLUDE_DIR)
    if(NOT EXISTS "${S3G_TRACKER_CLAP_TEST_INCLUDE_DIR}/clap/clap.h")
      message(FATAL_ERROR "S3G_TRACKER_CLAP_TEST_INCLUDE_DIR must contain clap/clap.h")
    endif()
    s3g_add_tracker_test(s3g_tracker_clap_midi_adapter_tests tests/clap_midi_adapter_tests.cpp)
    target_include_directories(s3g_tracker_clap_midi_adapter_tests PRIVATE
      ${S3G_TRACKER_CLAP_TEST_INCLUDE_DIR}
      ${CMAKE_SOURCE_DIR}/plugins/clap_tracker)
  endif()
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
  s3g_add_tracker_test(s3g_tracker_stereo_slice_sampler_tests tests/stereo_slice_sampler_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_project_codec_tests tests/project_codec_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_pattern_bank_tests tests/pattern_bank_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_phrase_library_tests tests/phrase_library_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_asset_pack_tests tests/asset_pack_tests.cpp)
  add_test(NAME s3g_tracker_starter_pack_generator_tests
    COMMAND s3g_tracker_starter_pack_generator
      ${CMAKE_CURRENT_BINARY_DIR}/s3g-drum-foundations-01.test.s3gpack)
  add_test(NAME s3g_tracker_high_energy_pack_generator_tests
    COMMAND s3g_tracker_high_energy_pack_generator
      ${CMAKE_CURRENT_BINARY_DIR}/s3g-high-energy-club-01.test.s3gpack)
  set_tests_properties(
    s3g_tracker_starter_pack_generator_tests
    s3g_tracker_high_energy_pack_generator_tests
    PROPERTIES LABELS "non_nim;tracker;core")
  add_dependencies(s3g_tracker_portable_core_tests
    s3g_tracker_starter_pack_generator s3g_tracker_high_energy_pack_generator)
  s3g_add_tracker_test(s3g_tracker_pitch_map_tests tests/pitch_map_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_pattern_reshape_tests tests/pattern_reshape_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_song_playback_planner_tests tests/song_playback_planner_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_song_timing_runtime_tests tests/song_timing_runtime_tests.cpp)
  s3g_add_tracker_test(s3g_tracker_visual_note_hit_mailbox_tests tests/visual_note_hit_mailbox_tests.cpp)

  add_executable(s3g_tracker_workspace_layout_tests tests/workspace_layout_tests.cpp)
  target_include_directories(s3g_tracker_workspace_layout_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/macos)
  add_test(NAME s3g_tracker_workspace_layout_tests
    COMMAND s3g_tracker_workspace_layout_tests)
  set_tests_properties(s3g_tracker_workspace_layout_tests PROPERTIES
    LABELS "non_nim;tracker;gui")

  add_executable(s3g_tracker_grid_selection_tests tests/grid_selection_tests.cpp)
  target_include_directories(s3g_tracker_grid_selection_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
      src/app/macos/s3g_tracker_phrase_view.mm
      src/app/macos/s3g_tracker_assemble_view.mm
      src/app/macos/s3g_tracker_controls.mm
      src/app/macos/s3g_tracker_reshape_window.mm
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
