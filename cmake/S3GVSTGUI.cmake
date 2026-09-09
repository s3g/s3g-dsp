include_guard(GLOBAL)

function(s3g_enable_vstgui_gui target gui_source compile_definition)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "s3g_enable_vstgui_gui: unknown target ${target}")
  endif()
  if(NOT S3G_VSTGUI_AVAILABLE)
    message(FATAL_ERROR
      "s3g_enable_vstgui_gui requires S3G_VSTGUI_AVAILABLE")
  endif()

  set(font_file
    "${PROJECT_SOURCE_DIR}/assets/fonts/FiraCode-Regular.ttf")
  set(font_license
    "${PROJECT_SOURCE_DIR}/assets/fonts/FiraCode-LICENSE.txt")
  set(common_dir "${PROJECT_SOURCE_DIR}/plugins/common")

  target_sources(${target} PRIVATE
    "${gui_source}"
    "${common_dir}/s3g_vstgui_foundation.cpp")
  target_include_directories(${target} PRIVATE
    "${common_dir}"
    "${vstgui_SOURCE_DIR}")
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE
    "${compile_definition}=1"
    VSTGUI_ENABLE_DEPRECATED_METHODS=0
    VSTGUI_ENABLE_XML_PARSER=0
    VSTGUI_OPENGL_SUPPORT=0)
  target_link_libraries(${target} PRIVATE vstgui)

  if(compile_definition STREQUAL "S3G_ENABLE_VSTGUI_SAMPLE_FAMILY_GUI")
    target_sources(${target} PRIVATE "${common_dir}/s3g_sample_cursor_presenter.cpp")
    if(APPLE)
      target_compile_definitions(${target} PRIVATE
        "S3GPortableSampleCursorOverlay=${target}_CursorOverlay")
      set_source_files_properties("${common_dir}/s3g_sample_cursor_presenter.cpp"
        PROPERTIES LANGUAGE OBJCXX)
      target_link_libraries(${target} PRIVATE "-framework QuartzCore")
    elseif(WIN32)
      target_link_libraries(${target} PRIVATE d3d11 d2d1 dcomp dxgi)
    endif()
  endif()

  if(APPLE)
    set_source_files_properties(
      "${font_file}"
      "${font_license}"
      PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Fonts")
    target_sources(${target} PRIVATE
      "${font_file}"
      "${font_license}")
    # The linker supplies only an executable-level ad-hoc signature. Sign the
    # completed bundle after CMake has copied its font resources so macOS sees
    # a valid resource seal when a development build is installed directly.
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND /usr/bin/codesign --force --deep --sign -
        "$<TARGET_BUNDLE_DIR:${target}>"
      VERBATIM)
  elseif(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>/Resources/Fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${font_file}"
        "$<TARGET_FILE_DIR:${target}>/Resources/Fonts/FiraCode-Regular.ttf"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${font_license}"
        "$<TARGET_FILE_DIR:${target}>/Resources/Fonts/FiraCode-LICENSE.txt"
      VERBATIM)
  endif()
endfunction()

function(s3g_enable_windows_sample_decode target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR
      "s3g_enable_windows_sample_decode: unknown target ${target}")
  endif()
  if(WIN32)
    if(NOT S3G_DR_LIBS_INCLUDE_DIR)
      message(FATAL_ERROR
        "Windows sample decoding requires the pinned dr_libs source")
    endif()
    target_sources(${target} PRIVATE
      "${PROJECT_SOURCE_DIR}/plugins/common/s3g_sample_file_decode.cpp")
    target_include_directories(${target} SYSTEM PRIVATE
      "${S3G_DR_LIBS_INCLUDE_DIR}")
  endif()
endfunction()
