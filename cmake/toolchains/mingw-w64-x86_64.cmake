set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(S3G_MINGW_CXX_COMPILER
  NAMES x86_64-w64-mingw32-g++
  REQUIRED)
find_program(S3G_MINGW_RC_COMPILER
  NAMES x86_64-w64-mingw32-windres
  REQUIRED)

set(CMAKE_CXX_COMPILER "${S3G_MINGW_CXX_COMPILER}")
set(CMAKE_RC_COMPILER "${S3G_MINGW_RC_COMPILER}")

# Cross-configuration checks must not try to launch Windows executables on the
# macOS build host.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Keep the pilot CLAPs self-contained. Windows system DLLs remain dynamic, but
# the GCC, libstdc++, and winpthreads runtimes are linked into each module.
set(CMAKE_EXE_LINKER_FLAGS_INIT
  "-static -static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT
  "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
  "-static -static-libgcc -static-libstdc++")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
