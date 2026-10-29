add_firelink_library(FirelinkCore)

target_sources(FirelinkCore
    PRIVATE
    src/pch.h
    src/pch.cpp
    src/Binder.cpp
    src/BinaryReadWrite.cpp
    src/Collections.cpp
    src/DevTools.cpp
    src/DCX.cpp
    src/DDS.cpp
    src/Encodings.cpp
    src/Logging.cpp
    src/Oodle.cpp
    src/Paths.cpp
    src/Swizzle.cpp
    src/TPF.cpp
    src/MemoryUtils.cpp

    src/Havok/Helpers.cpp
    src/Havok/HKX.cpp
    src/Havok/Types.cpp
    src/Havok/TagfileUnpacker.cpp
)

# Windows-only: the DLL entry point and the process/memory-hooking machinery (`ManagedProcess`, `Pointer`, `BaseHook`)
# used by the game hooks. None of the file-format code (Binder, DCX, TPF, FLVER, MSB...) depends on these.
if(WIN32)
    target_sources(FirelinkCore
        PRIVATE
        src/dllmain.cpp
        src/BaseHook.cpp
        src/Pointer.cpp
        src/Process.cpp
    )
else()
    # PNG conversion without WIC (stb_image / stb_image_write).
    target_sources(FirelinkCore PRIVATE src/PngCodec.cpp)
endif()

target_sources(FirelinkCore
    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    FILES
        include/FirelinkCore/BaseHook.h
        include/FirelinkCore/Binder.h
        include/FirelinkCore/BinaryReadWrite.h
        include/FirelinkCore/BinaryValidation.h
        include/FirelinkCore/Collections.h
        include/FirelinkCore/DevTools.h
        include/FirelinkCore/DCX.h
        include/FirelinkCore/DDS.h
        include/FirelinkCore/Encodings.h
        include/FirelinkCore/GameFile.h
        include/FirelinkCore/GameType.h
        include/FirelinkCore/Logging.h
        include/FirelinkCore/Logging.h
        include/FirelinkCore/Oodle.h
        include/FirelinkCore/Paths.h
        include/FirelinkCore/Pointer.h
        include/FirelinkCore/Process.h
        include/FirelinkCore/Swizzle.h
        include/FirelinkCore/TPF.h
        include/FirelinkCore/MemoryUtils.h

        include/FirelinkCore/Havok/Enums.h
        include/FirelinkCore/Havok/Helpers.h
        include/FirelinkCore/Havok/HKX.h
        include/FirelinkCore/Havok/Types.h
        include/FirelinkCore/Havok/TagfileUnpacker.h
)

target_precompile_headers(FirelinkCore
    PRIVATE src/pch.h
)

generate_firelink_export_header(FirelinkCore FIRELINK_CORE_API)

# DCX compression support requires zlib and zstd (fetched automatically).
include(FetchContent)

macro(get_firelink_core_dependencies)

    # Outside Windows, build dependencies privately: `EXCLUDE_FROM_ALL` (FetchContent, CMake 3.28+) skips their install()
    # rules, so they are not copied into the Python wheel, and CMake does not require them to be in export sets.
    # Targets we link against are still built on demand.
    set(_FIRELINK_FC_ARGS "")
    if(NOT WIN32)
        if(CMAKE_VERSION VERSION_LESS 3.28)
            message(FATAL_ERROR "CMake 3.28 or newer is required to build Firelink outside Windows (found ${CMAKE_VERSION}).")
        endif()
        set(_FIRELINK_FC_ARGS EXCLUDE_FROM_ALL)
    endif()

    # --- zlib ---
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.2
        ${_FIRELINK_FC_ARGS}
    )
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)

    # zlib's CMakeLists.txt creates a `zlibstatic` target.
    # Alias it so downstream code can use the canonical name if needed.
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()

    # --- zstd ---
    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG        v1.5.7
        SOURCE_SUBDIR  build/cmake
        ${_FIRELINK_FC_ARGS}
    )
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zstd)

    # TODO: Re-enable or remove.
    # Ensure the fetched headers are visible to FirelinkCore sources.
    #target_include_directories(FirelinkCore PRIVATE
    #    "${zlib_SOURCE_DIR}"
    #    "${zlib_BINARY_DIR}"
    #    "${zstd_SOURCE_DIR}/lib"
    #)

    # --- DirectXMath / DirectX-Headers / stb (non-Windows only) ---
    # On Windows, DirectXTex uses the Windows SDK. Everywhere else it needs the DirectXMath and DirectX-Headers packages
    # (DXGI format enums, SAL/HRESULT adapters), which we fetch here. `OVERRIDE_FIND_PACKAGE` makes DirectXTex's own
    # `find_package(directxmath)` / `find_package(directx-headers)` calls resolve to these copies.
    # `stb` provides PNG I/O in place of WIC (see src/PngCodec.cpp); it is header-only and has no CMakeLists.
    if(NOT WIN32)
        FetchContent_Declare(
            directxmath
            GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
            GIT_TAG        oct2024
            GIT_SHALLOW    TRUE
            # DirectXTex calls `find_package(directxmath CONFIG REQUIRED)`; resolve it to this copy.
            OVERRIDE_FIND_PACKAGE
            ${_FIRELINK_FC_ARGS}
        )
        FetchContent_Declare(
            directx-headers
            GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
            GIT_TAG        v1.721.2
            GIT_SHALLOW    TRUE
            # DirectXTex calls `find_package(directx-headers CONFIG REQUIRED)`; resolve it to this copy.
            OVERRIDE_FIND_PACKAGE
            ${_FIRELINK_FC_ARGS}
        )
        set(DXHEADERS_BUILD_TEST        OFF CACHE BOOL "" FORCE)
        set(DXHEADERS_BUILD_GOOGLE_TEST OFF CACHE BOOL "" FORCE)
        # ON: DirectXTex exports itself with install(EXPORT) and CMake insists that everything it links is in an export set too.
        # (The install rules themselves are never run: all fetched dependencies are EXCLUDE_FROM_ALL.)
        set(DXHEADERS_INSTALL           ON  CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(directxmath directx-headers)

        # DirectXMath (and DirectXTex) include "sal.h" (MSVC source annotations), which only exists on Windows. The .NET
        # runtime ships an MIT-licensed open-source version, which is what DirectXMath's own Linux CI uses.
        FetchContent_Declare(
            sal
            URL      https://raw.githubusercontent.com/dotnet/runtime/v8.0.1/src/coreclr/pal/inc/rt/sal.h
            URL_HASH SHA256=7dae281adc3a09a691291fb90526f05e4f9ef8b16d7f33d716ba690f7241a492
            DOWNLOAD_NO_EXTRACT TRUE
        )
        FetchContent_MakeAvailable(sal)
        # BUILD_INTERFACE: DirectXMath exports itself with install(EXPORT), and build-tree paths must not leak into that export.
        target_include_directories(DirectXMath INTERFACE "$<BUILD_INTERFACE:${sal_SOURCE_DIR}>")

        # DirectXMath only defines its namespaced target when installed; DirectXTex links the namespaced name.
        if(NOT TARGET Microsoft::DirectXMath)
            add_library(Microsoft::DirectXMath ALIAS DirectXMath)
        endif()

        FetchContent_Declare(
            stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG        003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
        )
        FetchContent_MakeAvailable(stb)
    endif()

    # --- DirectXTex ---
    # In-memory DDS conversion (LoadFromDDSMemory, Decompress, Convert,
    # SaveToTGAMemory, SaveToWICMemory).  We only need the static library.
    FetchContent_Declare(
            DirectXTex
            GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
            GIT_TAG        oct2025
            ${_FIRELINK_FC_ARGS}
    )
    set(BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(BUILD_SAMPLE   OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)
    if(NOT WIN32)
        # Software BC6H/BC7 compression can use OpenMP, but GCC 16's <omp.h> fails to compile in DirectXTexCompress.cpp
        # (default-argument macros in its `omp_depend_t` declarations), so it is off by default outside Windows. The
        # only cost is that BC6H/BC7 compression runs on one thread (BC1-BC5 never used OpenMP).
        option(FIRELINK_DIRECTXTEX_OPENMP "Build DirectXTex with OpenMP (multi-threaded BC6H/BC7 compression)" OFF)
        set(BC_USE_OPENMP ${FIRELINK_DIRECTXTEX_OPENMP} CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(DirectXTex)
endmacro()

get_firelink_core_dependencies()


# Link against the fetched dependencies.
if(WIN32)
    # ole32   — COM / WIC (PNG output via SaveToWICMemory)
    # d3d11   — GPU-accelerated BC6H/BC7 compression via DirectCompute
    # dxgi    — DXGI adapter enumeration for D3D11 device creation
    target_link_libraries(FirelinkCore PRIVATE
        zlibstatic
        libzstd_static
        DirectXTex
        ole32.lib
        d3d11.lib
        dxgi.lib)
else()
    # No WIC/D3D11 here: PNG goes through stb, and BC6H/BC7 are compressed on the CPU.
    # Threads: `std::thread` is used for parallel file loading. ${CMAKE_DL_LIBS}: `dlopen` for the Oodle library.
    find_package(Threads REQUIRED)
    target_link_libraries(FirelinkCore PRIVATE
        zlibstatic
        libzstd_static
        DirectXTex
        Threads::Threads
        ${CMAKE_DL_LIBS})
    target_include_directories(FirelinkCore PRIVATE "${stb_SOURCE_DIR}")
endif()

install(TARGETS FirelinkCore
        EXPORT FirelinkTargets
        FILE_SET HEADERS
)
