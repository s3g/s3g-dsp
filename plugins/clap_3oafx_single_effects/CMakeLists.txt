function(add_s3g_3oafx_single_target target kind output_name bundle_id bundle_name description)
  add_library(${target} MODULE
    s3g_3oafx_single_effect_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE
    S3G_3OAFX_SINGLE_KIND=${kind}
    S3G_3OAFX_SINGLE_PLUGIN_ID="${bundle_id}"
    S3G_3OAFX_SINGLE_PLUGIN_NAME="${bundle_name}"
    S3G_3OAFX_SINGLE_DESCRIPTION="${description}"
  )

  if(APPLE)
    set_source_files_properties(s3g_3oafx_single_effect_clap.cpp PROPERTIES LANGUAGE OBJCXX)
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

add_s3g_3oafx_single_target(
  s3g_3oafx_delay_clap
  0
  "s3g_3oafx_delay"
  "org.s3g.s3g-dsp.3oafx-delay"
  "s3g 3OAFX Delay"
  "Single-slot 3OAFX delay with virtual-speaker return mask and 3OA output."
)

add_s3g_3oafx_single_target(
  s3g_3oafx_pitch_clap
  1
  "s3g_3oafx_pitch"
  "org.s3g.s3g-dsp.3oafx-pitch"
  "s3g 3OAFX Pitch"
  "Single-slot 3OAFX pitch shift with virtual-speaker return mask and 3OA output."
)

add_s3g_3oafx_single_target(
  s3g_3oafx_filter_clap
  2
  "s3g_3oafx_filter"
  "org.s3g.s3g-dsp.3oafx-filter"
  "s3g 3OAFX Filter"
  "Single-slot 3OAFX filter with virtual-speaker return mask and 3OA output."
)

add_s3g_3oafx_single_target(
  s3g_3oafx_gain_clap
  3
  "s3g_3oafx_gain"
  "org.s3g.s3g-dsp.3oafx-gain"
  "s3g 3OAFX Gain"
  "Single-slot 3OAFX gain and cut processor with virtual-speaker mask and 3OA output."
)
