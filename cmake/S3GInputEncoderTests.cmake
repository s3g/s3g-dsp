if(S3G_INPUT_ENCODERS_VSTGUI_AVAILABLE AND BUILD_TESTING AND S3G_BUILD_CLAP_PLUGIN)
  set(input_encoder_kind 0)
  foreach(name cloud path ray ray_bilocation)
    set(target s3g_input_encoder_${name}_canvas_smoke)
    add_executable(${target} "${PROJECT_SOURCE_DIR}/tests/input_encoder_canvas_smoke.cpp")
    target_link_libraries(${target} PRIVATE s3g_dsp)
    target_include_directories(${target} PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
    if(input_encoder_kind LESS 2)
      set(output kOutputParamId)
    else()
      set(output kParamOutput)
    endif()
    target_compile_definitions(${target} PRIVATE
      S3G_INPUT_ENCODER_KIND=${input_encoder_kind} S3G_ROUTING_FAMILY=10
      S3G_TEST_ROUTING_EDITOR=input_encoder_canvas::Editor
      "S3G_TEST_ROUTING_SOURCE=\"../plugins/clap_ambi_${name}_encoder/s3g_ambi_${name}_encoder_clap.cpp\""
      S3G_TEST_OUTPUT_PARAM=${output})
    s3g_enable_vstgui_gui(${target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_input_encoder_${name}_canvas.inc"
      S3G_ENABLE_VSTGUI_CANVAS_GUI)
    if(APPLE)
      set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/input_encoder_canvas_smoke.cpp" PROPERTIES LANGUAGE OBJCXX)
      set_target_properties(${target} PROPERTIES MACOSX_BUNDLE TRUE)
      target_link_libraries(${target} PRIVATE "-framework Cocoa")
    endif()
    if(input_encoder_kind GREATER 1)
      file(GLOB encoder_test_atlas CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/plugins/clap_ambi_ray_encoder/atlas/*.s3gray"
        "${PROJECT_SOURCE_DIR}/plugins/clap_ambi_ray_encoder/atlas/manifest.json")
      if(APPLE)
        set_source_files_properties(${encoder_test_atlas} PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources/Ray Atlas")
        target_sources(${target} PRIVATE ${encoder_test_atlas})
      elseif(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Resources/Ray Atlas"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${encoder_test_atlas} "$<TARGET_FILE_DIR:${target}>/Resources/Ray Atlas"
          VERBATIM)
      endif()
    endif()
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES LABELS "non_nim;encoder;gui;smoke" TIMEOUT 240)
    math(EXPR input_encoder_kind "${input_encoder_kind}+1")
  endforeach()
  if(APPLE)
    add_executable(s3g_input_encoder_clap_parity_smoke "${PROJECT_SOURCE_DIR}/tests/input_encoder_clap_parity_smoke.cpp")
    target_include_directories(s3g_input_encoder_clap_parity_smoke PRIVATE "${S3G_CLAP_INCLUDE_DIR}")
    target_compile_features(s3g_input_encoder_clap_parity_smoke PRIVATE cxx_std_17)
  endif()
endif()
