function(add_s3g_tape_delay_target target channels output_name bundle_id bundle_name)
  add_library(${target} MODULE
    s3g_tape_delay_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE S3G_TAPE_DELAY_CHANNEL_COUNT=${channels})

  if(APPLE)
    set_source_files_properties(s3g_tape_delay_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa")
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

add_s3g_tape_delay_target(
  s3g_8ch_tape_delay_clap
  8
  "s3g_8ch_delay_processor"
  "org.s3g.s3g-dsp.delay-processor-8ch"
  "s3g Delay Processor 8ch"
)

add_s3g_tape_delay_target(
  s3g_24ch_tape_delay_clap
  24
  "s3g_24ch_delay_processor"
  "org.s3g.s3g-dsp.delay-processor-24ch"
  "s3g Delay Processor 24ch"
)
