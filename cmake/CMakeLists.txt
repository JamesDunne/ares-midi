# dummy cmake target used to enable editing of CMake files within generated IDE projects

add_library(cmake INTERFACE)

target_sources(
  cmake
  PRIVATE
    common/bootstrap.cmake
    common/dependencies_common.cmake
    common/ccache.cmake
    common/compiler_common.cmake
    common/helpers_common.cmake
    common/osconfig.cmake
    common/versionconfig.cmake
    finders/Findlibrashader.cmake
    finders/Findslang_shaders.cmake
    finders/FindMoltenVK.cmake
    finders/FindSDL.cmake
    finders/FindGTK.cmake
    finders/FindAO.cmake
    finders/FindOSS.cmake
    finders/FindPulseAudio.cmake
    finders/Findudev.cmake
    finders/Findusbhid.cmake
    macos/dependencies.cmake
    macos/compilerconfig.cmake
    macos/defaults.cmake
    macos/helpers.cmake
    macos/xcode.cmake
    windows/compilerconfig.cmake
    windows/defaults.cmake
    windows/dependencies.cmake
    windows/helpers.cmake
    linux/compilerconfig.cmake
    linux/defaults.cmake
    linux/helpers.cmake
    linux/dependencies.cmake
)

get_target_property(cmake_SOURCES cmake SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${cmake_SOURCES})
