# The generic ALL target also contains POSIX-only CLAP hosts and test fixtures
# that have not been ported to MSVC. Keep the Windows release inventory complete
# without building those tools. This does not disable tests or alter Mac builds.
function(s3g_add_windows_prerelease_target)
  get_property(subdirectories DIRECTORY PROPERTY SUBDIRECTORIES)
  set(directories "${CMAKE_CURRENT_SOURCE_DIR}")
  foreach(directory IN LISTS subdirectories)
    if(directory MATCHES "/plugins/clap_[^/]+$" OR
       directory STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/tracker")
      list(APPEND directories "${directory}")
    endif()
  endforeach()

  set(plugins)
  set(regressions)
  foreach(directory IN LISTS directories)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type "${target}" TYPE)
      get_target_property(suffix "${target}" SUFFIX)
      if(type STREQUAL "MODULE_LIBRARY" AND suffix STREQUAL ".clap")
        list(APPEND plugins "${target}")
      elseif(type STREQUAL "EXECUTABLE" AND
             target MATCHES "^(s3g_windows_|s3g_tracker_|s3g_[a-z0-9_]*_windows_|s3g_shared_efficiency_|s3g_sample_family_interaction_smoke$)")
        # Match the CTest selection in windows-prerelease.yml. Tracker's two
        # pack generators have test names ending in _tests; include the actual
        # generator executables here as well as its core and CLAP regressions.
        list(APPEND regressions "${target}")
      endif()
    endforeach()
  endforeach()
  if(NOT plugins OR NOT regressions)
    message(FATAL_ERROR "Windows prerelease requires CLAP modules and regression targets")
  endif()
  add_custom_target(s3g_windows_prerelease DEPENDS ${plugins} ${regressions})
  list(LENGTH plugins plugin_count)
  list(LENGTH regressions regression_count)
  message(STATUS "Windows prerelease: ${plugin_count} CLAP modules, ${regression_count} regression executables")
endfunction()
