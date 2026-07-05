cmake_minimum_required(VERSION 3.20)

project(s3g_dsp VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(s3g_dsp INTERFACE)
target_include_directories(s3g_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp)

include(FetchContent)

add_executable(s3g_dsp_smoke tests/dsp_smoke.cpp)
target_link_libraries(s3g_dsp_smoke PRIVATE s3g_dsp)

option(S3G_BUILD_CLAP_PLUGIN "Build experimental CLAP plugin targets" OFF)
option(S3G_FETCH_CLAP "Fetch the CLAP headers when S3G_CLAP_INCLUDE_DIR is not set" ON)
set(S3G_CLAP_INCLUDE_DIR "" CACHE PATH "Path containing clap/clap.h")
set(S3G_CLAP_GIT_REPOSITORY "https://github.com/free-audio/clap.git" CACHE STRING "CLAP git repository used by FetchContent")
set(S3G_CLAP_GIT_TAG "1.2.6" CACHE STRING "CLAP git tag or commit used by FetchContent")

if(S3G_BUILD_CLAP_PLUGIN)
  if(NOT S3G_CLAP_INCLUDE_DIR)
    if(NOT S3G_FETCH_CLAP)
      message(FATAL_ERROR "Set S3G_CLAP_INCLUDE_DIR to a CLAP include directory containing clap/clap.h, or enable S3G_FETCH_CLAP")
    endif()

    FetchContent_Declare(
      clap
      GIT_REPOSITORY ${S3G_CLAP_GIT_REPOSITORY}
      GIT_TAG ${S3G_CLAP_GIT_TAG}
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(clap)
    set(S3G_CLAP_INCLUDE_DIR ${clap_SOURCE_DIR}/include)
  endif()

  if(NOT EXISTS "${S3G_CLAP_INCLUDE_DIR}/clap/clap.h")
    message(FATAL_ERROR "S3G_CLAP_INCLUDE_DIR must contain clap/clap.h: ${S3G_CLAP_INCLUDE_DIR}")
  endif()

  add_subdirectory(plugins/clap_24ch_passthrough)
  add_subdirectory(plugins/clap_diffusion_mesh)
  add_subdirectory(plugins/clap_24ch_diffusion_mesh)
  add_subdirectory(plugins/clap_tape_delay)
  add_subdirectory(plugins/clap_3oafx_boundary)
endif()
