function(add_s3g_array_delay_target CHANNELS)
  set(target "s3g_array_delay_${CHANNELS}_clap")
  add_library(${target} MODULE s3g_array_delay_clap.cpp)
  target_compile_definitions(${target} PRIVATE S3G_ARRAY_DELAY_CHANNELS=${CHANNELS})
  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})

  if(APPLE)
    set_source_files_properties(s3g_array_delay_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa")
  endif()

  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_array_delay_${CHANNELS}"
  )

  if(APPLE)
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.array-delay-${CHANNELS}"
      MACOSX_BUNDLE_BUNDLE_NAME "s3g Array Delay ${CHANNELS}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endfunction()

add_s3g_array_delay_target(16)
add_s3g_array_delay_target(26)
add_s3g_array_delay_target(32)
add_s3g_array_delay_target(64)
