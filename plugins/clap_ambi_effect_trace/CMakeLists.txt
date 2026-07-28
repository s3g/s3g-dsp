add_library(s3g_ambi_effect_partial_trace_clap MODULE
  s3g_ambi_effect_trace_clap.cpp)
target_compile_definitions(s3g_ambi_effect_partial_trace_clap
  PRIVATE S3G_PARTIAL_TRACE=1)

add_library(s3g_ambi_effect_response_trace_clap MODULE
  s3g_ambi_effect_trace_clap.cpp)
target_compile_definitions(s3g_ambi_effect_response_trace_clap
  PRIVATE S3G_PARTIAL_TRACE=0)

foreach(target IN ITEMS
    s3g_ambi_effect_partial_trace_clap
    s3g_ambi_effect_response_trace_clap)
  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  if(APPLE)
    set_source_files_properties(s3g_ambi_effect_trace_clap.cpp
      PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa" "-framework Accelerate")
  endif()
  set_target_properties(${target} PROPERTIES PREFIX "")
endforeach()

set_target_properties(s3g_ambi_effect_partial_trace_clap PROPERTIES
  OUTPUT_NAME "s3g_ambi_effect_partial_trace")
set_target_properties(s3g_ambi_effect_response_trace_clap PROPERTIES
  OUTPUT_NAME "s3g_ambi_effect_response_trace")

if(APPLE)
  set_target_properties(s3g_ambi_effect_partial_trace_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER
      "org.s3g.s3g-dsp.ambi-effect-partial-trace-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Effect Partial Trace 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
  set_target_properties(s3g_ambi_effect_response_trace_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER
      "org.s3g.s3g-dsp.ambi-effect-response-trace-64"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Effect Response Trace 64"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

