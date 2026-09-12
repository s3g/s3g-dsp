add_library(s3g_ambi_neural_ecology_clap MODULE
  s3g_ambi_neural_ecology_clap.cpp
)

target_link_libraries(s3g_ambi_neural_ecology_clap PRIVATE s3g_dsp)
if(S3G_CIRCUIT_ENCODERS_VSTGUI_AVAILABLE)
 s3g_enable_vstgui_gui(s3g_ambi_neural_ecology_clap
  "${PROJECT_SOURCE_DIR}/plugins/common/s3g_circuit_encoder_neural_canvas.inc"
  S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
 set_target_properties(s3g_ambi_neural_ecology_clap PROPERTIES SUFFIX ".clap")
endif()
target_include_directories(s3g_ambi_neural_ecology_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_ambi_neural_ecology_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_ambi_neural_ecology_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_ambi_neural_ecology_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_neural_ecology"
)

if(APPLE)
  set_target_properties(s3g_ambi_neural_ecology_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-neural-ecology-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Encoder Neural Ecology 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.8.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.8.0"
  )
endif()
