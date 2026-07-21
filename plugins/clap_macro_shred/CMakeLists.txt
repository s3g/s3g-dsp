function(add_s3g_macro_shred_target target channels output_name bundle_id bundle_name description)
  add_library(${target} MODULE
    s3g_macro_shred_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE
    S3G_MACRO_SHRED_CHANNEL_COUNT=${channels}
    S3G_MACRO_SHRED_PLUGIN_ID="${bundle_id}"
    S3G_MACRO_SHRED_PLUGIN_NAME="${bundle_name}"
    S3G_MACRO_SHRED_DESCRIPTION="${description}"
  )

  if(APPLE)
    set_source_files_properties(s3g_macro_shred_clap.cpp PROPERTIES LANGUAGE OBJCXX)
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

add_s3g_macro_shred_target(
  s3g_macro_shred_mono_clap
  1
  "s3g_macro_shred_mono"
  "org.s3g.s3g-dsp.macro-shred-mono"
  "s3g Macro Shred Mono"
  "Mono pressure, wavefolding, and bounded resonant-feedback processor for voice and microphone input."
)

add_s3g_macro_shred_target(
  s3g_macro_shred_clap
  8
  "s3g_macro_shred"
  "org.s3g.s3g-dsp.macro-shred-8ch"
  "s3g Macro Shred 8ch"
  "8-channel macro shred processor with shared voice controls and smoothed lane relationships."
)

add_s3g_macro_shred_target(
  s3g_24ch_macro_shred_clap
  24
  "s3g_24ch_macro_shred"
  "org.s3g.s3g-dsp.macro-shred-24ch"
  "s3g Macro Shred 24ch"
  "24-channel macro shred processor with shared voice controls and smoothed lane relationships."
)
