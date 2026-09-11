if(S3G_DECODERS_VSTGUI_AVAILABLE AND BUILD_TESTING AND S3G_BUILD_CLAP_PLUGIN)
  if(APPLE)
    add_executable(s3g_decoder_clap_parity_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_clap_parity_smoke.cpp")
    target_include_directories(s3g_decoder_clap_parity_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
    target_compile_features(s3g_decoder_clap_parity_smoke PRIVATE cxx_std_17)
  endif()
  add_executable(s3g_decoder_speaker_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_speaker_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_speaker_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_speaker_canvas_smoke PRIVATE S3G_DECODER_KIND=0
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambi_speaker_decoder/s3g_ambi_speaker_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kOutputParamId)
  s3g_enable_vstgui_gui(s3g_decoder_speaker_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_speaker_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_speaker_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_speaker_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_speaker_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_speaker_canvas_smoke>)
  set_tests_properties(s3g_decoder_speaker_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
  add_executable(s3g_decoder_object_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_object_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_object_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_object_canvas_smoke PRIVATE S3G_DECODER_KIND=1
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambi_object_decoder/s3g_ambi_object_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kOutputParamId)
  s3g_enable_vstgui_gui(s3g_decoder_object_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_object_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_object_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_object_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_object_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_object_canvas_smoke>)
  set_tests_properties(s3g_decoder_object_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
  add_executable(s3g_decoder_adaptive_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_adaptive_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_adaptive_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_adaptive_canvas_smoke PRIVATE S3G_DECODER_KIND=2
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambi_adaptive_decoder/s3g_ambi_adaptive_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kOutputParamId)
  s3g_enable_vstgui_gui(s3g_decoder_adaptive_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_adaptive_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_adaptive_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_adaptive_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_adaptive_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_adaptive_canvas_smoke>)
  set_tests_properties(s3g_decoder_adaptive_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
  add_executable(s3g_decoder_stereo_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_stereo_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_stereo_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_stereo_canvas_smoke PRIVATE S3G_DECODER_KIND=3
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kParamOutputGain)
  s3g_enable_vstgui_gui(s3g_decoder_stereo_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_stereo_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_stereo_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_stereo_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_stereo_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_stereo_canvas_smoke>)
  set_tests_properties(s3g_decoder_stereo_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
  add_executable(s3g_decoder_head_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_head_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_head_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_head_canvas_smoke PRIVATE S3G_DECODER_KIND=4
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kParamOutput)
  s3g_enable_vstgui_gui(s3g_decoder_head_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_head_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_head_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_head_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_head_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_head_canvas_smoke>)
  set_tests_properties(s3g_decoder_head_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
  add_executable(s3g_decoder_sub_canvas_smoke "${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp")
  target_link_libraries(s3g_decoder_sub_canvas_smoke PRIVATE s3g_dsp)
  target_include_directories(s3g_decoder_sub_canvas_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
  target_compile_definitions(s3g_decoder_sub_canvas_smoke PRIVATE S3G_DECODER_KIND=5
    S3G_ROUTING_FAMILY=9 S3G_TEST_ROUTING_EDITOR=decoder_canvas::Editor
    "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambisonic_sub_decoder/s3g_ambisonic_sub_decoder_clap.cpp\""
    S3G_TEST_OUTPUT_PARAM=kOutputParamId)
  s3g_enable_vstgui_gui(s3g_decoder_sub_canvas_smoke
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_decoder_sub_canvas.inc" S3G_ENABLE_VSTGUI_CANVAS_GUI)
  if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/decoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(s3g_decoder_sub_canvas_smoke PROPERTIES MACOSX_BUNDLE TRUE)
    target_link_libraries(s3g_decoder_sub_canvas_smoke PRIVATE "-framework Cocoa")
  endif()
  add_test(NAME s3g_decoder_sub_canvas_smoke COMMAND $<TARGET_FILE:s3g_decoder_sub_canvas_smoke>)
  set_tests_properties(s3g_decoder_sub_canvas_smoke PROPERTIES LABELS "non_nim;decoder;gui;smoke" TIMEOUT 180)
endif()
