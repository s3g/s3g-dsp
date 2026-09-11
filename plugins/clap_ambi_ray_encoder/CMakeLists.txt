add_library(s3g_ambi_ray_encoder_clap MODULE
  s3g_ambi_ray_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_ray_encoder_clap PRIVATE s3g_dsp)
if(S3G_INPUT_ENCODERS_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_ambi_ray_encoder_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_input_encoder_ray_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_ambi_ray_encoder_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_ambi_ray_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  file(GLOB S3G_AMBI_RAY_ATLAS_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/*.s3gray"
    "${CMAKE_CURRENT_SOURCE_DIR}/atlas/manifest.json"
  )
  target_sources(s3g_ambi_ray_encoder_clap PRIVATE ${S3G_AMBI_RAY_ATLAS_FILES})
  set_source_files_properties(${S3G_AMBI_RAY_ATLAS_FILES} PROPERTIES
    HEADER_FILE_ONLY TRUE
    MACOSX_PACKAGE_LOCATION "Resources/Ray Atlas"
  )
  set_source_files_properties(s3g_ambi_ray_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_ray_encoder_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_ray_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_ray_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_ray_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-ray-encoder"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Ray 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.4.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4.0"
  )
endif()

if(WIN32 AND S3G_INPUT_ENCODERS_VSTGUI_AVAILABLE)
  file(GLOB ray_resources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/plugins/clap_ambi_ray_encoder/atlas/*.s3gray"
    "${PROJECT_SOURCE_DIR}/plugins/clap_ambi_ray_encoder/atlas/manifest.json")
  add_custom_command(TARGET s3g_ambi_ray_encoder_clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:s3g_ambi_ray_encoder_clap>/Resources/Ray Atlas"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ray_resources}
      "$<TARGET_FILE_DIR:s3g_ambi_ray_encoder_clap>/Resources/Ray Atlas"
    VERBATIM)
endif()
