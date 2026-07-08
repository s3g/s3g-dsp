function(add_s3g_macro_pitch_target target channels output_name bundle_id bundle_name description)
  add_library(${target} MODULE
    s3g_macro_pitch_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE
    S3G_MACRO_PITCH_CHANNEL_COUNT=${channels}
    S3G_MACRO_PITCH_PLUGIN_ID="${bundle_id}"
    S3G_MACRO_PITCH_PLUGIN_NAME="${bundle_name}"
    S3G_MACRO_PITCH_DESCRIPTION="${description}"
  )

  if(APPLE)
    set_source_files_properties(s3g_macro_pitch_clap.cpp PROPERTIES LANGUAGE OBJCXX)
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

add_s3g_macro_pitch_target(
  s3g_macro_pitch_clap
  8
  "s3g_macro_pitch"
  "org.s3g.s3g-dsp.macro-pitch-8ch"
  "s3g Macro Pitch 8ch"
  "8-channel macro pitch shifter with smoothed dual-reader time-domain pitch and lane relationship controls."
)

add_s3g_macro_pitch_target(
  s3g_24ch_macro_pitch_clap
  24
  "s3g_24ch_macro_pitch"
  "org.s3g.s3g-dsp.macro-pitch-24ch"
  "s3g Macro Pitch 24ch"
  "24-channel macro pitch shifter with smoothed dual-reader time-domain pitch and lane relationship controls."
)
