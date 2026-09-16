add_library(s3g_ambi_pyrosphere_encoder_clap MODULE
  s3g_ambi_pyrosphere_encoder_clap.cpp
)

target_link_libraries(s3g_ambi_pyrosphere_encoder_clap PRIVATE s3g_dsp)
if(S3G_ENVIRONMENT_ENCODERS_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_ambi_pyrosphere_encoder_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_environment_encoder_pyrosphere_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_ambi_pyrosphere_encoder_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_ambi_pyrosphere_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_ambi_pyrosphere_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_pyrosphere_encoder_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_pyrosphere_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_pyrosphere_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_pyrosphere_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Pyrosphere 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

if(WIN32 AND BUILD_TESTING)
  add_executable(s3g_pyrosphere_windows_float_mode
    "${PROJECT_SOURCE_DIR}/tests/pyrosphere_windows_float_mode.cpp")
  target_compile_features(s3g_pyrosphere_windows_float_mode PRIVATE cxx_std_17)
  target_include_directories(s3g_pyrosphere_windows_float_mode PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  add_dependencies(s3g_pyrosphere_windows_float_mode s3g_ambi_pyrosphere_encoder_clap)
  add_test(NAME s3g_pyrosphere_windows_float_mode
    COMMAND s3g_pyrosphere_windows_float_mode $<TARGET_FILE:s3g_ambi_pyrosphere_encoder_clap>)
  set_tests_properties(s3g_pyrosphere_windows_float_mode PROPERTIES SKIP_RETURN_CODE 77)
endif()
