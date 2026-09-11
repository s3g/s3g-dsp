function(add_s3g_ambi_effect_pitch_gain target output_name bundle_id bundle_name is_pitch)
  add_library(${target} MODULE s3g_ambi_effect_pitch_gain_clap.cpp)
  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE S3G_AMBI_EFFECT_PITCH=${is_pitch})

  if(APPLE)
    set_source_files_properties(s3g_ambi_effect_pitch_gain_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa")
  endif()

  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${output_name}")
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

add_s3g_ambi_effect_pitch_gain(
  s3g_ambi_effect_pitch_clap
  "s3g_ambi_effect_pitch"
  "org.s3g.s3g-dsp.ambi-effect-pitch-64"
  "s3g Ambi Effect Pitch 64"
  1
)

add_s3g_ambi_effect_pitch_gain(
  s3g_ambi_effect_gain_clap
  "s3g_ambi_effect_gain"
  "org.s3g.s3g-dsp.ambi-effect-gain-64"
  "s3g Ambi Effect Gain 64"
  0
)

foreach(s3g_ambi_effect_target IN ITEMS s3g_ambi_effect_pitch_clap s3g_ambi_effect_gain_clap)
  if(S3G_AMBI_EFFECTS_VSTGUI_AVAILABLE)
    s3g_enable_vstgui_gui(${s3g_ambi_effect_target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_ambi_effect_pitch_gain_canvas.inc"
      S3G_ENABLE_VSTGUI_CANVAS_GUI)
  endif()
  if(WIN32)
    set_target_properties(${s3g_ambi_effect_target} PROPERTIES SUFFIX ".clap")
  endif()
endforeach()
