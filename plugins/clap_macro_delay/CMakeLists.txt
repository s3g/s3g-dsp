function(add_s3g_macro_delay_target target channels output_name bundle_id bundle_name description)
  add_library(${target} MODULE
    s3g_macro_delay_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  target_compile_definitions(${target} PRIVATE
    S3G_MACRO_DELAY_CHANNEL_COUNT=${channels}
    S3G_MACRO_DELAY_PLUGIN_ID="${bundle_id}"
    S3G_MACRO_DELAY_PLUGIN_NAME="${bundle_name}"
    S3G_MACRO_DELAY_DESCRIPTION="${description}"
  )

  if(S3G_VSTGUI_AVAILABLE)
    s3g_enable_vstgui_gui(${target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_macro_delay_vstgui.cpp"
      S3G_ENABLE_VSTGUI_GUI)
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
  elseif(WIN32)
    set_target_properties(${target} PROPERTIES SUFFIX ".clap")
  endif()
endfunction()

add_s3g_macro_delay_target(
  s3g_macro_delay_clap
  8
  "s3g_macro_delay"
  "org.s3g.s3g-dsp.macro-delay-8ch"
  "s3g Macro Delay 8"
  "8-channel macro delay with shared effect trims and smoothed channel-relationship controls."
)

add_s3g_macro_delay_target(
  s3g_24ch_macro_delay_clap
  24
  "s3g_24ch_macro_delay"
  "org.s3g.s3g-dsp.macro-delay-24ch"
  "s3g Macro Delay 24"
  "24-channel macro delay with shared effect trims and smoothed channel-relationship controls."
)

if(WIN32 AND BUILD_TESTING)
  add_executable(s3g_macro_delay_windows_clap_smoke
    ../../tests/macro_delay_windows_clap_smoke.cpp)
  target_include_directories(s3g_macro_delay_windows_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  add_dependencies(s3g_macro_delay_windows_clap_smoke
    s3g_macro_delay_clap)
  add_test(NAME s3g_macro_delay_windows_clap_smoke
    COMMAND $<TARGET_FILE:s3g_macro_delay_windows_clap_smoke>
      $<TARGET_FILE:s3g_macro_delay_clap>)
  set_tests_properties(s3g_macro_delay_windows_clap_smoke PROPERTIES
    LABELS "clap;gui;windows;smoke"
    TIMEOUT 60)
endif()
