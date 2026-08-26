add_library(s3g_processor_stack_clap MODULE
  s3g_processor_stack_clap.cpp
)

target_link_libraries(s3g_processor_stack_clap PRIVATE s3g_dsp)
target_include_directories(s3g_processor_stack_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(s3g_processor_stack_clap PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

set_target_properties(s3g_processor_stack_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_processor_stack"
)

if(APPLE)
  set_source_files_properties(s3g_processor_stack_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_processor_stack_clap PRIVATE
    "-framework Cocoa")
  set_target_properties(s3g_processor_stack_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.processor-stack"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Processor Stack 2"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.9.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.9.0"
  )
endif()
