function(add_s3g_spectral_topology_target target channels output_name bundle_id bundle_name)
  add_library(${target} MODULE
    s3g_spectral_topology_processor_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE
    S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT=${channels}
    S3G_SPECTRAL_TOPOLOGY_PLUGIN_ID="${bundle_id}"
    S3G_SPECTRAL_TOPOLOGY_PLUGIN_NAME="${bundle_name}"
  )

  if(APPLE)
    set_source_files_properties(s3g_spectral_topology_processor_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa" "-framework Accelerate")
  endif()

  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${output_name}"
  )

  if(APPLE)
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
      MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endfunction()

add_s3g_spectral_topology_target(
  s3g_spectral_topology_processor_clap
  8
  "s3g_spectral_topology_processor"
  "org.s3g.s3g-dsp.spectral-topology-processor"
  "s3g Spectral Topology Processor 8ch"
)

add_s3g_spectral_topology_target(
  s3g_24ch_spectral_topology_processor_clap
  24
  "s3g_24ch_spectral_topology_processor"
  "org.s3g.s3g-dsp.spectral-topology-processor-24ch"
  "s3g Spectral Topology Processor 24ch"
)
