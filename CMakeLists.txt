cmake_minimum_required(VERSION 3.20)

# Collection and default bundle version. Individual targets and CLAP
# descriptors may keep component-scoped versions and advance independently.
project(s3g_dsp VERSION 0.7.0 LANGUAGES CXX)

include(CTest)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(s3g_dsp INTERFACE)
target_include_directories(s3g_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp)

# Compatibility toggles for the components introduced in 0.7. Fresh builds
# include the complete release inventory; the switches remain available for
# focused developer builds and older local configure commands.
option(S3G_BUILD_FUTURE_COMPONENTS
  "Build the complete set of components introduced in 0.7" ON)
option(S3G_BUILD_DRUM_OVERLOAD_PREVIEW
  "Build Drum Overload when the complete 0.7 component set is disabled" OFF)
option(S3G_BUILD_TRACKER_PREVIEW
  "Build the host-synchronized s3g Tracker MIDI CLAP" ON)
option(S3G_BUILD_BREAKBEAT_SLICER_PREVIEW
  "Build the s3g Breakbeat Slicer multisample CLAP" ON)

if(S3G_BUILD_TRACKER_PREVIEW)
  add_subdirectory(tracker)
endif()

include(FetchContent)
include(cmake/S3GRealtimeAllocProbe.cmake)

add_executable(s3g_dsp_smoke tests/dsp_smoke.cpp)
target_link_libraries(s3g_dsp_smoke PRIVATE s3g_dsp)
target_compile_definitions(s3g_dsp_smoke PRIVATE
  $<$<CONFIG:Release>:S3G_ENABLE_TEST_TIMING_ASSERTIONS=1>)
if(APPLE)
  target_link_libraries(s3g_dsp_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_crcltr_smoke tests/crcltr_smoke.cpp)
target_link_libraries(s3g_crcltr_smoke PRIVATE s3g_dsp)

add_executable(s3g_breakbeat_slicer_smoke
  tests/breakbeat_slicer_smoke.cpp)
target_link_libraries(s3g_breakbeat_slicer_smoke PRIVATE s3g_dsp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_breakbeat_slicer_smoke PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(s3g_feedback_shift_smoke
  tests/feedback_shift_smoke.cpp)
target_link_libraries(s3g_feedback_shift_smoke PRIVATE s3g_dsp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_feedback_shift_smoke PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(s3g_processor_fissure_smoke
  tests/processor_fissure_smoke.cpp)
target_link_libraries(s3g_processor_fissure_smoke PRIVATE s3g_dsp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_processor_fissure_smoke PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(s3g_crcltr_external_memory_smoke
  tests/crcltr_external_memory_smoke.cpp)
target_link_libraries(s3g_crcltr_external_memory_smoke PRIVATE s3g_dsp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_crcltr_external_memory_smoke PRIVATE
    -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(s3g_ambi_effect_displacement_smoke tests/ambi_effect_displacement_smoke.cpp)
target_link_libraries(s3g_ambi_effect_displacement_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_imprint_safety_smoke tests/ambi_imprint_safety_smoke.cpp)
target_link_libraries(s3g_ambi_imprint_safety_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_ambi_imprint_safety_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_ambi_ray_encoder_smoke tests/ambi_ray_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_ray_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_cartography_encoder_smoke
  tests/ambi_cartography_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_cartography_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_fractional_waveguide_network_smoke
  tests/fractional_waveguide_network_smoke.cpp)
target_link_libraries(s3g_fractional_waveguide_network_smoke PRIVATE s3g_dsp)

if(S3G_BUILD_FUTURE_COMPONENTS)
  add_executable(s3g_drum_primitives_smoke
    tests/drum_primitives_smoke.cpp)
  target_link_libraries(s3g_drum_primitives_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_character_smoke
    tests/drum_character_smoke.cpp)
  target_link_libraries(s3g_drum_character_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_overload_smoke
    tests/drum_overload_smoke.cpp)
  target_link_libraries(s3g_drum_overload_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_echo_smoke
    tests/drum_echo_smoke.cpp)
  target_link_libraries(s3g_drum_echo_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_mixer_smoke
    tests/drum_mixer_smoke.cpp)
  target_link_libraries(s3g_drum_mixer_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_kick_smoke
    tests/drum_kick_smoke.cpp)
  target_link_libraries(s3g_drum_kick_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_snare_smoke
    tests/drum_snare_smoke.cpp)
  target_link_libraries(s3g_drum_snare_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_tom_voice_smoke
    tests/drum_tom_voice_smoke.cpp)
  target_link_libraries(s3g_drum_tom_voice_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_floor_tom_smoke
    tests/drum_floor_tom_smoke.cpp)
  target_link_libraries(s3g_drum_floor_tom_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_concert_bass_smoke
    tests/drum_concert_bass_smoke.cpp)
  target_link_libraries(s3g_drum_concert_bass_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_toms_smoke
    tests/drum_toms_smoke.cpp)
  target_link_libraries(s3g_drum_toms_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_hi_hat_smoke
    tests/drum_hi_hat_smoke.cpp)
  target_link_libraries(s3g_drum_hi_hat_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_clap_smoke
    tests/drum_clap_smoke.cpp)
  target_link_libraries(s3g_drum_clap_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_cowbell_smoke
    tests/drum_cowbell_smoke.cpp)
  target_link_libraries(s3g_drum_cowbell_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_crash_smoke
    tests/drum_crash_smoke.cpp)
  target_link_libraries(s3g_drum_crash_smoke PRIVATE s3g_dsp)

  add_executable(s3g_drum_break_smoke
    tests/drum_break_smoke.cpp)
  target_link_libraries(s3g_drum_break_smoke PRIVATE s3g_dsp)

  add_executable(s3g_processor_errant_smoke
    tests/processor_errant_smoke.cpp)
  target_link_libraries(s3g_processor_errant_smoke PRIVATE s3g_dsp)

  add_executable(s3g_ambi_membrane_kick_smoke
    tests/ambi_membrane_kick_smoke.cpp)
  target_link_libraries(s3g_ambi_membrane_kick_smoke PRIVATE s3g_dsp)

  add_executable(s3g_ambi_acid_encoder_smoke
    tests/ambi_acid_encoder_smoke.cpp)
  target_link_libraries(s3g_ambi_acid_encoder_smoke PRIVATE s3g_dsp)

  add_executable(s3g_low_frequency_synth_smoke
    tests/low_frequency_synth_smoke.cpp)
  target_link_libraries(s3g_low_frequency_synth_smoke PRIVATE s3g_dsp)

  add_executable(s3g_processor_stack_smoke
    tests/processor_stack_smoke.cpp)
  target_link_libraries(s3g_processor_stack_smoke PRIVATE s3g_dsp)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_processor_stack_smoke PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()

  add_executable(s3g_ambi_acid_encoder_render
    tools/render_ambi_acid_encoder.cpp)
  target_link_libraries(s3g_ambi_acid_encoder_render PRIVATE s3g_dsp)
endif()

if(S3G_BUILD_DRUM_OVERLOAD_PREVIEW AND NOT S3G_BUILD_FUTURE_COMPONENTS)
  add_executable(s3g_drum_overload_smoke
    tests/drum_overload_smoke.cpp)
  target_link_libraries(s3g_drum_overload_smoke PRIVATE s3g_dsp)
endif()

add_executable(s3g_ambi_ray_bilocation_encoder_smoke tests/ambi_ray_bilocation_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_ray_bilocation_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_pulsar_encoder_smoke tests/pulsar_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_pulsar_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_neural_ecology_smoke tests/ambi_neural_ecology_smoke.cpp)
target_link_libraries(s3g_ambi_neural_ecology_smoke PRIVATE s3g_dsp)

add_executable(s3g_no_input_mixer_smoke tests/no_input_mixer_smoke.cpp)
target_link_libraries(s3g_no_input_mixer_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_insect_encoder_smoke tests/ambi_insect_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_insect_encoder_smoke PRIVATE s3g_dsp)
target_compile_definitions(s3g_ambi_insect_encoder_smoke PRIVATE
  $<$<CONFIG:Release>:S3G_ENABLE_TEST_TIMING_ASSERTIONS=1>)

add_executable(s3g_accelerometer_field_encoder_smoke tests/accelerometer_field_encoder_smoke.cpp)
target_link_libraries(s3g_accelerometer_field_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_accelerometer_field_encoder_render tools/render_accelerometer_field_encoder.cpp)
target_link_libraries(s3g_accelerometer_field_encoder_render PRIVATE s3g_dsp)

add_executable(s3g_ambi_field_listener_generators_smoke tests/ambi_field_listener_generators_smoke.cpp)
target_link_libraries(s3g_ambi_field_listener_generators_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_ambi_field_listener_generators_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_ambi_wrangler_listener_smoke tests/ambi_wrangler_listener_smoke.cpp)
target_link_libraries(s3g_ambi_wrangler_listener_smoke PRIVATE s3g_dsp)

add_executable(s3g_parameter_surface_smoke tests/parameter_surface_smoke.cpp)
target_link_libraries(s3g_parameter_surface_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_effect_dj_filter_smoke tests/ambi_effect_dj_filter_smoke.cpp)
target_link_libraries(s3g_ambi_effect_dj_filter_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_effect_delay_smoke tests/ambi_effect_delay_smoke.cpp)
target_link_libraries(s3g_ambi_effect_delay_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_effect_pitch_gain_smoke tests/ambi_effect_pitch_gain_smoke.cpp)
target_link_libraries(s3g_ambi_effect_pitch_gain_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_effect_resonance_print_smoke tests/ambi_effect_resonance_print_smoke.cpp)
target_link_libraries(s3g_ambi_effect_resonance_print_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_ambi_effect_resonance_print_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_ambi_effect_trace_smoke tests/ambi_effect_trace_smoke.cpp)
target_link_libraries(s3g_ambi_effect_trace_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_ambi_effect_trace_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_environmental_encoder_smoke tests/environmental_encoder_smoke.cpp)
target_link_libraries(s3g_environmental_encoder_smoke PRIVATE s3g_dsp)

if(S3G_BUILD_FUTURE_COMPONENTS)
  add_executable(s3g_ambi_horizon_encoder_smoke
    tests/ambi_horizon_encoder_smoke.cpp)
  target_link_libraries(s3g_ambi_horizon_encoder_smoke PRIVATE s3g_dsp)
endif()

add_executable(s3g_vox_builder_smoke tests/vox_builder_smoke.cpp)
target_link_libraries(s3g_vox_builder_smoke PRIVATE s3g_dsp)

add_executable(s3g_acapella_source_synth_smoke
  tests/acapella_source_synth_smoke.cpp)
target_link_libraries(s3g_acapella_source_synth_smoke PRIVATE s3g_dsp)

add_executable(s3g_acapella_resonator_regression_smoke
  tests/acapella_resonator_regression_smoke.cpp)
target_link_libraries(s3g_acapella_resonator_regression_smoke PRIVATE s3g_dsp)

add_executable(s3g_acapella_source_synth_render
  tools/render_acapella_source_synth.cpp)
target_link_libraries(s3g_acapella_source_synth_render PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_smoke tests/psd_raw_field_smoke.cpp)
target_link_libraries(s3g_psd_raw_field_smoke PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_parameter_audit tests/psd_raw_field_parameter_audit.cpp)
target_link_libraries(s3g_psd_raw_field_parameter_audit PRIVATE s3g_dsp)

add_executable(s3g_gui_layout_contract_smoke tests/gui_layout_contract_smoke.cpp)

add_executable(s3g_ambi_group_rotate_equivalence_smoke
  tests/ambi_group_rotate_equivalence_smoke.cpp)
target_link_libraries(s3g_ambi_group_rotate_equivalence_smoke PRIVATE s3g_dsp)

add_executable(s3g_environmental_svf_regression_smoke
  tests/environmental_svf_regression_smoke.cpp)
target_link_libraries(s3g_environmental_svf_regression_smoke PRIVATE s3g_dsp)

add_custom_target(audit_gui_style
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit-gui-style.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running advisory CLAP GUI style audit")

find_package(Python3 QUIET COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
  add_custom_target(audit_objc_class_names
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-objc-class-uniqueness.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-objc-class-uniqueness.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking process-global Objective-C class names across CLAP bundles"
    VERBATIM)

  add_custom_target(audit_clap_bundle_manifest
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-clap-bundle-manifest.py
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-clap-bundle-manifest.py
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-legacy-bundles.tsv
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating canonical CLAP bundle manifest"
    VERBATIM)
endif()

add_custom_target(audit_clap_bundle_installer
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/clap_bundle_installer_smoke.sh
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/clap_bundle_installer_smoke.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-clap-bundles.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-legacy-bundles.tsv
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Testing canonical CLAP install and legacy migration"
  VERBATIM)

if(APPLE)
  add_custom_target(audit_no_input_mixer_app_installer
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/no_input_mixer_app_installer_smoke.sh
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/no_input_mixer_app_installer_smoke.sh
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-no-input-mixer-app.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Testing No Input Mixer user-app install, upgrade, and rollback"
    VERBATIM)
endif()

if(BUILD_TESTING)
  set(S3G_NON_NIM_CORE_SMOKE_TARGETS
    s3g_dsp_smoke
    s3g_crcltr_smoke
    s3g_breakbeat_slicer_smoke
    s3g_feedback_shift_smoke
    s3g_processor_fissure_smoke
    s3g_crcltr_external_memory_smoke
    s3g_ambi_effect_displacement_smoke
    s3g_ambi_imprint_safety_smoke
    s3g_ambi_ray_encoder_smoke
    s3g_ambi_cartography_encoder_smoke
    s3g_fractional_waveguide_network_smoke
    s3g_ambi_ray_bilocation_encoder_smoke
    s3g_ambi_pulsar_encoder_smoke
    s3g_ambi_neural_ecology_smoke
    s3g_ambi_insect_encoder_smoke
    s3g_accelerometer_field_encoder_smoke
    s3g_ambi_field_listener_generators_smoke
    s3g_ambi_wrangler_listener_smoke
    s3g_parameter_surface_smoke
    s3g_ambi_effect_dj_filter_smoke
    s3g_ambi_effect_delay_smoke
    s3g_ambi_effect_pitch_gain_smoke
    s3g_ambi_effect_resonance_print_smoke
    s3g_ambi_effect_trace_smoke
    s3g_environmental_encoder_smoke
    s3g_vox_builder_smoke
    s3g_acapella_source_synth_smoke
    s3g_acapella_resonator_regression_smoke
    s3g_psd_raw_field_smoke
    s3g_psd_raw_field_parameter_audit
    s3g_gui_layout_contract_smoke
    s3g_ambi_group_rotate_equivalence_smoke
    s3g_environmental_svf_regression_smoke)
  foreach(S3G_SMOKE_TARGET IN LISTS S3G_NON_NIM_CORE_SMOKE_TARGETS)
    add_test(NAME ${S3G_SMOKE_TARGET} COMMAND $<TARGET_FILE:${S3G_SMOKE_TARGET}>)
    set_tests_properties(${S3G_SMOKE_TARGET} PROPERTIES
      LABELS "non_nim;smoke"
      TIMEOUT 300)
  endforeach()

  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_test(NAME s3g_drum_primitives_smoke
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>)
    set_tests_properties(s3g_drum_primitives_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_character_smoke
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>)
    set_tests_properties(s3g_drum_character_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_overload_smoke
      COMMAND $<TARGET_FILE:s3g_drum_overload_smoke>)
    set_tests_properties(s3g_drum_overload_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_echo_smoke
      COMMAND $<TARGET_FILE:s3g_drum_echo_smoke>)
    set_tests_properties(s3g_drum_echo_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_mixer_smoke
      COMMAND $<TARGET_FILE:s3g_drum_mixer_smoke>)
    set_tests_properties(s3g_drum_mixer_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_kick_smoke
      COMMAND $<TARGET_FILE:s3g_drum_kick_smoke>)
    set_tests_properties(s3g_drum_kick_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_snare_smoke
      COMMAND $<TARGET_FILE:s3g_drum_snare_smoke>)
    set_tests_properties(s3g_drum_snare_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_tom_voice_smoke
      COMMAND $<TARGET_FILE:s3g_drum_tom_voice_smoke>)
    set_tests_properties(s3g_drum_tom_voice_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_floor_tom_smoke
      COMMAND $<TARGET_FILE:s3g_drum_floor_tom_smoke>)
    set_tests_properties(s3g_drum_floor_tom_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_concert_bass_smoke
      COMMAND $<TARGET_FILE:s3g_drum_concert_bass_smoke>)
    set_tests_properties(s3g_drum_concert_bass_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_toms_smoke
      COMMAND $<TARGET_FILE:s3g_drum_toms_smoke>)
    set_tests_properties(s3g_drum_toms_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_hi_hat_smoke
      COMMAND $<TARGET_FILE:s3g_drum_hi_hat_smoke>)
    set_tests_properties(s3g_drum_hi_hat_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_clap_smoke
      COMMAND $<TARGET_FILE:s3g_drum_clap_smoke>)
    set_tests_properties(s3g_drum_clap_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_cowbell_smoke
      COMMAND $<TARGET_FILE:s3g_drum_cowbell_smoke>)
    set_tests_properties(s3g_drum_cowbell_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_drum_crash_smoke
      COMMAND $<TARGET_FILE:s3g_drum_crash_smoke>)
    set_tests_properties(s3g_drum_crash_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_processor_errant_smoke
      COMMAND $<TARGET_FILE:s3g_processor_errant_smoke>)
    set_tests_properties(s3g_processor_errant_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_ambi_membrane_kick_smoke
      COMMAND $<TARGET_FILE:s3g_ambi_membrane_kick_smoke>)
    set_tests_properties(s3g_ambi_membrane_kick_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_ambi_acid_encoder_smoke
      COMMAND $<TARGET_FILE:s3g_ambi_acid_encoder_smoke>)
    set_tests_properties(s3g_ambi_acid_encoder_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_low_frequency_synth_smoke
      COMMAND $<TARGET_FILE:s3g_low_frequency_synth_smoke>)
    set_tests_properties(s3g_low_frequency_synth_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_processor_stack_smoke
      COMMAND $<TARGET_FILE:s3g_processor_stack_smoke>)
    set_tests_properties(s3g_processor_stack_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
    add_test(NAME s3g_ambi_horizon_encoder_smoke
      COMMAND $<TARGET_FILE:s3g_ambi_horizon_encoder_smoke>)
    set_tests_properties(s3g_ambi_horizon_encoder_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
  endif()

  if(S3G_BUILD_DRUM_OVERLOAD_PREVIEW AND NOT S3G_BUILD_FUTURE_COMPONENTS)
    add_test(NAME s3g_drum_overload_smoke
      COMMAND $<TARGET_FILE:s3g_drum_overload_smoke>)
    set_tests_properties(s3g_drum_overload_smoke PROPERTIES
      LABELS "non_nim;smoke" TIMEOUT 300)
  endif()

  add_test(NAME s3g_no_input_mixer_smoke
    COMMAND $<TARGET_FILE:s3g_no_input_mixer_smoke>)
  set_tests_properties(s3g_no_input_mixer_smoke PROPERTIES
    LABELS "nim;smoke" TIMEOUT 300)

  add_test(NAME clap_bundle_installer_smoke
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/clap_bundle_installer_smoke.sh)
  set_tests_properties(clap_bundle_installer_smoke PROPERTIES
    LABELS "non_nim;packaging" TIMEOUT 120)

  if(APPLE)
    add_test(NAME no_input_mixer_app_installer_smoke
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/no_input_mixer_app_installer_smoke.sh)
    set_tests_properties(no_input_mixer_app_installer_smoke PROPERTIES
      LABELS "nim;packaging" TIMEOUT 120)
  endif()

  if(Python3_Interpreter_FOUND)
    add_test(NAME clap_bundle_manifest_source_audit
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-clap-bundle-manifest.py)
    add_test(NAME docs_static_audit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-docs.py)
    add_test(NAME realtime_audit_runner_unit
      COMMAND ${Python3_EXECUTABLE} -m unittest
        tests/test_run_clap_realtime_audit.py)
    add_test(NAME realtime_release_gates_runner_unit
      COMMAND ${Python3_EXECUTABLE} -m unittest
        tests/test_run_clap_realtime_release_gates.py)
    add_test(NAME validator_manifest_runner_unit
      COMMAND ${Python3_EXECUTABLE} -m unittest
        tests/test_run_clap_validator_manifest.py)
    add_test(NAME non_nim_release_checks_runner_unit
      COMMAND ${Python3_EXECUTABLE} -m unittest
        tests/test_run_non_nim_release_checks.py)
    add_test(NAME nim_app_package_verifier_unit
      COMMAND ${Python3_EXECUTABLE} -m unittest
        tests/test_verify_macos_nim_app_package.py)
    set_tests_properties(
      clap_bundle_manifest_source_audit
      docs_static_audit
      realtime_audit_runner_unit
      realtime_release_gates_runner_unit
      validator_manifest_runner_unit
      non_nim_release_checks_runner_unit
      PROPERTIES
        LABELS "non_nim;structural"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        TIMEOUT 120)
    set_tests_properties(nim_app_package_verifier_unit PROPERTIES
      LABELS "nim;packaging"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      TIMEOUT 120)
  endif()
endif()

option(S3G_BUILD_CLAP_PLUGIN "Build experimental CLAP plugin targets" OFF)
option(S3G_BUILD_STANDALONE_APPS "Build experimental standalone app targets" OFF)
option(S3G_FETCH_CLAP "Fetch the CLAP headers when S3G_CLAP_INCLUDE_DIR is not set" ON)
option(S3G_ENABLE_WORLD "Enable WORLD speech vocoder support where available" ON)
set(S3G_CLAP_INCLUDE_DIR "" CACHE PATH "Path containing clap/clap.h")
set(S3G_CLAP_GIT_REPOSITORY "https://github.com/free-audio/clap.git" CACHE STRING "CLAP git repository used by FetchContent")
set(S3G_CLAP_GIT_TAG "1.2.6" CACHE STRING "CLAP git tag or commit used by FetchContent")
set(S3G_WORLD_GIT_REPOSITORY "https://github.com/mmorise/World.git" CACHE STRING "WORLD git repository used by FetchContent")
set(S3G_WORLD_GIT_TAG "d625e7608ca23a870018f01e7c562ac683d9847f" CACHE STRING "WORLD git tag or commit used by FetchContent")

if(S3G_ENABLE_WORLD AND (S3G_BUILD_CLAP_PLUGIN OR S3G_BUILD_STANDALONE_APPS))
  set(WORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    world
    GIT_REPOSITORY ${S3G_WORLD_GIT_REPOSITORY}
    GIT_TAG ${S3G_WORLD_GIT_TAG}
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(world)
endif()

if(S3G_BUILD_CLAP_PLUGIN OR S3G_BUILD_STANDALONE_APPS)
  if(NOT S3G_CLAP_INCLUDE_DIR)
    if(NOT S3G_FETCH_CLAP)
      message(FATAL_ERROR "Set S3G_CLAP_INCLUDE_DIR to a CLAP include directory containing clap/clap.h, or enable S3G_FETCH_CLAP")
    endif()

    FetchContent_Declare(
      clap
      GIT_REPOSITORY ${S3G_CLAP_GIT_REPOSITORY}
      GIT_TAG ${S3G_CLAP_GIT_TAG}
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(clap)
    set(S3G_CLAP_INCLUDE_DIR ${clap_SOURCE_DIR}/include)
  endif()

  if(NOT EXISTS "${S3G_CLAP_INCLUDE_DIR}/clap/clap.h")
    message(FATAL_ERROR "S3G_CLAP_INCLUDE_DIR must contain clap/clap.h: ${S3G_CLAP_INCLUDE_DIR}")
  endif()
endif()

if(S3G_BUILD_CLAP_PLUGIN)
  if(APPLE AND Python3_Interpreter_FOUND)
    add_custom_target(audit_clap_objc_symbols
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-clap-objc-symbols.py
        --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
        --build-root ${CMAKE_BINARY_DIR}/plugins
      DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-clap-objc-symbols.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Checking compiled CLAP bundles for process-global Objective-C class collisions"
      VERBATIM)
  endif()

  if(S3G_BUILD_TRACKER_PREVIEW)
    add_executable(s3g_tracker_clap_smoke tests/tracker_clap_smoke.cpp)
    target_include_directories(s3g_tracker_clap_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_tracker_clap_smoke PRIVATE ${CMAKE_DL_LIBS})
    if(APPLE)
      set_source_files_properties(tests/tracker_clap_smoke.cpp PROPERTIES
        LANGUAGE OBJCXX)
      target_compile_options(s3g_tracker_clap_smoke PRIVATE -fobjc-arc)
      target_link_libraries(s3g_tracker_clap_smoke PRIVATE "-framework Cocoa")
    endif()
  endif()

  if(S3G_BUILD_BREAKBEAT_SLICER_PREVIEW)
    add_executable(s3g_breakbeat_slicer_clap_smoke
      tests/breakbeat_slicer_clap_smoke.cpp)
    target_include_directories(s3g_breakbeat_slicer_clap_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
    target_link_libraries(s3g_breakbeat_slicer_clap_smoke PRIVATE
      s3g_dsp
      ${CMAKE_DL_LIBS})
    if(APPLE)
      set_source_files_properties(tests/breakbeat_slicer_clap_smoke.cpp
        PROPERTIES LANGUAGE OBJCXX)
      target_compile_options(s3g_breakbeat_slicer_clap_smoke PRIVATE
        -fobjc-arc)
      target_link_libraries(s3g_breakbeat_slicer_clap_smoke PRIVATE
        "-framework Cocoa")
    endif()
  endif()

  add_executable(s3g_accelerometer_field_encoder_clap_state_smoke
    tests/accelerometer_field_encoder_clap_state_smoke.cpp)
  target_include_directories(s3g_accelerometer_field_encoder_clap_state_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_accelerometer_field_encoder_clap_state_smoke PRIVATE
    s3g_dsp ${CMAKE_DL_LIBS})

  add_executable(s3g_parameter_surface_clap_smoke tests/parameter_surface_clap_smoke.cpp)
  target_include_directories(s3g_parameter_surface_clap_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_parameter_surface_clap_smoke PRIVATE ${CMAKE_DL_LIBS})

  add_executable(s3g_formant_matrix_clap_smoke
    tests/formant_matrix_clap_smoke.cpp)
  target_include_directories(s3g_formant_matrix_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_formant_matrix_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_formant_matrix_clap_smoke PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_executable(s3g_ambi_cartography_encoder_clap_smoke
    tests/ambi_cartography_encoder_clap_smoke.cpp)
  target_include_directories(s3g_ambi_cartography_encoder_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_ambi_cartography_encoder_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_delay_processor_block_coalescing_clap_smoke
    tests/delay_processor_block_coalescing_clap_smoke.cpp)
  target_include_directories(s3g_delay_processor_block_coalescing_clap_smoke
    PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_delay_processor_block_coalescing_clap_smoke
    PRIVATE ${CMAKE_DL_LIBS})
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_delay_processor_block_coalescing_clap_smoke
      PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_executable(s3g_clap_realtime_audit
    tests/clap_realtime_audit.cpp)
  find_package(Threads REQUIRED)
  target_include_directories(s3g_clap_realtime_audit PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_clap_realtime_audit PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_clap_realtime_audit PRIVATE
      -Wall -Wextra -Wpedantic)
  endif()

  add_executable(s3g_clap_state_fixture_smoke
    tests/clap_state_fixture_smoke.cpp)
  target_include_directories(s3g_clap_state_fixture_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_clap_state_fixture_smoke PRIVATE ${CMAKE_DL_LIBS})

  add_executable(s3g_panner_state_publication_clap_smoke
    tests/panner_state_publication_clap_smoke.cpp)
  target_include_directories(s3g_panner_state_publication_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_panner_state_publication_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(s3g_panner_state_publication_clap_smoke PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_executable(s3g_preset_output_preservation_clap_smoke
    tests/preset_output_preservation_clap_smoke.cpp)
  target_include_directories(s3g_preset_output_preservation_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_preset_output_preservation_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_node_bus_clap_state_smoke
    tests/node_bus_clap_state_smoke.cpp)
  target_include_directories(s3g_node_bus_clap_state_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_node_bus_clap_state_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_spectral_spray_clap_routing_smoke
    tests/spectral_spray_clap_routing_smoke.cpp)
  target_include_directories(s3g_spectral_spray_clap_routing_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_spectral_spray_clap_routing_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_crcltr_clap_smoke tests/crcltr_clap_smoke.cpp)
  target_include_directories(s3g_crcltr_clap_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_crcltr_clap_smoke PRIVATE ${CMAKE_DL_LIBS})

  add_executable(s3g_no_input_mixer_clap_smoke
    tests/no_input_mixer_clap_smoke.cpp)
  target_include_directories(s3g_no_input_mixer_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_no_input_mixer_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_macro_shred_clap_smoke
    tests/macro_shred_clap_smoke.cpp)
  target_include_directories(s3g_macro_shred_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_macro_shred_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_macro_fracture_clap_smoke
    tests/macro_fracture_clap_smoke.cpp)
  target_include_directories(s3g_macro_fracture_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_macro_fracture_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_nim_gesture_clap_smoke
    tests/nim_gesture_clap_smoke.cpp)
  target_include_directories(s3g_nim_gesture_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_nim_gesture_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})

  add_executable(s3g_ambi_effect_resonance_print_clap_smoke
    tests/ambi_effect_resonance_print_clap_smoke.cpp)
  target_include_directories(s3g_ambi_effect_resonance_print_clap_smoke
    PRIVATE ${S3G_CLAP_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
  target_link_libraries(s3g_ambi_effect_resonance_print_clap_smoke
    PRIVATE ${CMAKE_DL_LIBS})

  add_executable(s3g_ambi_effect_trace_clap_smoke
    tests/ambi_effect_trace_clap_smoke.cpp)
  target_include_directories(s3g_ambi_effect_trace_clap_smoke
    PRIVATE ${S3G_CLAP_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
  target_link_libraries(s3g_ambi_effect_trace_clap_smoke
    PRIVATE ${CMAKE_DL_LIBS})

  add_executable(s3g_fractional_waveguide_network_clap_smoke
    tests/fractional_waveguide_network_clap_smoke.cpp)
  target_include_directories(s3g_fractional_waveguide_network_clap_smoke
    PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_fractional_waveguide_network_clap_smoke
    PRIVATE ${CMAKE_DL_LIBS})

  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_executable(s3g_feedback_shift_clap_smoke
      tests/feedback_shift_clap_smoke.cpp)
    target_include_directories(s3g_feedback_shift_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_feedback_shift_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_kick_clap_smoke
      tests/drum_kick_clap_smoke.cpp)
    target_include_directories(s3g_drum_kick_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_kick_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_snare_clap_smoke
      tests/drum_snare_clap_smoke.cpp)
    target_include_directories(s3g_drum_snare_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_snare_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_overload_clap_smoke
      tests/drum_overload_clap_smoke.cpp)
    target_include_directories(s3g_drum_overload_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_overload_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_echo_clap_smoke
      tests/drum_echo_clap_smoke.cpp)
    target_include_directories(s3g_drum_echo_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_echo_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_mixer_clap_smoke
      tests/drum_mixer_clap_smoke.cpp)
    target_include_directories(s3g_drum_mixer_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_mixer_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_floor_tom_clap_smoke
      tests/drum_floor_tom_clap_smoke.cpp)
    target_include_directories(s3g_drum_floor_tom_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_floor_tom_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_concert_bass_clap_smoke
      tests/drum_concert_bass_clap_smoke.cpp)
    target_include_directories(s3g_drum_concert_bass_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_concert_bass_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_toms_clap_smoke
      tests/drum_toms_clap_smoke.cpp)
    target_include_directories(s3g_drum_toms_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_toms_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_hi_hat_clap_smoke
      tests/drum_hi_hat_clap_smoke.cpp)
    target_include_directories(s3g_drum_hi_hat_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_hi_hat_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_clap_clap_smoke
      tests/drum_clap_clap_smoke.cpp)
    target_include_directories(s3g_drum_clap_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_clap_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_cowbell_clap_smoke
      tests/drum_cowbell_clap_smoke.cpp)
    target_include_directories(s3g_drum_cowbell_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_cowbell_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_crash_clap_smoke
      tests/drum_crash_clap_smoke.cpp)
    target_include_directories(s3g_drum_crash_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_crash_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_drum_break_clap_smoke
      tests/drum_break_clap_smoke.cpp)
    target_include_directories(s3g_drum_break_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_break_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_ambi_membrane_kick_clap_smoke
      tests/ambi_membrane_kick_clap_smoke.cpp)
    target_include_directories(s3g_ambi_membrane_kick_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_ambi_membrane_kick_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_ambi_acid_encoder_clap_smoke
      tests/ambi_acid_encoder_clap_smoke.cpp)
    target_include_directories(s3g_ambi_acid_encoder_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_ambi_acid_encoder_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_low_frequency_synth_clap_smoke
      tests/low_frequency_synth_clap_smoke.cpp)
    target_include_directories(s3g_low_frequency_synth_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_low_frequency_synth_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})

    add_executable(s3g_processor_stack_clap_smoke
      tests/processor_stack_clap_smoke.cpp)
    target_include_directories(s3g_processor_stack_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_processor_stack_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(s3g_processor_stack_clap_smoke PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
  endif()

  if(S3G_BUILD_DRUM_OVERLOAD_PREVIEW AND NOT S3G_BUILD_FUTURE_COMPONENTS)
    add_executable(s3g_drum_overload_clap_smoke
      tests/drum_overload_clap_smoke.cpp)
    target_include_directories(s3g_drum_overload_clap_smoke
      PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_drum_overload_clap_smoke
      PRIVATE ${CMAKE_DL_LIBS})
  endif()

  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_executable(s3g_ambi_horizon_encoder_clap_smoke
      tests/ambi_horizon_encoder_clap_smoke.cpp)
    target_include_directories(s3g_ambi_horizon_encoder_clap_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_ambi_horizon_encoder_clap_smoke PRIVATE
      ${CMAKE_DL_LIBS})
  endif()

  if(APPLE)
    add_executable(s3g_ambi_pulsar_encoder_gui_smoke tests/pulsar_encoder_gui_smoke.mm)
    target_include_directories(s3g_ambi_pulsar_encoder_gui_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
    target_link_libraries(s3g_ambi_pulsar_encoder_gui_smoke PRIVATE "-framework AppKit")

    add_executable(s3g_encoder_family_gui_smoke tests/encoder_family_gui_smoke.mm)
    target_include_directories(s3g_encoder_family_gui_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
    target_link_libraries(s3g_encoder_family_gui_smoke PRIVATE "-framework AppKit")

    add_executable(s3g_decoder_family_gui_smoke tests/encoder_family_gui_smoke.mm)
    target_include_directories(s3g_decoder_family_gui_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
    target_link_libraries(s3g_decoder_family_gui_smoke PRIVATE "-framework AppKit")
  endif()

  add_subdirectory(plugins/clap_24ch_passthrough)
  add_subdirectory(plugins/clap_delay_processor)
  add_subdirectory(plugins/clap_mc_to_stereo_autogain)
  add_subdirectory(plugins/clap_mc_to_quad_autogain)
  add_subdirectory(plugins/clap_ambi_effect_displacement)
  add_subdirectory(plugins/clap_ambi_point_encoder)
  add_subdirectory(plugins/clap_ambi_cloud_encoder)
  add_subdirectory(plugins/clap_ambi_cartography_encoder)
  add_subdirectory(plugins/clap_ambi_terrain_navigator)
  add_subdirectory(plugins/clap_ambi_vot_encoder)
  add_subdirectory(plugins/clap_ambi_vox_encoder)
  add_subdirectory(plugins/clap_acapella_source_synth)
  if(BUILD_TESTING)
    add_test(NAME s3g_formant_matrix_clap_smoke
      COMMAND $<TARGET_FILE:s3g_formant_matrix_clap_smoke>
        $<TARGET_FILE:s3g_acapella_source_synth_clap>)
    set_tests_properties(s3g_formant_matrix_clap_smoke PROPERTIES
      LABELS "non_nim;clap;smoke" TIMEOUT 120)
    if(APPLE)
      add_test(NAME s3g_formant_matrix_gui_smoke
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_acapella_source_synth_clap>
          org.s3g.s3g-dsp.formant-matrix
          1356 968 "s3g Processor Formant Matrix" responsive)
      set_tests_properties(s3g_formant_matrix_gui_smoke PROPERTIES
        LABELS "non_nim;clap;gui" TIMEOUT 120)
    endif()
  endif()
  add_subdirectory(plugins/clap_ambi_wave_terrain_encoder)
  add_subdirectory(plugins/clap_ambi_stochastic_encoder)
  add_subdirectory(plugins/clap_ambi_wrangler_encoder)
  add_subdirectory(plugins/clap_ambi_wind_encoder)
  add_subdirectory(plugins/clap_ambi_water_encoder)
  add_subdirectory(plugins/clap_ambi_pyrosphere_encoder)
  add_subdirectory(plugins/clap_ambi_cryosphere_encoder)
  add_subdirectory(plugins/clap_ambi_insect_encoder)
  add_subdirectory(plugins/clap_accelerometer_field_encoder)
  add_subdirectory(plugins/clap_ambi_encoder_medium)
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_kick)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_snare)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_overload)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_echo)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_mixer)
  endif()
  if(S3G_BUILD_DRUM_OVERLOAD_PREVIEW AND NOT S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_overload)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_floor_tom)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_concert_bass)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_toms)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_hi_hat)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_clap)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_cowbell)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_crash)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_drum_break)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_ambi_membrane_kick)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_ambi_acid_encoder)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_low_frequency_synth)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_processor_stack)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_ambi_horizon_encoder)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_processor_errant)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_feedback_shift)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_subdirectory(plugins/clap_processor_fissure)
  endif()
  if(S3G_BUILD_FUTURE_COMPONENTS AND BUILD_TESTING)
    add_test(NAME s3g_feedback_shift_clap_smoke
      COMMAND $<TARGET_FILE:s3g_feedback_shift_clap_smoke>
        $<TARGET_FILE:s3g_feedback_shift_clap>)
    set_tests_properties(s3g_feedback_shift_clap_smoke PROPERTIES
      LABELS "non_nim;clap;smoke" TIMEOUT 120)
    if(APPLE)
      add_test(NAME s3g_feedback_shift_gui_smoke
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_feedback_shift_clap>
          org.s3g.s3g-dsp.feedback-shift
          1100 760 "s3g Processor Feedback Shift" responsive)
      set_tests_properties(s3g_feedback_shift_gui_smoke PROPERTIES
        LABELS "non_nim;clap;gui" TIMEOUT 120)
    endif()
  endif()
  add_subdirectory(plugins/clap_ambi_pulsar_encoder)
  add_subdirectory(plugins/clap_ambi_neural_ecology)
  add_subdirectory(plugins/clap_no_input_mixer)
  add_subdirectory(plugins/clap_nim_gesture)
  if(S3G_BUILD_TRACKER_PREVIEW)
    add_subdirectory(plugins/clap_tracker)
  endif()
  if(S3G_BUILD_BREAKBEAT_SLICER_PREVIEW)
    add_subdirectory(plugins/clap_breakbeat_slicer)
  endif()
  if(S3G_BUILD_TRACKER_PREVIEW AND BUILD_TESTING)
    add_test(NAME s3g_tracker_clap_smoke
      COMMAND $<TARGET_FILE:s3g_tracker_clap_smoke>
        $<TARGET_FILE:s3g_tracker_clap>)
    set_tests_properties(s3g_tracker_clap_smoke PROPERTIES
      LABELS "non_nim;tracker;clap;smoke" TIMEOUT 120)
  endif()
  if(S3G_BUILD_BREAKBEAT_SLICER_PREVIEW AND BUILD_TESTING)
    add_test(NAME s3g_breakbeat_slicer_clap_smoke
      COMMAND $<TARGET_FILE:s3g_breakbeat_slicer_clap_smoke>
        $<TARGET_FILE:s3g_breakbeat_slicer_clap>)
    set_tests_properties(s3g_breakbeat_slicer_clap_smoke PROPERTIES
      LABELS "non_nim;breakbeat-slicer;clap;smoke" TIMEOUT 120)
  endif()
  add_subdirectory(plugins/clap_ambi_imprint)
  add_subdirectory(plugins/clap_ambi_ray_encoder)
  add_subdirectory(plugins/clap_ambi_ray_bilocation_encoder)
  add_subdirectory(plugins/clap_ambi_path_encoder)
  add_subdirectory(plugins/clap_ambi_speaker_decoder)
  add_subdirectory(plugins/clap_layout_panner)
  add_subdirectory(plugins/clap_dbap_panner)
  add_subdirectory(plugins/clap_lbap_panner)
  add_subdirectory(plugins/clap_vbap_panner)
  add_subdirectory(plugins/clap_sub_crossover)
  add_subdirectory(plugins/clap_loop_processor)
  add_subdirectory(plugins/clap_multi_loop_processor)
  add_subdirectory(plugins/clap_ambi_grain_processor)
  add_subdirectory(plugins/clap_macro_delay)
  add_subdirectory(plugins/clap_macro_fracture)
  add_subdirectory(plugins/clap_macro_pitch)
  add_subdirectory(plugins/clap_macro_shred)
  add_subdirectory(plugins/clap_buffer_processor)
  add_subdirectory(plugins/clap_wave_geometry_processor)
  add_subdirectory(plugins/clap_multichannel_meter)
  add_subdirectory(plugins/clap_ambisonic_energy_visualizer)
  add_subdirectory(plugins/clap_ambisonic_stereo_decoder)
  add_subdirectory(plugins/clap_ambisonic_head_decoder)
  add_subdirectory(plugins/clap_ambisonic_sub_decoder)
  add_subdirectory(plugins/clap_ambi_object_decoder)
  add_subdirectory(plugins/clap_ambi_adaptive_decoder)
  add_subdirectory(plugins/clap_array_hpf)
  add_subdirectory(plugins/clap_array_delay)
  add_subdirectory(plugins/clap_array_trim)
  add_subdirectory(plugins/clap_ambisonic_rotate)
  add_subdirectory(plugins/clap_ambi_effect_dj_filter)
  add_subdirectory(plugins/clap_ambi_effect_delay)
  add_subdirectory(plugins/clap_ambi_effect_pitch_gain)
  add_subdirectory(plugins/clap_ambi_effect_resonance_print)
  add_subdirectory(plugins/clap_ambi_effect_trace)
  add_subdirectory(plugins/clap_ambi_group_rotate)
  add_subdirectory(plugins/clap_ambi_group_depth)
  add_subdirectory(plugins/clap_ambisonic_order_band_tool)
  add_subdirectory(plugins/clap_ambi_group_matrix)
  add_subdirectory(plugins/clap_ambi_group_matrix_128)
  add_subdirectory(plugins/clap_group_matrix)
  add_subdirectory(plugins/clap_group_matrix_32)
  add_subdirectory(plugins/clap_node_track_mixer)
  add_subdirectory(plugins/clap_spectral_spray)
  add_subdirectory(plugins/clap_8ch_spectral_spray)
  add_subdirectory(plugins/clap_spectral_topology_processor)
  add_subdirectory(plugins/clap_shard_scatter)
  add_subdirectory(plugins/clap_psd_raw_field)
  add_subdirectory(plugins/clap_orbit_delay)
  add_subdirectory(plugins/clap_cascade_taps)
  add_subdirectory(plugins/clap_crcltr)

  if(BUILD_TESTING)
    set(S3G_V050_STATE_FIXTURE_ROOT
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clap-state/v0.5.0-pre)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${S3G_V050_STATE_FIXTURE_ROOT}/manifest.tsv)
    file(STRINGS ${S3G_V050_STATE_FIXTURE_ROOT}/manifest.tsv
      S3G_V050_STATE_FIXTURE_ROWS)
    foreach(S3G_STATE_FIXTURE_ROW IN LISTS S3G_V050_STATE_FIXTURE_ROWS)
      if(S3G_STATE_FIXTURE_ROW MATCHES "^[ \t]*#" OR S3G_STATE_FIXTURE_ROW STREQUAL "")
        continue()
      endif()
      string(REPLACE "\t" ";" S3G_STATE_FIXTURE_FIELDS
        "${S3G_STATE_FIXTURE_ROW}")
      list(LENGTH S3G_STATE_FIXTURE_FIELDS S3G_STATE_FIXTURE_FIELD_COUNT)
      if(NOT S3G_STATE_FIXTURE_FIELD_COUNT EQUAL 4)
        message(FATAL_ERROR
          "Malformed v0.5.0-pre state fixture row: ${S3G_STATE_FIXTURE_ROW}")
      endif()
      list(GET S3G_STATE_FIXTURE_FIELDS 0 S3G_STATE_FIXTURE_BUILD_PATH)
      list(GET S3G_STATE_FIXTURE_FIELDS 1 S3G_STATE_FIXTURE_PLUGIN_ID)
      list(GET S3G_STATE_FIXTURE_FIELDS 2 S3G_STATE_FIXTURE_FILE)
      list(GET S3G_STATE_FIXTURE_FIELDS 3 S3G_STATE_FIXTURE_PARAMS_FILE)
      string(MAKE_C_IDENTIFIER "${S3G_STATE_FIXTURE_PLUGIN_ID}"
        S3G_STATE_FIXTURE_TEST_SUFFIX)
      set(S3G_STATE_FIXTURE_TEST
        state_compat_v050_${S3G_STATE_FIXTURE_TEST_SUFFIX})
      add_test(NAME ${S3G_STATE_FIXTURE_TEST}
        COMMAND $<TARGET_FILE:s3g_clap_state_fixture_smoke>
          --verify
          ${CMAKE_BINARY_DIR}/plugins/${S3G_STATE_FIXTURE_BUILD_PATH}
          ${S3G_STATE_FIXTURE_PLUGIN_ID}
          ${S3G_V050_STATE_FIXTURE_ROOT}/${S3G_STATE_FIXTURE_FILE}
          ${S3G_V050_STATE_FIXTURE_ROOT}/${S3G_STATE_FIXTURE_PARAMS_FILE})
      set_tests_properties(${S3G_STATE_FIXTURE_TEST} PROPERTIES
        LABELS "non_nim;clap;state_compat" TIMEOUT 120)
    endforeach()

    add_test(NAME panner_state_publication_layout
      COMMAND $<TARGET_FILE:s3g_panner_state_publication_clap_smoke>
        $<TARGET_FILE:s3g_layout_panner_clap>
        org.s3g.s3g-dsp.layout-panner)
    add_test(NAME panner_state_publication_dbap
      COMMAND $<TARGET_FILE:s3g_panner_state_publication_clap_smoke>
        $<TARGET_FILE:s3g_dbap_panner_clap>
        org.s3g.s3g-dsp.dbap-panner)
    add_test(NAME panner_state_publication_lbap
      COMMAND $<TARGET_FILE:s3g_panner_state_publication_clap_smoke>
        $<TARGET_FILE:s3g_lbap_panner_clap>
        org.s3g.s3g-dsp.lbap-panner)
    add_test(NAME panner_state_publication_vbap
      COMMAND $<TARGET_FILE:s3g_panner_state_publication_clap_smoke>
        $<TARGET_FILE:s3g_vbap_panner_clap>
        org.s3g.s3g-dsp.vbap-panner)
    set_tests_properties(
      panner_state_publication_layout
      panner_state_publication_dbap
      panner_state_publication_lbap
      panner_state_publication_vbap
      PROPERTIES LABELS "non_nim;clap;ownership" TIMEOUT 120)
  endif()

  add_custom_target(audit_node_bus_param_state
    COMMAND $<TARGET_FILE:s3g_node_bus_clap_state_smoke>
      $<TARGET_FILE:s3g_node_track_mixer_clap>
      org.s3g.s3g-dsp.node-bus-mixer
    DEPENDS
      s3g_node_bus_clap_state_smoke
      s3g_node_track_mixer_clap
    COMMENT "Validating Node Bus derived parameter publication and state"
    VERBATIM)

  add_custom_target(audit_delay_processor_block_coalescing
    COMMAND $<TARGET_FILE:s3g_delay_processor_block_coalescing_clap_smoke>
      $<TARGET_FILE:s3g_24ch_delay_processor_clap>
      org.s3g.s3g-dsp.delay-processor-24ch
    DEPENDS
      s3g_delay_processor_block_coalescing_clap_smoke
      s3g_24ch_delay_processor_clap
    COMMENT "Validating Delay Processor block-coalesced parameter automation"
    VERBATIM)

  # Manual, deliberately focused timing audit. Keep this representative set
  # small so it remains useful during DSP development; it is not a substitute
  # for a full inventory pass or live Core Audio xrun testing. Do not expose a
  # plausibly named timing target from an unoptimized single-config build.
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_target(audit_clap_realtime_representative
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_24ch_passthrough_clap>
      org.s3g.s3g-dsp.24ch-passthrough-test
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_no_input_mixer_clap>
      org.s3g.s3g-dsp.no-input-mixer-8ch
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_psd_raw_field_clap>
      org.s3g.s3g-dsp.fault
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_nim_gesture_clap>
      org.s3g.s3g-dsp.nim-gesture
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_ambi_insect_encoder_clap>
      org.s3g.s3g-dsp.ambi-insect-encoder-64
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_ambi_speaker_decoder_clap>
      org.s3g.s3g-dsp.ambi-speaker-decoder-64
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_24ch_spectral_topology_processor_clap>
      org.s3g.s3g-dsp.spectral-topology-processor-24ch
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256 --event-burst 1
      $<TARGET_FILE:s3g_ambi_group_rotate_128_clap>
      org.s3g.s3g-dsp.ambi-group-rotate-128
    COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
      --sample-rates 48000 --blocks 64,256
      $<TARGET_FILE:s3g_ambi_group_matrix_128_clap>
      org.s3g.s3g-dsp.ambi-group-matrix-128
    DEPENDS
      s3g_clap_realtime_audit
      s3g_24ch_passthrough_clap
      s3g_no_input_mixer_clap
      s3g_psd_raw_field_clap
      s3g_nim_gesture_clap
      s3g_ambi_insect_encoder_clap
      s3g_ambi_speaker_decoder_clap
      s3g_24ch_spectral_topology_processor_clap
      s3g_ambi_group_rotate_128_clap
      s3g_ambi_group_matrix_128_clap
      COMMENT "Timing a representative Release CLAP set without opening an audio device"
      VERBATIM)

    if(Python3_Interpreter_FOUND)
      set(S3G_REALTIME_WEAK_POINT_TARGETS
        s3g_24ch_delay_processor_clap
        s3g_ambisonic_rotate_clap
        s3g_ambi_group_rotate_64_clap
        s3g_ambi_group_rotate_128_clap
        s3g_node_track_mixer_clap
        s3g_spectral_topology_processor_clap
        s3g_24ch_spectral_topology_processor_clap
        s3g_spectral_spray_clap
        s3g_8ch_spectral_spray_clap
        s3g_ambi_water_encoder_clap
        s3g_ambi_insect_encoder_clap)

      add_custom_target(audit_clap_realtime_weak_points
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-clap-realtime-audit.py
          --audit-executable $<TARGET_FILE:s3g_clap_realtime_audit>
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
          --build-root ${CMAKE_BINARY_DIR}/plugins
          --affected-only --full-sweep --automation-ladder
          --timeout-seconds 1800
          --output ${CMAKE_BINARY_DIR}/clap-realtime-weak-points.json
        DEPENDS
          s3g_clap_realtime_audit
          ${S3G_REALTIME_WEAK_POINT_TARGETS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Auditing weak-point CLAPs at 48/96 kHz and 32/64/128/256 frames"
        VERBATIM)

      if(TARGET s3g_realtime_alloc_probe)
        add_custom_target(audit_clap_realtime_release_gate
          COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-clap-realtime-release-gates.py
            --audit-executable $<TARGET_FILE:s3g_clap_realtime_audit>
            --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
            --build-root ${CMAKE_BINARY_DIR}/plugins
            --output-dir ${CMAKE_BINARY_DIR}
            --allocation-probe-library $<TARGET_FILE:s3g_realtime_alloc_probe>
            --include-advisory
          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
          COMMENT "Running allocation-probed strict CLAP release gates and the 96 kHz advisory"
          VERBATIM)
        add_dependencies(audit_clap_realtime_release_gate
          s3g_clap_realtime_audit
          s3g_realtime_alloc_probe
          ${S3G_REALTIME_WEAK_POINT_TARGETS})
      endif()

      add_custom_target(audit_no_input_mixer_controller_realtime
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-clap-realtime-audit.py
          --audit-executable $<TARGET_FILE:s3g_clap_realtime_audit>
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
          --build-root ${CMAKE_BINARY_DIR}/plugins
          --filter "^org\\.s3g\\.s3g-dsp\\.no-input-mixer-8ch$"
          --full-sweep --automation-ladder
          --control-publication-stress --nim-midi-flood
          --release-gate
          --output ${CMAKE_BINARY_DIR}/no-input-mixer-controller-realtime.json
        DEPENDS
          s3g_clap_realtime_audit
          s3g_no_input_mixer_clap
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running the long NIM E16/BU16 and control-publication gate"
        VERBATIM)
    endif()
  endif()

  add_custom_target(audit_ambi_encoder_medium
    COMMAND $<TARGET_FILE:s3g_fractional_waveguide_network_smoke>
    COMMAND $<TARGET_FILE:s3g_fractional_waveguide_network_clap_smoke>
      $<TARGET_FILE:s3g_ambi_encoder_medium_clap>
      org.s3g.s3g-dsp.ambi-encoder-medium-16
    COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
      $<TARGET_FILE:s3g_ambi_encoder_medium_clap>
      org.s3g.s3g-dsp.ambi-encoder-medium-16
      920 680 "s3g Ambi Encoder Medium" responsive
    DEPENDS
      s3g_fractional_waveguide_network_smoke
      s3g_fractional_waveguide_network_clap_smoke
      s3g_encoder_family_gui_smoke
      s3g_ambi_encoder_medium_clap
    COMMENT "Validating fractional waveguide DSP, CLAP contract, state, and GUI")

  if(S3G_BUILD_FUTURE_COMPONENTS)
    add_custom_target(audit_drum_kick
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_kick_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_kick_clap_smoke>
        $<TARGET_FILE:s3g_drum_kick_clap>
        org.s3g.s3g-dsp.drum-kick
      DEPENDS
        s3g_drum_character_smoke
        s3g_drum_kick_smoke
        s3g_drum_kick_clap_smoke
        s3g_drum_kick_clap
      COMMENT "Validating shared drum character DSP, kick synthesis, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_kick_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_kick_clap>
          org.s3g.s3g-dsp.drum-kick
          920 680 "s3g Drum Kick" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_kick_clap
        COMMENT "Validating the responsive Drum Kick editor")
      add_dependencies(audit_drum_kick
        audit_drum_kick_gui)
    endif()

    add_custom_target(audit_drum_snare
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_snare_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_snare_clap_smoke>
        $<TARGET_FILE:s3g_drum_snare_clap>
        org.s3g.s3g-dsp.drum-snare
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_snare_smoke
        s3g_drum_snare_clap_smoke
        s3g_drum_snare_clap
      COMMENT "Validating shared drum primitives, snare synthesis, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_snare_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_snare_clap>
          org.s3g.s3g-dsp.drum-snare
          920 680 "s3g Drum Snare" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_snare_clap
        COMMENT "Validating the responsive Drum Snare editor")
      add_dependencies(audit_drum_snare
        audit_drum_snare_gui)
    endif()

    add_custom_target(audit_drum_floor_tom
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_tom_voice_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_floor_tom_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_floor_tom_clap_smoke>
        $<TARGET_FILE:s3g_drum_floor_tom_clap>
        org.s3g.s3g-dsp.drum-floor-tom
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_tom_voice_smoke
        s3g_drum_floor_tom_smoke
        s3g_drum_floor_tom_clap_smoke
        s3g_drum_floor_tom_clap
      COMMENT "Validating shared tom voice DSP, Floor Tom synthesis, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_floor_tom_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_floor_tom_clap>
          org.s3g.s3g-dsp.drum-floor-tom
          920 680 "s3g Drum Floor Tom" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_floor_tom_clap
        COMMENT "Validating the responsive Drum Floor Tom editor")
      add_dependencies(audit_drum_floor_tom
        audit_drum_floor_tom_gui)
    endif()

    add_custom_target(audit_drum_concert_bass
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_concert_bass_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_concert_bass_clap_smoke>
        $<TARGET_FILE:s3g_drum_concert_bass_clap>
        org.s3g.s3g-dsp.drum-concert-bass
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 44100,48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_drum_concert_bass_clap>
        org.s3g.s3g-dsp.drum-concert-bass
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_concert_bass_smoke
        s3g_drum_concert_bass_clap_smoke
        s3g_drum_concert_bass_clap
        s3g_clap_realtime_audit
      COMMENT "Validating Concert Bass Drum synthesis, articulations, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_concert_bass_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_concert_bass_clap>
          org.s3g.s3g-dsp.drum-concert-bass
          920 704 "s3g Drum Concert Bass" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_concert_bass_clap
        COMMENT "Validating the responsive Drum Concert Bass editor")
      add_dependencies(audit_drum_concert_bass
        audit_drum_concert_bass_gui)
    endif()

    add_custom_target(audit_drum_toms
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_tom_voice_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_toms_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_toms_clap_smoke>
        $<TARGET_FILE:s3g_drum_toms_clap>
        org.s3g.s3g-dsp.drum-toms
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_tom_voice_smoke
        s3g_drum_toms_smoke
        s3g_drum_toms_clap_smoke
        s3g_drum_toms_clap
      COMMENT "Validating shared tom voice DSP, Low/Mid/High Toms synthesis, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_toms_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_toms_clap>
          org.s3g.s3g-dsp.drum-toms
          920 680 "s3g Drum Toms" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_toms_clap
        COMMENT "Validating the responsive Low/Mid/High Drum Toms editor")
      add_dependencies(audit_drum_toms
        audit_drum_toms_gui)
    endif()

    add_custom_target(audit_drum_hi_hat
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_hi_hat_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_hi_hat_clap_smoke>
        $<TARGET_FILE:s3g_drum_hi_hat_clap>
        org.s3g.s3g-dsp.drum-hi-hat
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_hi_hat_smoke
        s3g_drum_hi_hat_clap_smoke
        s3g_drum_hi_hat_clap
      COMMENT "Validating hi-hat synthesis, articulations, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_hi_hat_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_hi_hat_clap>
          org.s3g.s3g-dsp.drum-hi-hat
          920 680 "s3g Drum Hi-Hat" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_hi_hat_clap
        COMMENT "Validating the responsive Drum Hi-Hat editor")
      add_dependencies(audit_drum_hi_hat
        audit_drum_hi_hat_gui)
    endif()

    add_custom_target(audit_drum_clap
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_clap_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_clap_clap_smoke>
        $<TARGET_FILE:s3g_drum_clap_clap>
        org.s3g.s3g-dsp.drum-clap
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_clap_smoke
        s3g_drum_clap_clap_smoke
        s3g_drum_clap_clap
      COMMENT "Validating clap synthesis, articulations, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_clap_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_clap_clap>
          org.s3g.s3g-dsp.drum-clap
          920 680 "s3g Drum Clap" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_clap_clap
        COMMENT "Validating the responsive Drum Clap editor")
      add_dependencies(audit_drum_clap
        audit_drum_clap_gui)
    endif()

    add_custom_target(audit_drum_cowbell
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_cowbell_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_cowbell_clap_smoke>
        $<TARGET_FILE:s3g_drum_cowbell_clap>
        org.s3g.s3g-dsp.drum-cowbell
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_cowbell_smoke
        s3g_drum_cowbell_clap_smoke
        s3g_drum_cowbell_clap
      COMMENT "Validating cowbell synthesis, articulations, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_cowbell_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_cowbell_clap>
          org.s3g.s3g-dsp.drum-cowbell
          920 680 "s3g Drum Cowbell" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_cowbell_clap
        COMMENT "Validating the responsive Drum Cowbell editor")
      add_dependencies(audit_drum_cowbell
        audit_drum_cowbell_gui)
    endif()

    add_custom_target(audit_drum_crash
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_crash_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_crash_clap_smoke>
        $<TARGET_FILE:s3g_drum_crash_clap>
        org.s3g.s3g-dsp.drum-crash
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_crash_smoke
        s3g_drum_crash_clap_smoke
        s3g_drum_crash_clap
      COMMENT "Validating crash synthesis, articulations, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_crash_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_crash_clap>
          org.s3g.s3g-dsp.drum-crash
          920 680 "s3g Drum Crash" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_crash_clap
        COMMENT "Validating the responsive Drum Crash editor")
      add_dependencies(audit_drum_crash
        audit_drum_crash_gui)
    endif()

    add_custom_target(audit_drum_break
      COMMAND $<TARGET_FILE:s3g_drum_primitives_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_character_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_break_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_break_clap_smoke>
        $<TARGET_FILE:s3g_drum_break_clap>
        org.s3g.s3g-dsp.drum-break
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_drum_break_clap>
        org.s3g.s3g-dsp.drum-break
      DEPENDS
        s3g_drum_primitives_smoke
        s3g_drum_character_smoke
        s3g_drum_break_smoke
        s3g_drum_break_clap_smoke
        s3g_drum_break_clap
        s3g_clap_realtime_audit
      COMMENT "Validating four-voice Break synthesis, MIDI, state, realtime safety, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_drum_break_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_break_clap>
          org.s3g.s3g-dsp.drum-break
          920 760 "s3g Drum Break" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_break_clap
        COMMENT "Validating the responsive Drum Break editor")
      add_dependencies(audit_drum_break audit_drum_break_gui)
    endif()

    add_custom_target(audit_drum_overload
      COMMAND $<TARGET_FILE:s3g_drum_overload_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_overload_clap_smoke>
        $<TARGET_FILE:s3g_drum_overload_clap>
        org.s3g.s3g-dsp.drum-overload
      DEPENDS
        s3g_drum_overload_smoke
        s3g_drum_overload_clap_smoke
        s3g_drum_overload_clap
      COMMENT "Validating stereo drum overload DSP, CLAP ports, automation, and state")
    if(APPLE)
      add_custom_target(audit_drum_overload_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_overload_clap>
          org.s3g.s3g-dsp.drum-overload
          760 376 "s3g Drum " responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_overload_clap
        COMMENT "Validating the responsive Drum Overload editor")
      add_dependencies(audit_drum_overload
        audit_drum_overload_gui)
    endif()

    add_custom_target(audit_drum_echo
      COMMAND $<TARGET_FILE:s3g_drum_echo_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_echo_clap_smoke>
        $<TARGET_FILE:s3g_drum_echo_clap>
        org.s3g.s3g-dsp.drum-echo
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_drum_echo_clap>
        org.s3g.s3g-dsp.drum-echo
      DEPENDS
        s3g_drum_echo_smoke
        s3g_drum_echo_clap_smoke
        s3g_drum_echo_clap
        s3g_clap_realtime_audit
      COMMENT "Validating transient-aware multi-head Drum Echo DSP, CLAP ports, automation, and state")
    if(APPLE)
      add_custom_target(audit_drum_echo_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_echo_clap>
          org.s3g.s3g-dsp.drum-echo
          760 376 "s3g Drum " responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_echo_clap
        COMMENT "Validating the responsive Drum Echo editor")
      add_dependencies(audit_drum_echo audit_drum_echo_gui)
    endif()

    add_custom_target(audit_drum_mixer
      COMMAND $<TARGET_FILE:s3g_drum_mixer_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_mixer_clap_smoke>
        $<TARGET_FILE:s3g_drum_mixer_clap>
        org.s3g.s3g-dsp.drum-mixer-16
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_drum_mixer_clap>
        org.s3g.s3g-dsp.drum-mixer-16
      DEPENDS
        s3g_drum_mixer_smoke
        s3g_drum_mixer_clap_smoke
        s3g_drum_mixer_clap
        s3g_clap_realtime_audit
      COMMENT "Validating 8x2 drum mixer DSP, 16-channel routing, automation, and state")
    if(APPLE)
      add_custom_target(audit_drum_mixer_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_mixer_clap>
          org.s3g.s3g-dsp.drum-mixer-16
          1320 600 "s3g Drum Mixer 16" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_mixer_clap
        COMMENT "Validating the responsive Drum Mixer 16 editor")
      add_dependencies(audit_drum_mixer audit_drum_mixer_gui)
    endif()

    add_custom_target(audit_drum_family
      DEPENDS
        audit_drum_kick
        audit_drum_snare
        audit_drum_floor_tom
        audit_drum_concert_bass
        audit_drum_toms
        audit_drum_hi_hat
        audit_drum_clap
        audit_drum_cowbell
        audit_drum_crash
        audit_drum_break
        audit_drum_overload
        audit_drum_echo
        audit_drum_mixer
      COMMENT "Validating the shared procedural drum instrument and effect family")

    add_custom_target(audit_ambi_membrane_kick
      COMMAND $<TARGET_FILE:s3g_ambi_membrane_kick_smoke>
      COMMAND $<TARGET_FILE:s3g_ambi_membrane_kick_clap_smoke>
        $<TARGET_FILE:s3g_ambi_membrane_kick_clap>
        org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16
      DEPENDS
        s3g_ambi_membrane_kick_smoke
        s3g_ambi_membrane_kick_clap_smoke
        s3g_ambi_membrane_kick_clap
      COMMENT "Validating shaped membrane bass synthesis, ambisonic radiation, MIDI, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_ambi_membrane_kick_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_ambi_membrane_kick_clap>
          org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16
          920 680 "s3g Ambi Encoder Membrane Kick" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_ambi_membrane_kick_clap
        COMMENT "Validating the responsive Ambi Encoder Membrane Kick editor")
      add_dependencies(audit_ambi_membrane_kick
        audit_ambi_membrane_kick_gui)
    endif()

    add_custom_target(audit_ambi_acid_encoder
      COMMAND $<TARGET_FILE:s3g_ambi_acid_encoder_smoke>
      COMMAND $<TARGET_FILE:s3g_ambi_acid_encoder_clap_smoke>
        $<TARGET_FILE:s3g_ambi_acid_encoder_clap>
        org.s3g.s3g-dsp.ambi-encoder-acid-16
      DEPENDS
        s3g_ambi_acid_encoder_smoke
        s3g_ambi_acid_encoder_clap_smoke
        s3g_ambi_acid_encoder_clap
      COMMENT "Validating acid synthesis, sub/drive voice, MIDI transpose, spatial/dual-mono output, state, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_ambi_acid_encoder_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_ambi_acid_encoder_clap>
          org.s3g.s3g-dsp.ambi-encoder-acid-16
          920 620 "s3g Ambi Encoder Acid" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_ambi_acid_encoder_clap
        COMMENT "Validating the responsive Ambi Encoder Acid dual-view path editor")
      add_dependencies(audit_ambi_acid_encoder
        audit_ambi_acid_encoder_gui)
    endif()

    add_custom_target(audit_low_frequency_synth
      COMMAND $<TARGET_FILE:s3g_low_frequency_synth_smoke>
      COMMAND $<TARGET_FILE:s3g_low_frequency_synth_clap_smoke>
        $<TARGET_FILE:s3g_low_frequency_synth_clap>
        org.s3g.s3g-dsp.low-frequency-synth
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 44100,48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_low_frequency_synth_clap>
        org.s3g.s3g-dsp.low-frequency-synth
      DEPENDS
        s3g_low_frequency_synth_smoke
        s3g_low_frequency_synth_clap_smoke
        s3g_low_frequency_synth_clap
        s3g_clap_realtime_audit
      COMMENT "Validating composite-membrane bass synthesis, stereo Shred regeneration, MIDI, state, realtime safety, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_low_frequency_synth_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_low_frequency_synth_clap>
          org.s3g.s3g-dsp.low-frequency-synth
          980 672 "s3g Processor LF Synth" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_low_frequency_synth_clap
        COMMENT "Validating the responsive Processor LF Synth editor")
      add_dependencies(audit_low_frequency_synth
        audit_low_frequency_synth_gui)
    endif()

    add_custom_target(audit_processor_stack
      COMMAND $<TARGET_FILE:s3g_processor_stack_smoke>
      COMMAND $<TARGET_FILE:s3g_processor_stack_clap_smoke>
        $<TARGET_FILE:s3g_processor_stack_clap>
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 44100,48000,96000 --blocks 16,64,256
        --event-bursts 1,16 --distributed-events
        $<TARGET_FILE:s3g_processor_stack_clap>
        org.s3g.s3g-dsp.processor-stack
      DEPENDS
        s3g_processor_stack_smoke
        s3g_processor_stack_clap_smoke
        s3g_processor_stack_clap
        s3g_clap_realtime_audit
      COMMENT "Validating shared-stack synthesis, governed speaker feedback, MIDI, state, realtime safety, and CLAP contract")
    if(APPLE)
      add_custom_target(audit_processor_stack_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_processor_stack_clap>
          org.s3g.s3g-dsp.processor-stack
          980 856 "s3g Processor Stack" responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_processor_stack_clap
        COMMENT "Validating the responsive Processor Stack editor")
      add_dependencies(audit_processor_stack
        audit_processor_stack_gui)
    endif()
  endif()

  if(S3G_BUILD_DRUM_OVERLOAD_PREVIEW AND NOT S3G_BUILD_FUTURE_COMPONENTS)
    add_custom_target(audit_drum_overload
      COMMAND $<TARGET_FILE:s3g_drum_overload_smoke>
      COMMAND $<TARGET_FILE:s3g_drum_overload_clap_smoke>
        $<TARGET_FILE:s3g_drum_overload_clap>
        org.s3g.s3g-dsp.drum-overload
      DEPENDS
        s3g_drum_overload_smoke
        s3g_drum_overload_clap_smoke
        s3g_drum_overload_clap
      COMMENT "Validating stereo drum overload DSP, CLAP ports, automation, and state")
    if(APPLE)
      add_custom_target(audit_drum_overload_gui
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
          $<TARGET_FILE:s3g_drum_overload_clap>
          org.s3g.s3g-dsp.drum-overload
          760 376 "s3g Drum " responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_drum_overload_clap
        COMMENT "Validating the responsive Drum Overload editor")
      add_dependencies(audit_drum_overload audit_drum_overload_gui)
    endif()
  endif()

  if(S3G_BUILD_FUTURE_COMPONENTS)
    set(S3G_HORIZON_REALTIME_REPORT
      "${CMAKE_BINARY_DIR}/ambi-horizon-realtime.json")
    add_custom_target(audit_ambi_horizon
      COMMAND $<TARGET_FILE:s3g_ambi_horizon_encoder_smoke>
      COMMAND $<TARGET_FILE:s3g_ambi_horizon_encoder_clap_smoke>
        $<TARGET_FILE:s3g_ambi_horizon_encoder_clap>
        org.s3g.s3g-dsp.ambi-horizon-encoder-64
      COMMAND $<TARGET_FILE:s3g_clap_realtime_audit>
        --sample-rates 48000,96000 --blocks 16,32,64,256
        --event-bursts 1,8,32 --distributed-events
        --json "${S3G_HORIZON_REALTIME_REPORT}"
        $<TARGET_FILE:s3g_ambi_horizon_encoder_clap>
        org.s3g.s3g-dsp.ambi-horizon-encoder-64
      COMMAND ${CMAKE_COMMAND}
        "-DREPORT=${S3G_HORIZON_REALTIME_REPORT}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_horizon_realtime_report.cmake
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
        $<TARGET_FILE:s3g_ambi_horizon_encoder_clap>
        org.s3g.s3g-dsp.ambi-horizon-encoder-64
        1160 760 "s3g Ambi Encoder Horizon" responsive
      DEPENDS
        s3g_ambi_horizon_encoder_smoke
        s3g_ambi_horizon_encoder_clap_smoke
        s3g_clap_realtime_audit
        s3g_encoder_family_gui_smoke
        s3g_ambi_horizon_encoder_clap
      COMMENT "Validating Horizon DSP, realtime CLAP, and GUI")

    if(TARGET s3g_realtime_alloc_probe)
      set(S3G_HORIZON_ALLOC_REPORT
        "${CMAKE_BINARY_DIR}/ambi-horizon-realtime-alloc.json")
      add_custom_target(audit_ambi_horizon_alloc
        COMMAND ${CMAKE_COMMAND} -E env
          "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:s3g_realtime_alloc_probe>"
          $<TARGET_FILE:s3g_clap_realtime_audit>
          --sample-rates 48000,96000 --blocks 16,32,64,256
          --iterations 500 --event-bursts 1,8,32 --distributed-events
          --allocation-probe --json "${S3G_HORIZON_ALLOC_REPORT}"
          $<TARGET_FILE:s3g_ambi_horizon_encoder_clap>
          org.s3g.s3g-dsp.ambi-horizon-encoder-64
        COMMAND ${CMAKE_COMMAND}
          "-DREPORT=${S3G_HORIZON_ALLOC_REPORT}"
          -DREQUIRE_ZERO_ALLOC=TRUE
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_horizon_realtime_report.cmake
        DEPENDS
          s3g_ambi_horizon_encoder_clap
          s3g_clap_realtime_audit
          s3g_realtime_alloc_probe
        COMMENT "Allocation-probing Horizon underrun scenarios")
      add_dependencies(audit_ambi_horizon
        audit_ambi_horizon_alloc)
    endif()
  endif()

  add_custom_target(audit_ambi_cartography_encoder
    COMMAND $<TARGET_FILE:s3g_ambi_cartography_encoder_smoke>
    COMMAND $<TARGET_FILE:s3g_ambi_cartography_encoder_clap_smoke>
      $<TARGET_FILE:s3g_ambi_cartography_encoder_clap>
      org.s3g.s3g-dsp.ambi-cartography-encoder-64
    DEPENDS
      s3g_ambi_cartography_encoder_smoke
      s3g_ambi_cartography_encoder_clap_smoke
      s3g_ambi_cartography_encoder_clap
    COMMENT "Validating Cartography propagation, macro field, CLAP state, and routing"
    VERBATIM)

  # Discover the module targets contributed by each already-configured CLAP
  # subdirectory so release checks cannot run against a partial build.
  get_property(S3G_CONFIGURED_SUBDIRECTORIES DIRECTORY PROPERTY SUBDIRECTORIES)
  set(S3G_MANIFEST_CLAP_TARGETS)
  foreach(S3G_CONFIGURED_SUBDIRECTORY IN LISTS S3G_CONFIGURED_SUBDIRECTORIES)
    if(S3G_CONFIGURED_SUBDIRECTORY MATCHES "/plugins/clap_[^/]+$")
      get_property(S3G_DIRECTORY_TARGETS
        DIRECTORY "${S3G_CONFIGURED_SUBDIRECTORY}" PROPERTY BUILDSYSTEM_TARGETS)
      list(APPEND S3G_MANIFEST_CLAP_TARGETS ${S3G_DIRECTORY_TARGETS})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES S3G_MANIFEST_CLAP_TARGETS)

  find_program(S3G_CLAP_VALIDATOR_EXECUTABLE NAMES clap-validator)
  if(S3G_CLAP_VALIDATOR_EXECUTABLE)

    if(APPLE)
      set(S3G_SPEAKER_DECODER_VALIDATOR_PATH
        $<TARGET_BUNDLE_DIR:s3g_ambi_speaker_decoder_clap>)
      set(S3G_STOCHASTIC_ENCODER_VALIDATOR_PATH
        $<TARGET_BUNDLE_DIR:s3g_ambi_stochastic_encoder_clap>)
    else()
      set(S3G_SPEAKER_DECODER_VALIDATOR_PATH
        $<TARGET_FILE:s3g_ambi_speaker_decoder_clap>)
      set(S3G_STOCHASTIC_ENCODER_VALIDATOR_PATH
        $<TARGET_FILE:s3g_ambi_stochastic_encoder_clap>)
    endif()
    add_custom_target(audit_clap_state_validator
      COMMAND ${S3G_CLAP_VALIDATOR_EXECUTABLE} validate
        --no-parallel --only-failed
        --test-filter state-reproducibility|state-buffered
        ${S3G_SPEAKER_DECODER_VALIDATOR_PATH}
      COMMAND ${S3G_CLAP_VALIDATOR_EXECUTABLE} validate
        --no-parallel --only-failed
        --test-filter param-conversions|state-reproducibility|state-buffered
        ${S3G_STOCHASTIC_ENCODER_VALIDATOR_PATH}
      DEPENDS
        s3g_ambi_speaker_decoder_clap
        s3g_ambi_stochastic_encoder_clap
      COMMENT "Validating Speaker Decoder and Stochastic parameter/state contracts"
      VERBATIM)

    if(Python3_Interpreter_FOUND)
      add_custom_target(audit_clap_validator_manifest
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-clap-validator-manifest.py
          --validator ${S3G_CLAP_VALIDATOR_EXECUTABLE}
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
          --build-root ${CMAKE_BINARY_DIR}/plugins
          --output ${CMAKE_BINARY_DIR}/clap-validator-manifest.json
          --timeout-seconds 180 --jobs 4
        DEPENDS ${S3G_MANIFEST_CLAP_TARGETS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running isolated clap-validator coverage for every manifest bundle"
        VERBATIM)

      add_custom_target(audit_clap_validator_non_nim
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-clap-validator-manifest.py
          --validator ${S3G_CLAP_VALIDATOR_EXECUTABLE}
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/scripts/clap-bundles.tsv
          --build-root ${CMAKE_BINARY_DIR}/plugins
          --exclude-filter "(no-input-mixer-8ch|nim-gesture)$"
          --output ${CMAKE_BINARY_DIR}/clap-validator-non-nim.json
          --timeout-seconds 180 --jobs 4
        DEPENDS ${S3G_MANIFEST_CLAP_TARGETS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running isolated clap-validator coverage for every non-NIM bundle"
        VERBATIM)
    endif()
  endif()

  add_custom_target(audit_spectral_spray_routing
    COMMAND $<TARGET_FILE:s3g_spectral_spray_clap_routing_smoke>
      $<TARGET_FILE:s3g_spectral_spray_clap>
      org.s3g.s3g-dsp.spectral-spray 2 16
    COMMAND $<TARGET_FILE:s3g_spectral_spray_clap_routing_smoke>
      $<TARGET_FILE:s3g_8ch_spectral_spray_clap>
      org.s3g.s3g-dsp.8ch-spectral-spray 8 16
    DEPENDS
      s3g_spectral_spray_clap_routing_smoke
      s3g_spectral_spray_clap
      s3g_8ch_spectral_spray_clap
    COMMENT "Checking Spectral Spray routing in a wider host buffer")

  add_custom_target(audit_gui_layout_contract
    COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
    DEPENDS
      s3g_gui_layout_contract_smoke
      s3g_ambi_stochastic_encoder_clap
      s3g_ambi_wrangler_encoder_clap
      s3g_ambi_pulsar_encoder_clap
    COMMENT "Building and validating shared GUI layout-contract pilots")

  add_custom_target(audit_parameter_surface
    COMMAND $<TARGET_FILE:s3g_parameter_surface_smoke>
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_stochastic_encoder_clap>
      org.s3g.s3g-dsp.ambi-stochastic-encoder-64 43 44
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_wrangler_encoder_clap>
      org.s3g.s3g-dsp.ambi-wrangler-encoder-64 63 64
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_water_encoder_clap>
      org.s3g.s3g-dsp.ambi-water-encoder-64 43 44
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_wind_encoder_clap>
      org.s3g.s3g-dsp.ambi-wind-encoder-64 55 56
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_pyrosphere_encoder_clap>
      org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64 55 56
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_cryosphere_encoder_clap>
      org.s3g.s3g-dsp.ambi-cryosphere-encoder-64 43 44
    COMMAND $<TARGET_FILE:s3g_parameter_surface_clap_smoke>
      $<TARGET_FILE:s3g_ambi_insect_encoder_clap>
      org.s3g.s3g-dsp.ambi-insect-encoder-64 42 43
    DEPENDS
      s3g_parameter_surface_smoke
      s3g_parameter_surface_clap_smoke
      s3g_ambi_stochastic_encoder_clap
      s3g_ambi_wrangler_encoder_clap
      s3g_ambi_water_encoder_clap
      s3g_ambi_wind_encoder_clap
      s3g_ambi_pyrosphere_encoder_clap
      s3g_ambi_cryosphere_encoder_clap
      s3g_ambi_insect_encoder_clap
    COMMENT "Validating shared Parameter Surface DSP, CLAP parameters, and state")

  add_custom_target(audit_preset_output_preservation
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_accelerometer_field_encoder_clap>
      org.s3g.s3g-dsp.accelerometer-field-encoder-16 30 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_cryosphere_encoder_clap>
      org.s3g.s3g-dsp.ambi-cryosphere-encoder-64 34 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_insect_encoder_clap>
      org.s3g.s3g-dsp.ambi-insect-encoder-64 32 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_neural_ecology_clap>
      org.s3g.s3g-dsp.ambi-neural-ecology-64 33 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_pulsar_encoder_clap>
      org.s3g.s3g-dsp.ambi-pulsar-encoder-64 48 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_pyrosphere_encoder_clap>
      org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64 32 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_water_encoder_clap>
      org.s3g.s3g-dsp.ambi-water-encoder-64 34 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_wind_encoder_clap>
      org.s3g.s3g-dsp.ambi-wind-encoder-64 32 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_ambi_wrangler_encoder_clap>
      org.s3g.s3g-dsp.ambi-wrangler-encoder-64 32 1 1
    COMMAND $<TARGET_FILE:s3g_preset_output_preservation_clap_smoke>
      $<TARGET_FILE:s3g_psd_raw_field_clap>
      org.s3g.s3g-dsp.fault 17 50 1
    DEPENDS
      s3g_preset_output_preservation_clap_smoke
      s3g_accelerometer_field_encoder_clap
      s3g_ambi_cryosphere_encoder_clap
      s3g_ambi_insect_encoder_clap
      s3g_ambi_neural_ecology_clap
      s3g_ambi_pulsar_encoder_clap
      s3g_ambi_pyrosphere_encoder_clap
      s3g_ambi_water_encoder_clap
      s3g_ambi_wind_encoder_clap
      s3g_ambi_wrangler_encoder_clap
      s3g_psd_raw_field_clap
    COMMENT "Checking that factory presets preserve user output levels")

  if(APPLE)
    add_custom_target(audit_gui_encoder_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_point_encoder_clap> org.s3g.s3g-dsp.ambi-point-encoder-64 900 716
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_cloud_encoder_clap> org.s3g.s3g-dsp.ambi-cloud-encoder-64 900 672
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_cartography_encoder_clap> org.s3g.s3g-dsp.ambi-cartography-encoder-64 1356 760 "s3g Ambi Encoder Cartography" responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_path_encoder_clap> org.s3g.s3g-dsp.ambi-path-encoder-64 900 792
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_terrain_navigator_clap> org.s3g.s3g-dsp.ambi-terrain-navigator-64 900 792
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_ray_encoder_clap> org.s3g.s3g-dsp.ambi-ray-encoder 1040 628
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_ray_bilocation_encoder_clap> org.s3g.s3g-dsp.ambi-ray-bilocation-encoder 1240 908
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_insect_encoder_clap> org.s3g.s3g-dsp.ambi-insect-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_accelerometer_field_encoder_clap> org.s3g.s3g-dsp.accelerometer-field-encoder-16 1160 760 "s3g Ambi Encoder Modal" responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_encoder_medium_clap> org.s3g.s3g-dsp.ambi-encoder-medium-16 920 680 "s3g Ambi Encoder Medium" responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_neural_ecology_clap> org.s3g.s3g-dsp.ambi-neural-ecology-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_pulsar_encoder_clap> org.s3g.s3g-dsp.ambi-pulsar-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_stochastic_encoder_clap> org.s3g.s3g-dsp.ambi-stochastic-encoder-64 1160 860
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_vot_encoder_clap> org.s3g.s3g-dsp.ambi-vot-encoder-64 1160 894
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_vox_encoder_clap> org.s3g.s3g-dsp.ambi-vox-encoder-64 1160 920
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_water_encoder_clap> org.s3g.s3g-dsp.ambi-water-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_pyrosphere_encoder_clap> org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_cryosphere_encoder_clap> org.s3g.s3g-dsp.ambi-cryosphere-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_wave_terrain_encoder_clap> org.s3g.s3g-dsp.ambi-wave-terrain-encoder-64 1158 828
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_wind_encoder_clap> org.s3g.s3g-dsp.ambi-wind-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_wrangler_encoder_clap> org.s3g.s3g-dsp.ambi-wrangler-encoder-64 1160 858
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_ambi_point_encoder_clap
        s3g_ambi_cloud_encoder_clap
        s3g_ambi_cartography_encoder_clap
        s3g_ambi_path_encoder_clap
        s3g_ambi_terrain_navigator_clap
        s3g_ambi_ray_encoder_clap
        s3g_ambi_ray_bilocation_encoder_clap
        s3g_ambi_insect_encoder_clap
        s3g_accelerometer_field_encoder_clap
        s3g_ambi_encoder_medium_clap
        s3g_ambi_neural_ecology_clap
        s3g_ambi_pulsar_encoder_clap
        s3g_ambi_stochastic_encoder_clap
        s3g_ambi_vot_encoder_clap
        s3g_ambi_vox_encoder_clap
        s3g_ambi_water_encoder_clap
        s3g_ambi_pyrosphere_encoder_clap
        s3g_ambi_cryosphere_encoder_clap
        s3g_ambi_wave_terrain_encoder_clap
        s3g_ambi_wind_encoder_clap
        s3g_ambi_wrangler_encoder_clap
      COMMENT "Rendering and validating every encoder-family GUI")

    add_custom_target(audit_gui_decoder_family
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_head_decoder_clap> org.s3g.s3g-dsp.ambisonic-head-decoder 930 720 "s3g Ambi Decoder " responsive
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_stereo_decoder_clap> org.s3g.s3g-dsp.ambisonic-stereo-decoder 940 650 "s3g Ambi Decoder " responsive
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_sub_decoder_clap> org.s3g.s3g-dsp.ambisonic-sub-decoder 600 360 "s3g Ambi Decoder " responsive
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_adaptive_decoder_clap> org.s3g.s3g-dsp.ambi-adaptive-decoder-64 900 620 "s3g Ambi Decoder " responsive
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_object_decoder_clap> org.s3g.s3g-dsp.ambi-object-decoder-64 900 620 "s3g Ambi Decoder " responsive
      COMMAND $<TARGET_FILE:s3g_decoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_speaker_decoder_clap> org.s3g.s3g-dsp.ambi-speaker-decoder-64 900 620 "s3g Ambi Decoder " responsive
      DEPENDS
        s3g_decoder_family_gui_smoke
        s3g_ambisonic_head_decoder_clap
        s3g_ambisonic_stereo_decoder_clap
        s3g_ambisonic_sub_decoder_clap
        s3g_ambi_adaptive_decoder_clap
        s3g_ambi_object_decoder_clap
        s3g_ambi_speaker_decoder_clap
      COMMENT "Rendering and validating every decoder-family GUI")

    add_custom_target(audit_gui_panner_family
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_layout_panner_clap> org.s3g.s3g-dsp.layout-panner 900 720 "s3g Panner " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_dbap_panner_clap> org.s3g.s3g-dsp.dbap-panner 900 720 "s3g Panner " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_lbap_panner_clap> org.s3g.s3g-dsp.lbap-panner 900 720 "s3g Panner " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_vbap_panner_clap> org.s3g.s3g-dsp.vbap-panner 900 720 "s3g Panner " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_layout_panner_clap
        s3g_dbap_panner_clap
        s3g_lbap_panner_clap
        s3g_vbap_panner_clap
      COMMENT "Rendering and validating every Panner-family GUI")

    add_custom_target(audit_gui_processor_family
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_8ch_delay_processor_clap> org.s3g.s3g-dsp.delay-processor-8ch 1356 696 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_delay_processor_clap> org.s3g.s3g-dsp.delay-processor-24ch 1356 696 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_buffer_processor_clap> org.s3g.s3g-dsp.buffer-processor 760 584 "s3g Processor " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_wave_geometry_processor_clap> org.s3g.s3g-dsp.wave-geometry-processor 1356 788 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_loop_processor_clap> org.s3g.s3g-dsp.loop-processor-8ch 920 640 "s3g Processor " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_multi_loop_processor_clap> org.s3g.s3g-dsp.multi-loop-processor-8ch 920 674 "s3g Processor " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_psd_raw_field_clap> org.s3g.s3g-dsp.fault 1356 720 "s3g Processor " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_grain_processor_clap> org.s3g.s3g-dsp.ambi-grain-processor 920 640 "s3g Processor " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_no_input_mixer_clap> org.s3g.s3g-dsp.no-input-mixer-8ch 1356 820 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_spectral_topology_processor_clap> org.s3g.s3g-dsp.spectral-topology-processor 1356 820 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_spectral_topology_processor_clap> org.s3g.s3g-dsp.spectral-topology-processor-24ch 1356 820 "s3g Processor " responsive-wide
      DEPENDS
        s3g_encoder_family_gui_smoke
        s3g_8ch_delay_processor_clap
        s3g_24ch_delay_processor_clap
        s3g_buffer_processor_clap
        s3g_wave_geometry_processor_clap
        s3g_loop_processor_clap
        s3g_multi_loop_processor_clap
        s3g_psd_raw_field_clap
        s3g_ambi_grain_processor_clap
        s3g_no_input_mixer_clap
        s3g_spectral_topology_processor_clap
        s3g_24ch_spectral_topology_processor_clap
      COMMENT "Rendering and validating every Processor-family GUI")

    if(S3G_BUILD_FUTURE_COMPONENTS)
      add_custom_target(audit_gui_processor_feedback_shift
        COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_feedback_shift_clap> org.s3g.s3g-dsp.feedback-shift 1100 760 "s3g Processor " responsive
        DEPENDS
          s3g_encoder_family_gui_smoke
          s3g_feedback_shift_clap
        COMMENT "Rendering and validating Processor Feedback Shift")
      add_dependencies(audit_gui_processor_family
        audit_gui_processor_feedback_shift)
    endif()

    add_custom_target(audit_gui_macro_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_delay_clap> org.s3g.s3g-dsp.macro-delay-8ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_delay_clap> org.s3g.s3g-dsp.macro-delay-24ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_pitch_clap> org.s3g.s3g-dsp.macro-pitch-8ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_pitch_clap> org.s3g.s3g-dsp.macro-pitch-24ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_shred_mono_clap> org.s3g.s3g-dsp.macro-shred-mono 416 498 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_shred_clap> org.s3g.s3g-dsp.macro-shred-8ch 760 620 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_shred_clap> org.s3g.s3g-dsp.macro-shred-24ch 760 620 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_fracture_mono_clap> org.s3g.s3g-dsp.macro-fracture-mono 416 498 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_fracture_clap> org.s3g.s3g-dsp.macro-fracture-8ch 760 620 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_fracture_clap> org.s3g.s3g-dsp.macro-fracture-24ch 760 620 "s3g Macro " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_macro_delay_clap
        s3g_24ch_macro_delay_clap
        s3g_macro_pitch_clap
        s3g_24ch_macro_pitch_clap
        s3g_macro_shred_mono_clap
        s3g_macro_shred_clap
        s3g_24ch_macro_shred_clap
        s3g_macro_fracture_mono_clap
        s3g_macro_fracture_clap
        s3g_24ch_macro_fracture_clap
      COMMENT "Rendering and validating every Macro-family GUI")

    add_custom_target(audit_macro_shred_circuits
      COMMAND $<TARGET_FILE:s3g_macro_shred_clap_smoke>
        $<TARGET_FILE:s3g_macro_shred_mono_clap>
        org.s3g.s3g-dsp.macro-shred-mono
      COMMAND $<TARGET_FILE:s3g_macro_shred_clap_smoke>
        $<TARGET_FILE:s3g_macro_shred_clap>
        org.s3g.s3g-dsp.macro-shred-8ch
      COMMAND $<TARGET_FILE:s3g_macro_shred_clap_smoke>
        $<TARGET_FILE:s3g_24ch_macro_shred_clap>
        org.s3g.s3g-dsp.macro-shred-24ch
      DEPENDS
        s3g_macro_shred_clap_smoke
        s3g_macro_shred_mono_clap
        s3g_macro_shred_clap
        s3g_24ch_macro_shred_clap
      COMMENT "Validating Macro Shred circuits, parameters, and state migration")

    add_custom_target(audit_macro_fracture_processors
      COMMAND $<TARGET_FILE:s3g_macro_fracture_clap_smoke>
        $<TARGET_FILE:s3g_macro_fracture_mono_clap>
        org.s3g.s3g-dsp.macro-fracture-mono 1
      COMMAND $<TARGET_FILE:s3g_macro_fracture_clap_smoke>
        $<TARGET_FILE:s3g_macro_fracture_clap>
        org.s3g.s3g-dsp.macro-fracture-8ch 8
      COMMAND $<TARGET_FILE:s3g_macro_fracture_clap_smoke>
        $<TARGET_FILE:s3g_24ch_macro_fracture_clap>
        org.s3g.s3g-dsp.macro-fracture-24ch 24
      DEPENDS
        s3g_macro_fracture_clap_smoke
        s3g_macro_fracture_mono_clap
        s3g_macro_fracture_clap
        s3g_24ch_macro_fracture_clap
      COMMENT "Validating Macro Fracture processors, ports, parameters, and state")

    add_custom_target(audit_gui_array_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_hpf_16_clap> org.s3g.s3g-dsp.array-hpf-16 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_hpf_26_clap> org.s3g.s3g-dsp.array-hpf-26 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_hpf_32_clap> org.s3g.s3g-dsp.array-hpf-32 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_hpf_64_clap> org.s3g.s3g-dsp.array-hpf-64 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_delay_16_clap> org.s3g.s3g-dsp.array-delay-16 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_delay_26_clap> org.s3g.s3g-dsp.array-delay-26 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_delay_32_clap> org.s3g.s3g-dsp.array-delay-32 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_delay_64_clap> org.s3g.s3g-dsp.array-delay-64 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_trim_16_clap> org.s3g.s3g-dsp.array-trim-16 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_trim_26_clap> org.s3g.s3g-dsp.array-trim-26 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_trim_32_clap> org.s3g.s3g-dsp.array-trim-32 720 388 "s3g Array " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_array_trim_64_clap> org.s3g.s3g-dsp.array-trim-64 720 388 "s3g Array " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_array_hpf_16_clap
        s3g_array_hpf_26_clap
        s3g_array_hpf_32_clap
        s3g_array_hpf_64_clap
        s3g_array_delay_16_clap
        s3g_array_delay_26_clap
        s3g_array_delay_32_clap
        s3g_array_delay_64_clap
        s3g_array_trim_16_clap
        s3g_array_trim_26_clap
        s3g_array_trim_32_clap
        s3g_array_trim_64_clap
      COMMENT "Rendering and validating every Array-family GUI")

    add_custom_target(audit_gui_transform_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_rotate_clap> org.s3g.s3g-dsp.ambisonic-rotate-64 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_rotate_64_clap> org.s3g.s3g-dsp.ambi-group-rotate-64 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_rotate_128_clap> org.s3g.s3g-dsp.ambi-group-rotate-128 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_depth_16_clap> org.s3g.s3g-dsp.ambi-depth-16 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_depth_64_clap> org.s3g.s3g-dsp.ambi-group-depth-64 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_depth_128_clap> org.s3g.s3g-dsp.ambi-group-depth-128 820 496 "s3g Ambi Transform " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_order_band_tool_clap> org.s3g.s3g-dsp.ambisonic-order-band-tool-64 820 496 "s3g Ambi Transform " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_ambisonic_rotate_clap
        s3g_ambi_group_rotate_64_clap
        s3g_ambi_group_rotate_128_clap
        s3g_ambi_depth_16_clap
        s3g_ambi_group_depth_64_clap
        s3g_ambi_group_depth_128_clap
        s3g_ambisonic_order_band_tool_clap
      COMMENT "Rendering and validating every Ambi Transform-family GUI")

    add_custom_target(audit_gui_ambi_effect_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_dj_filter_clap> org.s3g.s3g-dsp.ambi-effect-dj-filter-64 820 640 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_delay_clap> org.s3g.s3g-dsp.ambi-effect-delay-64 820 640 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_pitch_clap> org.s3g.s3g-dsp.ambi-effect-pitch-64 820 640 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_gain_clap> org.s3g.s3g-dsp.ambi-effect-gain-64 820 640 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_resonance_print_clap> org.s3g.s3g-dsp.ambi-effect-resonance-print-64 920 820 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_partial_trace_clap> org.s3g.s3g-dsp.ambi-effect-partial-trace-64 920 820 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_response_trace_clap> org.s3g.s3g-dsp.ambi-effect-response-trace-64 920 820 "s3g Ambi Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_displacement_clap> org.s3g.s3g-dsp.ambi-effect-displacement-64 920 820 "s3g Ambi Effect " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_ambi_effect_dj_filter_clap
        s3g_ambi_effect_delay_clap
        s3g_ambi_effect_pitch_clap
        s3g_ambi_effect_gain_clap
        s3g_ambi_effect_resonance_print_clap
        s3g_ambi_effect_partial_trace_clap
        s3g_ambi_effect_response_trace_clap
        s3g_ambi_effect_displacement_clap
      COMMENT "Rendering and validating every Ambi Effect-family GUI")

    add_custom_target(audit_gui_matrix_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_group_matrix_32_clap> org.s3g.s3g-dsp.group-matrix-32 1040 648 "s3g Matrix " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_group_matrix_clap> org.s3g.s3g-dsp.group-matrix 1040 648 "s3g Matrix " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_matrix_clap> org.s3g.s3g-dsp.ambi-group-matrix 1040 648 "s3g Ambi Matrix " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_group_matrix_128_clap> org.s3g.s3g-dsp.ambi-group-matrix-128 1040 648 "s3g Ambi Matrix " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_group_matrix_32_clap
        s3g_group_matrix_clap
        s3g_ambi_group_matrix_clap
        s3g_ambi_group_matrix_128_clap
      COMMENT "Rendering and validating every Matrix-family GUI")

    add_custom_target(audit_gui_mixer_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_node_track_mixer_clap> org.s3g.s3g-dsp.node-bus-mixer 920 920 "s3g Mixer " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_node_track_mixer_clap> org.s3g.s3g-dsp.ambi-node-bus-mixer 920 920 "s3g Ambi Mixer " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_node_track_mixer_clap
        s3g_ambi_node_track_mixer_clap
      COMMENT "Rendering and validating every Mixer-family GUI")

    add_custom_target(audit_gui_compact_effect_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_spectral_spray_clap> org.s3g.s3g-dsp.spectral-spray 760 376 "s3g Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_8ch_spectral_spray_clap> org.s3g.s3g-dsp.8ch-spectral-spray 760 376 "s3g Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_shard_scatter_clap> org.s3g.s3g-dsp.shard-scatter 760 376 "s3g Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_orbit_delay_clap> org.s3g.s3g-dsp.orbit-delay 760 376 "s3g Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_cascade_taps_clap> org.s3g.s3g-dsp.cascade-taps 760 376 "s3g Effect " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_crcltr_clap> org.s3g.s3g-dsp.crcltr 760 376 "s3g Effect " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_spectral_spray_clap
        s3g_8ch_spectral_spray_clap
        s3g_shard_scatter_clap
        s3g_orbit_delay_clap
        s3g_cascade_taps_clap
        s3g_crcltr_clap
      COMMENT "Rendering and validating every compact Effect-family GUI")

    add_custom_target(audit_gui_output_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_mc_to_stereo_autogain_clap> org.s3g.s3g-dsp.mc-to-stereo-autogain 920 560 "s3g Output " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_mc_to_quad_autogain_clap> org.s3g.s3g-dsp.mc-to-quad-autogain 920 560 "s3g Output " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_sub_crossover_clap> org.s3g.s3g-dsp.sub-crossover 920 560 "s3g Output " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_mc_to_stereo_autogain_clap
        s3g_mc_to_quad_autogain_clap
        s3g_sub_crossover_clap
      COMMENT "Rendering and validating every Output-family GUI")

    add_custom_target(audit_gui_analyzer_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_multichannel_meter_clap> org.s3g.s3g-dsp.multichannel-meter-64 980 560 "s3g Analyzer " dynamic
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambisonic_energy_visualizer_clap> org.s3g.s3g-dsp.ambisonic-energy-visualizer-64 980 560 "s3g Analyzer " dynamic
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_multichannel_meter_clap
        s3g_ambisonic_energy_visualizer_clap
      COMMENT "Rendering and validating every dynamically resizable Analyzer-family GUI")

    add_custom_target(audit_gui_imprint
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_imprint_clap> org.s3g.s3g-dsp.ambi-imprint-64 900 480 "s3g Processor Ambi Imprint " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_ambi_imprint_clap
      COMMENT "Rendering and validating the Ambi Imprint GUI")

    add_custom_target(audit_gui_nim_gesture
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_nim_gesture_clap> org.s3g.s3g-dsp.nim-gesture 1180 820 "s3g Utility " responsive
      DEPENDS
        s3g_encoder_family_gui_smoke
        s3g_nim_gesture_clap
      COMMENT "Rendering and validating the NIM Gesture E16 overview GUI")

    if(S3G_BUILD_TRACKER_PREVIEW)
      add_custom_target(audit_gui_tracker
        COMMAND $<TARGET_FILE:s3g_tracker_clap_smoke> $<TARGET_FILE:s3g_tracker_clap> org.s3g.s3g-dsp.tracker 1320 780
        DEPENDS
          s3g_tracker_clap_smoke
          s3g_tracker_clap
        COMMENT "Rendering and validating the s3g Tracker GUI")
    endif()
  endif()

  if(APPLE AND BUILD_TESTING AND Python3_Interpreter_FOUND
      AND S3G_CLAP_VALIDATOR_EXECUTABLE
      AND TARGET s3g_realtime_alloc_probe
      AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_target(release_checks_non_nim
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-non-nim-release-checks.py
        --source-root ${CMAKE_CURRENT_SOURCE_DIR}
        --build-root ${CMAKE_BINARY_DIR}
        --ctest ${CMAKE_CTEST_COMMAND}
        --validator ${S3G_CLAP_VALIDATOR_EXECUTABLE}
        --realtime-audit $<TARGET_FILE:s3g_clap_realtime_audit>
        --allocation-probe-library $<TARGET_FILE:s3g_realtime_alloc_probe>
        --output ${CMAKE_BINARY_DIR}/non-nim-release-checks.json
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running the complete sequential non-NIM release checklist"
      USES_TERMINAL
      VERBATIM)
    add_dependencies(release_checks_non_nim
      ${S3G_NON_NIM_CORE_SMOKE_TARGETS}
      ${S3G_MANIFEST_CLAP_TARGETS}
      s3g_clap_realtime_audit
      s3g_realtime_alloc_probe
      s3g_clap_state_fixture_smoke
      s3g_panner_state_publication_clap_smoke)
  endif()
endif()

if(TARGET s3g_realtime_alloc_probe)
  function(s3g_attach_realtime_probe_release_dependencies)
    if(TARGET run_realtime_alloc_probe_tests)
      if(TARGET audit_clap_realtime_release_gate)
        add_dependencies(audit_clap_realtime_release_gate
          run_realtime_alloc_probe_tests)
      endif()
      if(TARGET release_checks_non_nim)
        add_dependencies(release_checks_non_nim
          run_realtime_alloc_probe_tests)
      endif()
    endif()
  endfunction()
  cmake_language(DEFER CALL s3g_attach_realtime_probe_release_dependencies)
endif()

if(S3G_BUILD_STANDALONE_APPS)
  add_subdirectory(standalone)
  add_subdirectory(apps/multi_loop_processor_standalone)
  add_subdirectory(apps/no_input_mixer_standalone)
  add_subdirectory(apps/vox_builder)
endif()
