add_library(s3g_ambi_wrangler_encoder_clap MODULE
  s3g_ambi_wrangler_encoder_clap.cpp
)

if(BUILD_TESTING AND UNIX)
add_executable(s3g_ambi_wrangler_clap_smoke
  ../../tests/ambi_wrangler_clap_smoke.cpp
)
target_link_libraries(s3g_ambi_wrangler_clap_smoke PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(s3g_ambi_wrangler_clap_smoke PRIVATE ${S3G_CLAP_INCLUDE_DIR})
add_dependencies(s3g_ambi_wrangler_clap_smoke s3g_ambi_wrangler_encoder_clap)

if(BUILD_TESTING)
  add_test(NAME s3g_ambi_wrangler_clap_smoke
    COMMAND $<TARGET_FILE:s3g_ambi_wrangler_clap_smoke>
      $<TARGET_FILE:s3g_ambi_wrangler_encoder_clap>)
  set_tests_properties(s3g_ambi_wrangler_clap_smoke PROPERTIES
    LABELS "non_nim;clap;smoke" TIMEOUT 120)
endif()

endif()

target_link_libraries(s3g_ambi_wrangler_encoder_clap PRIVATE s3g_dsp)
if(S3G_CIRCUIT_ENCODERS_VSTGUI_AVAILABLE)
 s3g_enable_vstgui_gui(s3g_ambi_wrangler_encoder_clap
  "${PROJECT_SOURCE_DIR}/plugins/common/s3g_circuit_encoder_wrangler_canvas.inc"
  S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
 set_target_properties(s3g_ambi_wrangler_encoder_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_ambi_wrangler_encoder_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_ambi_wrangler_encoder_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_wrangler_encoder_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_wrangler_encoder_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_wrangler_encoder"
)

if(APPLE)
  set_target_properties(s3g_ambi_wrangler_encoder_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-wrangler-encoder-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Wrangler 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  )
endif()
