add_library(s3g_node_track_mixer_clap MODULE
  s3g_node_track_mixer_clap.cpp
)

add_library(s3g_ambi_node_track_mixer_clap MODULE
  s3g_node_track_mixer_clap.cpp
)

target_link_libraries(s3g_node_track_mixer_clap PRIVATE s3g_dsp)
target_include_directories(s3g_node_track_mixer_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})

target_link_libraries(s3g_ambi_node_track_mixer_clap PRIVATE s3g_dsp)
target_include_directories(s3g_ambi_node_track_mixer_clap PRIVATE ${S3G_CLAP_INCLUDE_DIR})
target_compile_definitions(s3g_ambi_node_track_mixer_clap PRIVATE S3G_AMBI_NODE_TRACK_MIXER=1)

set_target_properties(s3g_node_track_mixer_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_node_bus_mixer"
)

set_target_properties(s3g_ambi_node_track_mixer_clap PROPERTIES
  PREFIX ""
  OUTPUT_NAME "s3g_ambi_node_bus_mixer"
)

if(APPLE)
  set_source_files_properties(s3g_node_track_mixer_clap.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(s3g_node_track_mixer_clap PRIVATE "-framework Cocoa")
  target_link_libraries(s3g_ambi_node_track_mixer_clap PRIVATE "-framework Cocoa")

  set_target_properties(s3g_node_track_mixer_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.node-bus-mixer"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Mixer Node Bus 128"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )

  set_target_properties(s3g_ambi_node_track_mixer_clap PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "clap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-dsp.ambi-node-bus-mixer"
    MACOSX_BUNDLE_BUNDLE_NAME "s3g Ambi Mixer Node Bus 128"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()

if(S3G_ROUTING_VSTGUI_AVAILABLE)
  s3g_enable_vstgui_gui(s3g_node_track_mixer_clap
    "${PROJECT_SOURCE_DIR}/plugins/common/s3g_node_bus_canvas.inc"
    S3G_ENABLE_VSTGUI_CANVAS_GUI)
endif()
if(WIN32)
  set_target_properties(s3g_node_track_mixer_clap PROPERTIES SUFFIX ".clap")
endif()

foreach(s3g_ambi_target IN ITEMS s3g_ambi_node_track_mixer_clap)
  if(S3G_AMBI_BUS_VSTGUI_AVAILABLE)
    s3g_enable_vstgui_gui(${s3g_ambi_target}
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_node_bus_canvas.inc"
      S3G_ENABLE_VSTGUI_CANVAS_GUI)
  endif()
  if(WIN32)
    set_target_properties(${s3g_ambi_target} PROPERTIES SUFFIX ".clap")
  endif()
endforeach()
