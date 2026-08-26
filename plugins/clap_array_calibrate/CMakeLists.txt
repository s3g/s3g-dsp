function(add_s3g_array_calibrate_target CHANNELS)
  set(target "s3g_array_calibrate_${CHANNELS}_clap")
  add_library(${target} MODULE s3g_array_calibrate_clap.cpp)
  target_compile_definitions(${target} PRIVATE S3G_ARRAY_CALIBRATE_CHANNELS=${CHANNELS})
  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})

  if(APPLE)
    set_source_files_properties(s3g_array_calibrate_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa")
  endif()

  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_array_calibrate_${CHANNELS}"
  )

  if(APPLE)
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.array-calibrate-${CHANNELS}"
      MACOSX_BUNDLE_BUNDLE_NAME "s3g Array Calibrate ${CHANNELS}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endfunction()

add_s3g_array_calibrate_target(16)
add_s3g_array_calibrate_target(26)
add_s3g_array_calibrate_target(32)
add_s3g_array_calibrate_target(64)
