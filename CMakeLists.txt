cmake_minimum_required(VERSION 3.20)

project(s3g_dsp VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(s3g_dsp INTERFACE)
target_include_directories(s3g_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp)

include(FetchContent)

add_executable(s3g_dsp_smoke tests/dsp_smoke.cpp)
target_link_libraries(s3g_dsp_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_dsp_smoke PRIVATE "-framework Accelerate")
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

add_executable(s3g_ambi_ray_bilocation_encoder_smoke tests/ambi_ray_bilocation_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_ray_bilocation_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_pulsar_encoder_smoke tests/pulsar_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_pulsar_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_neural_ecology_smoke tests/ambi_neural_ecology_smoke.cpp)
target_link_libraries(s3g_ambi_neural_ecology_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_insect_encoder_smoke tests/ambi_insect_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_insect_encoder_smoke PRIVATE s3g_dsp)

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

add_executable(s3g_environmental_encoder_smoke tests/environmental_encoder_smoke.cpp)
target_link_libraries(s3g_environmental_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_vox_builder_smoke tests/vox_builder_smoke.cpp)
target_link_libraries(s3g_vox_builder_smoke PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_smoke tests/psd_raw_field_smoke.cpp)
target_link_libraries(s3g_psd_raw_field_smoke PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_parameter_audit tests/psd_raw_field_parameter_audit.cpp)
target_link_libraries(s3g_psd_raw_field_parameter_audit PRIVATE s3g_dsp)

add_executable(s3g_gui_layout_contract_smoke tests/gui_layout_contract_smoke.cpp)

add_custom_target(audit_gui_style
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audit-gui-style.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running advisory CLAP GUI style audit")

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
  add_executable(s3g_parameter_surface_clap_smoke tests/parameter_surface_clap_smoke.cpp)
  target_include_directories(s3g_parameter_surface_clap_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_parameter_surface_clap_smoke PRIVATE ${CMAKE_DL_LIBS})

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
  add_subdirectory(plugins/clap_ambi_terrain_navigator)
  add_subdirectory(plugins/clap_ambi_vot_encoder)
  add_subdirectory(plugins/clap_ambi_vox_encoder)
  add_subdirectory(plugins/clap_ambi_wave_terrain_encoder)
  add_subdirectory(plugins/clap_ambi_stochastic_encoder)
  add_subdirectory(plugins/clap_ambi_wrangler_encoder)
  add_subdirectory(plugins/clap_ambi_wind_encoder)
  add_subdirectory(plugins/clap_ambi_water_encoder)
  add_subdirectory(plugins/clap_ambi_pyrosphere_encoder)
  add_subdirectory(plugins/clap_ambi_cryosphere_encoder)
  add_subdirectory(plugins/clap_ambi_insect_encoder)
  add_subdirectory(plugins/clap_accelerometer_field_encoder)
  add_subdirectory(plugins/clap_ambi_pulsar_encoder)
  add_subdirectory(plugins/clap_ambi_neural_ecology)
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

  if(APPLE)
    add_custom_target(audit_gui_encoder_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_point_encoder_clap> org.s3g.s3g-dsp.ambi-point-encoder-64 900 716
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_cloud_encoder_clap> org.s3g.s3g-dsp.ambi-cloud-encoder-64 900 672
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_path_encoder_clap> org.s3g.s3g-dsp.ambi-path-encoder-64 900 792
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_terrain_navigator_clap> org.s3g.s3g-dsp.ambi-terrain-navigator-64 900 792
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_ray_encoder_clap> org.s3g.s3g-dsp.ambi-ray-encoder 1040 628
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_ray_bilocation_encoder_clap> org.s3g.s3g-dsp.ambi-ray-bilocation-encoder 1240 908
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_insect_encoder_clap> org.s3g.s3g-dsp.ambi-insect-encoder-64 1160 858
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_accelerometer_field_encoder_clap> org.s3g.s3g-dsp.accelerometer-field-encoder-16 1160 760 "s3g Ambi Encoder Modal" responsive
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
        s3g_ambi_path_encoder_clap
        s3g_ambi_terrain_navigator_clap
        s3g_ambi_ray_encoder_clap
        s3g_ambi_ray_bilocation_encoder_clap
        s3g_ambi_insect_encoder_clap
        s3g_accelerometer_field_encoder_clap
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
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_spectral_topology_processor_clap> org.s3g.s3g-dsp.spectral-topology-processor 1356 950 "s3g Processor " responsive-wide
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_spectral_topology_processor_clap> org.s3g.s3g-dsp.spectral-topology-processor-24ch 1356 950 "s3g Processor " responsive-wide
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
        s3g_spectral_topology_processor_clap
        s3g_24ch_spectral_topology_processor_clap
      COMMENT "Rendering and validating every Processor-family GUI")

    add_custom_target(audit_gui_macro_family
      COMMAND $<TARGET_FILE:s3g_gui_layout_contract_smoke>
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_delay_clap> org.s3g.s3g-dsp.macro-delay-8ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_delay_clap> org.s3g.s3g-dsp.macro-delay-24ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_pitch_clap> org.s3g.s3g-dsp.macro-pitch-8ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_pitch_clap> org.s3g.s3g-dsp.macro-pitch-24ch 760 496 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_shred_mono_clap> org.s3g.s3g-dsp.macro-shred-mono 416 498 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_macro_shred_clap> org.s3g.s3g-dsp.macro-shred-8ch 760 620 "s3g Macro " responsive
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_24ch_macro_shred_clap> org.s3g.s3g-dsp.macro-shred-24ch 760 620 "s3g Macro " responsive
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
      COMMENT "Rendering and validating every Macro-family GUI")

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
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke> $<TARGET_FILE:s3g_ambi_effect_displacement_clap> org.s3g.s3g-dsp.ambi-effect-displacement-64 920 820 "s3g Ambi Effect " responsive
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_ambi_effect_dj_filter_clap
        s3g_ambi_effect_delay_clap
        s3g_ambi_effect_pitch_clap
        s3g_ambi_effect_gain_clap
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
      DEPENDS
        s3g_gui_layout_contract_smoke
        s3g_encoder_family_gui_smoke
        s3g_spectral_spray_clap
        s3g_8ch_spectral_spray_clap
        s3g_shard_scatter_clap
        s3g_orbit_delay_clap
        s3g_cascade_taps_clap
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
  endif()
endif()

if(S3G_BUILD_STANDALONE_APPS)
  add_subdirectory(apps/multi_loop_processor_standalone)
  add_subdirectory(apps/vox_builder)
endif()
