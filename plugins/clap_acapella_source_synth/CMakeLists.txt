add_library(s3g_acapella_source_synth_clap MODULE
  s3g_acapella_source_synth_clap.cpp
)

target_link_libraries(s3g_acapella_source_synth_clap PRIVATE s3g_dsp)
target_include_directories(s3g_acapella_source_synth_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_acapella_source_synth_clap PRIVATE
    -Wall -Wextra -Wpedantic
  )
endif()

set_target_properties(s3g_acapella_source_synth_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_processor_articulator"
)

if(APPLE)
  set_source_files_properties(s3g_acapella_source_synth_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_acapella_source_synth_clap PRIVATE
    "-framework Cocoa" "-framework Accelerate")
  set_target_properties(s3g_acapella_source_synth_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.processor-articulator"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Processor Articulator"
    MACOSX_BUNDLE_BUNDLE_VERSION "3.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "3.1.0"
  )
endif()
