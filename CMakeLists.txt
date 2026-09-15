# Build the bundled StormLib as a static library.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "Use bundled StormLib dependencies" FORCE)

add_subdirectory(third_party/StormLib)
