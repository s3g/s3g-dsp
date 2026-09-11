# Source-level interaction tests compare the retained Cocoa implementation
# with the portable editor and exercise real CLAP state/automation/audio.
if(S3G_AMBI_BUS_VSTGUI_AVAILABLE AND BUILD_TESTING AND S3G_BUILD_CLAP_PLUGIN)
  if(APPLE)
    add_executable(s3g_ambi_bus_clap_parity_smoke
      "${PROJECT_SOURCE_DIR}/tests/ambi_bus_clap_parity_smoke.cpp")
    target_include_directories(s3g_ambi_bus_clap_parity_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
    target_link_libraries(s3g_ambi_bus_clap_parity_smoke PRIVATE ${CMAKE_DL_LIBS})
  endif()
  function(s3g_ambi_canvas_test name source canvas editor family)
    set(target "s3g_${name}_canvas_smoke")
    add_executable(${target} "${PROJECT_SOURCE_DIR}/tests/ambi_bus_canvas_smoke.cpp")
    target_link_libraries(${target} PRIVATE s3g_dsp)
    target_include_directories(${target} PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
    if(family EQUAL 2)
      set(output kParamOutputGain)
    else()
      set(output kParamOutput)
    endif()
    target_compile_definitions(${target} PRIVATE
      "S3G_TEST_ROUTING_SOURCE=\"../plugins/${source}\""
      S3G_TEST_ROUTING_EDITOR=${editor} S3G_ROUTING_FAMILY=${family}
      S3G_TEST_OUTPUT_PARAM=${output} ${ARGN})
    s3g_enable_vstgui_gui(${target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_${canvas}_canvas.inc"
      S3G_ENABLE_VSTGUI_CANVAS_GUI)
    if(APPLE)
      set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/ambi_bus_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
      set_target_properties(${target} PROPERTIES MACOSX_BUNDLE TRUE)
      target_link_libraries(${target} PRIVATE "-framework Cocoa")
    endif()
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES LABELS "non_nim;ambi_bus;gui;smoke" TIMEOUT 180)
  endfunction()
  s3g_ambi_canvas_test(ambi_group_matrix clap_ambi_group_matrix/s3g_ambi_group_matrix_clap.cpp
    group_matrix group_canvas::Editor 1 S3G_TEST_ROUTING_COCOA_VIEW=S3GAmbiGroupMatrixView)
  s3g_ambi_canvas_test(ambi_group_matrix_128 clap_ambi_group_matrix_128/s3g_ambi_group_matrix_128_clap.cpp
    group_matrix group_canvas::Editor 1 S3G_TEST_ROUTING_COCOA_VIEW=S3GAmbiGroupMatrix128View)
  s3g_ambi_canvas_test(ambi_node_bus clap_node_track_mixer/s3g_node_track_mixer_clap.cpp
    node_bus node_canvas::Editor 2 S3G_AMBI_NODE_TRACK_MIXER=1)
  s3g_ambi_canvas_test(ambi_rotate clap_ambisonic_rotate/s3g_ambisonic_rotate_clap.cpp
    ambi_rotate ambi_rotate_canvas::Editor 5 S3G_TEST_ROUTING_COCOA_VIEW=S3GAmbisonicRotateView)
  s3g_ambi_canvas_test(ambi_group_rotate_64 clap_ambi_group_rotate/s3g_ambi_group_rotate_clap.cpp
    ambi_rotate ambi_rotate_canvas::Editor 5 S3G_TEST_ROUTING_COCOA_VIEW=S3GAmbiGroupRotate64View)
  s3g_ambi_canvas_test(ambi_group_rotate_128 clap_ambi_group_rotate/s3g_ambi_group_rotate_clap.cpp
    ambi_rotate ambi_rotate_canvas::Editor 5 S3G_AMBI_GROUP_ROTATE_128=1 S3G_TEST_ROUTING_COCOA_VIEW=S3GAmbiGroupRotate128View)
  s3g_ambi_canvas_test(ambi_depth_16 clap_ambi_group_depth/s3g_ambi_group_depth_clap.cpp
    ambi_depth ambi_depth_canvas::Editor 6 S3G_AMBI_DEPTH_16=1)
  s3g_ambi_canvas_test(ambi_group_depth_64 clap_ambi_group_depth/s3g_ambi_group_depth_clap.cpp
    ambi_depth ambi_depth_canvas::Editor 6 )
  s3g_ambi_canvas_test(ambi_group_depth_128 clap_ambi_group_depth/s3g_ambi_group_depth_clap.cpp
    ambi_depth ambi_depth_canvas::Editor 6 S3G_AMBI_GROUP_DEPTH_128=1)
  s3g_ambi_canvas_test(ambi_order_band clap_ambisonic_order_band_tool/s3g_ambisonic_order_band_tool_clap.cpp
    ambi_order_band ambi_order_canvas::Editor 7 )
endif()
