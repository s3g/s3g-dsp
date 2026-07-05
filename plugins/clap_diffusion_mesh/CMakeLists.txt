add_library(s3g_diffusion_mesh_clap MODULE
  s3g_diffusion_mesh_clap.cpp
)

target_link_libraries(s3g_diffusion_mesh_clap PRIVATE s3g_dsp)
target_include_directories(s3g_diffusion_mesh_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

if(APPLE)
  set_source_files_properties(s3g_diffusion_mesh_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_diffusion_mesh_clap PRIVATE "-framework Cocoa")
endif()

set_target_properties(s3g_diffusion_mesh_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_diffusion_mesh"
)

if(APPLE)
  set_target_properties(s3g_diffusion_mesh_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.diffusion-mesh"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Diffusion Mesh"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

function(add_s3g_fixed_diffusion_mesh CHANNELS)
  if(${CHANNELS} EQUAL 24)
    set(target "s3g_${CHANNELS}ch_lane_diffusion_mesh_clap")
    set(output "s3g_${CHANNELS}ch_lane_diffusion_mesh")
    set(plugin_id "org.s3g.s3g-dsp.diffusion-mesh-${CHANNELS}ch-lanes")
    set(plugin_name "s3g Diffusion Mesh ${CHANNELS}ch Lanes")
  else()
    set(target "s3g_${CHANNELS}ch_diffusion_mesh_clap")
    set(output "s3g_${CHANNELS}ch_diffusion_mesh")
    set(plugin_id "org.s3g.s3g-dsp.diffusion-mesh-${CHANNELS}ch")
    set(plugin_name "s3g Diffusion Mesh ${CHANNELS}ch")
  endif()
  set(plugin_description "${CHANNELS}-channel fixed-width diffusion mesh wrapper over the scalable s3g DiffusionMesh DSP.")

  add_library(${target} MODULE
    s3g_diffusion_mesh_clap.cpp
  )

  target_link_libraries(${target} PRIVATE s3g_dsp)
  target_include_directories(${target} PRIVATE ${S3G_CLAP_INCLUDE_DIR})
  if(APPLE)
    set_source_files_properties(s3g_diffusion_mesh_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(${target} PRIVATE "-framework Cocoa")
  endif()
  target_compile_definitions(${target} PRIVATE
    S3G_DIFFUSION_MESH_DEFAULT_CHANNELS=${CHANNELS}
    S3G_DIFFUSION_MESH_FIXED_PORTS=1
    S3G_DIFFUSION_MESH_PLUGIN_ID="${plugin_id}"
    S3G_DIFFUSION_MESH_PLUGIN_NAME="${plugin_name}"
    S3G_DIFFUSION_MESH_PLUGIN_DESCRIPTION="${plugin_description}"
  )

  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${output}"
  )

  if(APPLE)
    set_target_properties(${target} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${plugin_id}"
      MACOSX_BUNDLE_BUNDLE_NAME "${plugin_name}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endfunction()

foreach(ch IN ITEMS 8 16 24 32 64)
  add_s3g_fixed_diffusion_mesh(${ch})
endforeach()
