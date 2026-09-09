add_library(s3g_ambi_point_encoder_clap MODULE
  s3g_ambi_point_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_point_encoder_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_point_encoder_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_compile_features(s3g_ambi_point_encoder_clap PRIVATE cxx_std_17)

if(S3G_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_ambi_point_encoder_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_ambi_point_encoder_vstgui.cpp"
    S3G_ENABLE_VSTGUI_AMBI_POINT_GUI)
elseif(APPLE)
  target_compile_definitions(s3g_ambi_point_encoder_clap PRIVATE
    S3G_USE_LEGACY_COCOA_AMBI_POINT_GUI=1)
endif()

if(APPLE)
  set_source_files_properties(s3g_ambi_point_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_point_encoder_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_point_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_point_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_point_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-point-encoder-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Point 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
elseif(WIN32)
  set_target_properties(s3g_ambi_point_encoder_clap PROPERTIES
    SUFFIX ".clap")
endif()
