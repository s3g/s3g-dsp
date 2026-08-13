add_library(s3g_processor_fissure_clap MODULE
  s3g_processor_fissure_clap.cpp
)

target_link_libraries(s3g_processor_fissure_clap PRIVATE s3g_dsp)
target_include_directories(s3g_processor_fissure_clap PRIVATE
  ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_processor_fissure_clap.cpp
    PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_processor_fissure_clap PRIVATE
    "-framework Cocoa")
endif()

set_target_properties(s3g_processor_fissure_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_processor_fissure"
)

if(APPLE)
  set_target_properties(s3g_processor_fissure_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.processor-fissure"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Processor Fissure"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.7.2"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.7.2"
  )
endif()

if(BUILD_TESTING)
  add_executable(s3g_processor_fissure_clap_smoke
    ../../tests/processor_fissure_clap_smoke.cpp)
  target_include_directories(s3g_processor_fissure_clap_smoke PRIVATE
    ${S3G_CLAP_INCLUDE_DIR})
  target_link_libraries(s3g_processor_fissure_clap_smoke PRIVATE
    ${CMAKE_DL_LIBS})
  add_dependencies(s3g_processor_fissure_clap_smoke
    s3g_processor_fissure_clap)
  add_test(NAME s3g_processor_fissure_clap_smoke
    COMMAND $<TARGET_FILE:s3g_processor_fissure_clap_smoke>
      $<TARGET_FILE:s3g_processor_fissure_clap>)
  set_tests_properties(s3g_processor_fissure_clap_smoke PROPERTIES
    LABELS "non_nim;clap;smoke" TIMEOUT 120)

  if(APPLE AND TARGET s3g_encoder_family_gui_smoke)
    add_test(NAME s3g_processor_fissure_gui_smoke
      COMMAND $<TARGET_FILE:s3g_encoder_family_gui_smoke>
        $<TARGET_FILE:s3g_processor_fissure_clap>
        org.s3g.s3g-dsp.processor-fissure
        1356 770 "s3g Processor Fissure" responsive)
    set_tests_properties(s3g_processor_fissure_gui_smoke PROPERTIES
      LABELS "non_nim;clap;gui" TIMEOUT 120)
  endif()
endif()
