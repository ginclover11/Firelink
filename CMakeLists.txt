# NOTE: This CML project can be used to build all Firelink modules as well as tests.
# You can also `add_subdirectory` the individual modules in your own CMake project.

cmake_minimum_required(VERSION 3.27)
project(Firelink VERSION 0.3.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    # The pybind11 modules statically link FirelinkCore and its dependencies (zlib, zstd, DirectXTex...) and are loaded
    # into a host process (Blender) that has its own copies of some of those libraries. Keep our symbols private so they
    # can neither clash with nor be interposed by the host's.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

option(FIRELINK_BUILD_TESTS      "Build doctest-based unit tests for FirelinkCore libraries" ON)
option(FIRELINK_BUILD_PYBIND    "Build pybind11 Python extension modules for Firelink"      ON)

# Default install to build_dir/install instead of Program Files
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Default install path" FORCE)
endif()

# Enable PIC (required for PYDs).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Add Firelink CMake scripts.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(FirelinkModules)

# Report BUILD_SHARED_LIBS.
message(STATUS "[Firelink] BUILD_SHARED_LIBS: ${BUILD_SHARED_LIBS}")

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /DEBUG")
endif()

# Define NOMINMAX globally to prevent Windows headers from defining min/max macros.
add_compile_definitions(NOMINMAX)

# Add Firelink libraries.
add_subdirectory(lib)

# PYTHON BINDINGS
if(FIRELINK_BUILD_PYBIND)
    include(FetchContent)
    FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v3.0.3
            GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(pybind11)
    add_subdirectory(python/pybind)
endif()

# TESTING
if(FIRELINK_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.5.1
            GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)

    enable_testing()

    set(FIRELINK_TEST_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test_resources")
    add_subdirectory(tests)
endif()

# Add Firelink executables.
add_subdirectory(app)

# The CMake package export is Windows-only: on other platforms the third-party dependencies (zlib, zstd, DirectXTex...)
# are built privately into the modules with `EXCLUDE_FROM_ALL` (see lib/FirelinkCore/CMakeLists.txt), so they have no
# export sets of their own for FirelinkTargets to refer to.
if(WIN32)
    install(EXPORT FirelinkTargets
            FILE FirelinkTargets.cmake
            NAMESPACE Firelink::
            DESTINATION lib/cmake/Firelink
    )
endif()

# If this is not scikit-build, install `pyrelink` folder of bindings.
if(NOT DEFINED SKBUILD)
    install(DIRECTORY pyrelink DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()
