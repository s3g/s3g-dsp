if(NOT APPLE)
  return()
endif()

if(NOT S3G_ENABLE_WORLD OR NOT TARGET world::core)
  message(FATAL_ERROR "s3g Vox Builder requires S3G_ENABLE_WORLD=ON")
endif()

set(S3G_VOX_BUILDER_RESOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../THIRD_PARTY_NOTICES.md
)

set_source_files_properties(${S3G_VOX_BUILDER_RESOURCES} PROPERTIES
  MACOSX_PACKAGE_LOCATION Resources
)

add_executable(s3g_vox_builder_app MACOSX_BUNDLE
  s3g_vox_builder_app.mm
  ${S3G_VOX_BUILDER_RESOURCES}
)

target_link_libraries(s3g_vox_builder_app PRIVATE
  s3g_dsp
  world::core
  "-framework AVFoundation"
  "-framework Cocoa"
)

target_include_directories(s3g_vox_builder_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/common
)

target_compile_options(s3g_vox_builder_app PRIVATE "-fobjc-arc")

set_target_properties(s3g_vox_builder_app PROPERTIES
  OUTPUT_NAME "s3g Vox Builder"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.vox-builder"
  MACOSX_BUNDLE_BUNDLE_NAME "s3g Vox Builder"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
