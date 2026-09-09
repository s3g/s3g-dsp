add_library(s3g_crcltr_clap MODULE
  s3g_crcltr_clap.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(s3g_crcltr_clap PRIVATE s3g_dsp Threads::Threads)
target_include_directories(s3g_crcltr_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_features(s3g_crcltr_clap PRIVATE cxx_std_17)

if(S3G_SAMPLE_FAMILY_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_crcltr_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_sample_family_vstgui.cpp"
    S3G_ENABLE_VSTGUI_SAMPLE_FAMILY_GUI)
  s3g_enable_windows_sample_decode(s3g_crcltr_clap)
endif()

if(APPLE)
  set_source_files_properties(s3g_crcltr_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_crcltr_clap PRIVATE
    "-framework Cocoa"
    "-framework AVFoundation")
endif()

if(WIN32)
  set_target_properties(s3g_crcltr_clap PROPERTIES SUFFIX ".clap")
endif()

set_target_properties(s3g_crcltr_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_crcltr"
)

if(APPLE)
  set_target_properties(s3g_crcltr_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.crcltr"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Sample Circulator 2"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
