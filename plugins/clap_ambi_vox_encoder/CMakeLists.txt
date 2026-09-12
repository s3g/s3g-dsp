add_library(s3g_ambi_vox_encoder_clap MODULE
  s3g_ambi_vox_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_vox_encoder_clap PRIVATE s3g_dsp)
if(S3G_COMPLEX_PROCESSORS_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_ambi_vox_encoder_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_complex_processor_vox_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_ambi_vox_encoder_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_ambi_vox_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(TARGET world::core)
  target_link_libraries(s3g_ambi_vox_encoder_clap PRIVATE world::core)
  target_compile_definitions(s3g_ambi_vox_encoder_clap PRIVATE S3G_HAS_WORLD=1)
  set(world_license "${PROJECT_BINARY_DIR}/vstgui-resources/WORLD-LICENSE.txt")
  configure_file("${world_SOURCE_DIR}/LICENSE.txt" "${world_license}" COPYONLY)
  if(APPLE)
    set_source_files_properties("${world_license}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
    target_sources(s3g_ambi_vox_encoder_clap PRIVATE "${world_license}")
  elseif(WIN32)
    add_custom_command(TARGET s3g_ambi_vox_encoder_clap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:s3g_ambi_vox_encoder_clap>/Resources/Licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${world_license}"
        "$<TARGET_FILE_DIR:s3g_ambi_vox_encoder_clap>/Resources/Licenses/WORLD-LICENSE.txt"
      VERBATIM)
  endif()
endif()

if(APPLE)
  set_source_files_properties(s3g_ambi_vox_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_vox_encoder_clap PRIVATE "-framework Cocoa" "-framework Accelerate")
endif()

set_target_properties(s3g_ambi_vox_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_vox_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_vox_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-vox-encoder-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Vox 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

if(APPLE AND TARGET world::core)
  add_executable(s3g_ambi_vox_playback_probe
    s3g_ambi_vox_encoder_clap.cpp
  )
  target_link_libraries(s3g_ambi_vox_playback_probe PRIVATE
    s3g_dsp
    world::core
    "-framework Cocoa"
    "-framework Accelerate"
  )
  target_include_directories(s3g_ambi_vox_playback_probe PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(s3g_ambi_vox_playback_probe PRIVATE
    S3G_HAS_WORLD=1
    S3G_AMBI_VOX_PLAYBACK_PROBE=1
  )
  if(BUILD_TESTING)
    set(S3G_AMBI_VOX_TEST_VOICEBANK
      "${PROJECT_SOURCE_DIR}/examples/voicebanks/s3g-demo-synthetic")
    if(NOT EXISTS "${S3G_AMBI_VOX_TEST_VOICEBANK}/voicebank.json")
      message(FATAL_ERROR
        "Ambi Vox playback test voicebank is missing: ${S3G_AMBI_VOX_TEST_VOICEBANK}")
    endif()
    add_test(NAME s3g_ambi_vox_playback_probe
      COMMAND $<TARGET_FILE:s3g_ambi_vox_playback_probe>
        ${S3G_AMBI_VOX_TEST_VOICEBANK})
    set_tests_properties(s3g_ambi_vox_playback_probe PROPERTIES
      LABELS "non_nim;clap;slow" TIMEOUT 300)
  endif()
endif()
