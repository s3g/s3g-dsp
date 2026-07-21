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

add_executable(s3g_3oafx_displacement_smoke tests/3oafx_displacement_smoke.cpp)
target_link_libraries(s3g_3oafx_displacement_smoke PRIVATE s3g_dsp)

add_executable(s3g_ambi_imprint_safety_smoke tests/ambi_imprint_safety_smoke.cpp)
target_link_libraries(s3g_ambi_imprint_safety_smoke PRIVATE s3g_dsp)
if(APPLE)
  target_link_libraries(s3g_ambi_imprint_safety_smoke PRIVATE "-framework Accelerate")
endif()

add_executable(s3g_ambi_ray_encoder_smoke tests/ambi_ray_encoder_smoke.cpp)
target_link_libraries(s3g_ambi_ray_encoder_smoke PRIVATE s3g_dsp)

add_executable(s3g_vox_builder_smoke tests/vox_builder_smoke.cpp)
target_link_libraries(s3g_vox_builder_smoke PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_smoke tests/psd_raw_field_smoke.cpp)
target_link_libraries(s3g_psd_raw_field_smoke PRIVATE s3g_dsp)

add_executable(s3g_psd_raw_field_parameter_audit tests/psd_raw_field_parameter_audit.cpp)
target_link_libraries(s3g_psd_raw_field_parameter_audit PRIVATE s3g_dsp)

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

if(S3G_BUILD_CLAP_PLUGIN)
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

  add_subdirectory(plugins/clap_24ch_passthrough)
  add_subdirectory(plugins/clap_delay_processor)
  add_subdirectory(plugins/clap_mc_to_stereo_autogain)
  add_subdirectory(plugins/clap_mc_to_quad_autogain)
  add_subdirectory(plugins/clap_3oafx_single_effects)
  add_subdirectory(plugins/clap_3oafx_displacement)
  add_subdirectory(plugins/clap_3oafx_point_encoder)
  add_subdirectory(plugins/clap_ambi_cloud_encoder)
  add_subdirectory(plugins/clap_ambi_terrain_navigator)
  add_subdirectory(plugins/clap_ambi_vot_encoder)
  add_subdirectory(plugins/clap_ambi_vox_encoder)
  add_subdirectory(plugins/clap_ambi_wave_terrain_encoder)
  add_subdirectory(plugins/clap_ambi_stochastic_encoder)
  add_subdirectory(plugins/clap_ambi_wrangler_encoder)
  add_subdirectory(plugins/clap_ambi_wind_encoder)
  add_subdirectory(plugins/clap_ambi_imprint)
  add_subdirectory(plugins/clap_ambi_ray_encoder)
  add_subdirectory(plugins/clap_ambi_path_encoder)
  add_subdirectory(plugins/clap_3oafx_speaker_decoder)
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
endif()

if(S3G_BUILD_STANDALONE_APPS)
  add_subdirectory(apps/multi_loop_processor_standalone)
  add_subdirectory(apps/vox_builder)
endif()
