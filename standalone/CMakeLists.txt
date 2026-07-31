if(NOT APPLE)
  return()
endif()

add_library(s3g_standalone_host STATIC
  host/s3g_embedded_clap_host.cpp
  audio/s3g_coreaudio_output.cpp
)

target_include_directories(s3g_standalone_host PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/host
  ${CMAKE_CURRENT_SOURCE_DIR}/audio
  ${S3G_CLAP_INCLUDE_DIR}
)

target_link_libraries(s3g_standalone_host PUBLIC
  "-framework AudioUnit"
  "-framework CoreAudio"
)
