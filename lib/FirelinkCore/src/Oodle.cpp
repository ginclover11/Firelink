// Oodle (Kraken) compression/decompression via dynamic loading of the Oodle core library:
//   - Windows: oo2core_6_win64.dll (e.g. from the Sekiro / Elden Ring game folder)
//   - Linux:   liboo2corelinux64.so[.N] (NOT shipped with Firelink or the games; supply your own copy from the Oodle
//              SDK / an Unreal Engine install). A Windows DLL cannot be loaded on Linux, even from a Proton prefix.

#include <FirelinkCore/Oodle.h>
#include <FirelinkCore/DCX.h>
#include <FirelinkCore/Logging.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Firelink::Oodle
{
    // ========================================================================
    // Oodle ABI types
    // ========================================================================

    struct CompressOptions
    {
        unsigned int verbosity;
        int minMatchLen;
        int seekChunkReset;
        int seekChunkLen;
        int profile;
        int dictionarySize;
        int spaceSpeedTradeoffBytes;
        int maxHuffmansPerChunk;
        int sendQuantumCRCs;
        int maxLocalDictionarySize;
        int makeLongRangeMatcher;
        int matchTableSizeLog2;
    };

    // Oodle constants
    static constexpr int Compressor_Kraken = 8;
    static constexpr int Level_Optimal2 = 6;
    static constexpr int FuzzSafe_Yes = 1;
    static constexpr int CheckCRC_No = 0;
    static constexpr int Verbosity_None = 0;
    static constexpr int ThreadPhase_Unthreaded = 3;

    // Oodle's C API uses `__stdcall` on 32-bit Windows; on x86-64 (Windows or Linux) there is a single calling convention
    // and the attribute is meaningless (GCC ignores it with a warning), so only spell it out where it matters.
#ifdef _WIN32
#define FIRELINK_OODLE_CALL __stdcall
#else
#define FIRELINK_OODLE_CALL
#endif

    // Function pointer types.
    // NOTE: `long` stands in for Oodle's `OO_SINTa` (pointer-sized signed integer). That is correct on Linux x86-64 (LP64)
    // and is left unchanged on Windows to preserve the existing, tested behaviour.
    using Fn_Compress = long(FIRELINK_OODLE_CALL*)(
        int, const void*, long, void*, int,
        CompressOptions*, const void*, const void*, void*, long);
    using Fn_Decompress = long(FIRELINK_OODLE_CALL*)(
        const void*, long, void*, long,
        int, int, int, void*, long, void*, void*, void*, long, int);
    using Fn_GetCompressedBufferSizeNeeded = long(FIRELINK_OODLE_CALL*)(long);
    using Fn_GetDecodeBufferSize = long(FIRELINK_OODLE_CALL*)(long, int);
    using Fn_CompressOptions_GetDefault = CompressOptions*(FIRELINK_OODLE_CALL*)(int, int);

    // ========================================================================
    // Module state
    // ========================================================================

    static std::once_flag s_initFlag;
    static std::mutex s_mutex;          // protects explicit LoadDLL after init
#ifdef _WIN32
    using LibHandle = HMODULE;
#else
    using LibHandle = void*;
#endif
    static LibHandle s_dll = nullptr;
    static std::string s_loadedPath;
    static std::string s_userSearchPath; // set by SetDLLSearchPath

    static Fn_Compress s_compress = nullptr;
    static Fn_Decompress s_decompress = nullptr;
    static Fn_GetCompressedBufferSizeNeeded s_getCompressedSize = nullptr;
    static Fn_GetDecodeBufferSize s_getDecodeSize = nullptr;
    static Fn_CompressOptions_GetDefault s_getDefaultOptions = nullptr;

#ifdef _WIN32
    static constexpr const char* DLL_NAME = "oo2core_6_win64.dll";
#else
    static constexpr const char* DLL_NAME = "liboo2corelinux64.so";  // prefix of the real file name, e.g. '.so.9'
#endif

    // ========================================================================
    // Platform shims for dynamic loading
    // ========================================================================

    static LibHandle OpenLibrary(const std::string& path)
    {
#ifdef _WIN32
        return LoadLibraryA(path.c_str());
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }

    static void* FindSymbol(LibHandle h, const char* name)
    {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(h, name));
#else
        return dlsym(h, name);
#endif
    }

    static void CloseLibrary(LibHandle h)
    {
#ifdef _WIN32
        FreeLibrary(h);
#else
        dlclose(h);
#endif
    }

    /// @brief Library files to try for a user-supplied location, best candidate first.
    ///
    /// `location` may be the library file itself or a directory containing it. On Windows the file name is fixed
    /// (`oo2core_6_win64.dll`). On Linux the Oodle library carries a version suffix (`.so.9`, `.so.8`...), so we accept
    /// any `liboo2corelinux64.so*` in the directory, highest version suffix first.
    static std::vector<std::string> CandidateLibraryPaths(const std::string& location)
    {
        namespace fs = std::filesystem;
        std::vector<std::string> candidates;
        std::error_code ec;

        const fs::path loc(location);
        if (fs::is_regular_file(loc, ec))
        {
            candidates.push_back(loc.string());
            return candidates;
        }
        if (!fs::is_directory(loc, ec))
            return candidates;

#ifdef _WIN32
        if (const auto dll = loc / DLL_NAME; fs::exists(dll, ec))
            candidates.push_back(dll.string());
#else
        const std::string prefix = DLL_NAME;
        for (const auto& entry : fs::directory_iterator(loc, ec))
        {
            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec))
                continue;
            if (const auto name = entry.path().filename().string(); name.rfind(prefix, 0) == 0)
                candidates.push_back(entry.path().string());
        }
        std::ranges::sort(candidates, std::greater<>());  // 'liboo2corelinux64.so.9' before '...so.8' before '...so'
#endif
        return candidates;
    }

    // ========================================================================
    // Internal helpers
    // ========================================================================

    /// @brief Load one specific library file (or, on Linux, a bare soname the dynamic linker can resolve) and bind the
    /// Oodle entry points. Returns false (leaving any previously loaded library untouched) if it is not a usable Oodle.
    static bool TryLoadLibraryFile(const std::string& libPath)
    {
        LibHandle h = OpenLibrary(libPath);
        if (!h)
            return false;

        auto fnC  = reinterpret_cast<Fn_Compress>(FindSymbol(h, "OodleLZ_Compress"));
        auto fnD  = reinterpret_cast<Fn_Decompress>(FindSymbol(h, "OodleLZ_Decompress"));
        auto fnGC = reinterpret_cast<Fn_GetCompressedBufferSizeNeeded>(
            FindSymbol(h, "OodleLZ_GetCompressedBufferSizeNeeded"));
        auto fnGD = reinterpret_cast<Fn_GetDecodeBufferSize>(
            FindSymbol(h, "OodleLZ_GetDecodeBufferSize"));
        auto fnOD = reinterpret_cast<Fn_CompressOptions_GetDefault>(
            FindSymbol(h, "OodleLZ_CompressOptions_GetDefault"));

        if (!fnC || !fnD || !fnGC || !fnGD || !fnOD)
        {
            CloseLibrary(h);
            return false;  // not an Oodle library (or an unexpected version)
        }

        // If we already had a library loaded, free the old one.
        if (s_dll)
            CloseLibrary(s_dll);

        s_dll = h;
        s_compress = fnC;
        s_decompress = fnD;
        s_getCompressedSize = fnGC;
        s_getDecodeSize = fnGD;
        s_getDefaultOptions = fnOD;
        s_loadedPath = libPath;
        return true;
    }

    /// @brief Try to load Oodle from `location` (a library file or a directory containing it).
    static bool TryLoadFrom(const std::string& location)
    {
        for (const std::string& libPath : CandidateLibraryPaths(location))
        {
            if (TryLoadLibraryFile(libPath))
                return true;
        }
        return false;
    }

    /// @brief Directories where a game install is likely to hold (or sit next to) a usable Oodle library.
    static std::vector<std::string> DefaultSearchLocations()
    {
        std::vector<std::string> locations;
#ifdef _WIN32
        locations.emplace_back("C:/Program Files (x86)/Steam/steamapps/common/ELDEN RING/Game");
#else
        if (const char* home = std::getenv("HOME"))
        {
            const std::string h = home;
            const std::string game = "/steamapps/common/ELDEN RING/Game";
            locations.push_back(h + "/.local/share/Steam" + game);
            locations.push_back(h + "/.steam/steam" + game);
            locations.push_back(h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam" + game);  // Flatpak Steam
        }
#endif
        return locations;
    }

    static void InitOnce()
    {
        // 1. User-specified search path (via SetDLLSearchPath)
        if (!s_userSearchPath.empty() && TryLoadFrom(s_userSearchPath))
        {
            Firelink::Info(std::format("[oodle] Loaded Oodle library from user path: {}", s_loadedPath));
            return;
        }

        // 2. Environment override: FIRELINK_OODLE_PATH may name the library file or the directory containing it.
        if (const char* envPath = std::getenv("FIRELINK_OODLE_PATH"); envPath && *envPath && TryLoadFrom(envPath))
        {
            Firelink::Info(std::format("[oodle] Loaded Oodle library from FIRELINK_OODLE_PATH: {}", s_loadedPath));
            return;
        }

        // 3. Default: Elden Ring Steam install locations
        for (const std::string& location : DefaultSearchLocations())
        {
            if (TryLoadFrom(location))
            {
                Firelink::Info(std::format("[oodle] Loaded Oodle library from: {}", s_loadedPath));
                return;
            }
        }

#ifndef _WIN32
        // 4. Linux only: let the dynamic linker find an installed copy (LD_LIBRARY_PATH, ldconfig cache).
        for (const char* soname : {"liboo2corelinux64.so.9", "liboo2corelinux64.so"})
        {
            if (TryLoadLibraryFile(soname))  // bare soname: `dlopen` searches the standard library paths
            {
                Firelink::Info(std::format("[oodle] Loaded Oodle library via system search path: {}", s_loadedPath));
                return;
            }
        }
#endif

        Firelink::Warning(std::format(
            "[oodle] {} not found. DCX_KRAK compression/decompression will be unavailable.", DLL_NAME));
    }

    static void EnsureInit()
    {
        std::call_once(s_initFlag, InitOnce);
    }

    static void RequireDLL()
    {
        EnsureInit();
        if (!s_dll)
#ifdef _WIN32
            throw DCXError(
                "Oodle DLL (oo2core_6_win64.dll) not found. "
                "Call Oodle::SetDLLSearchPath() or Oodle::LoadDLL() with the directory containing the DLL, "
                "or place it in your Elden Ring Game folder.");
#else
            throw DCXError(
                "Oodle library (liboo2corelinux64.so) not found. Oodle cannot use the Windows oo2core DLL on Linux. "
                "Obtain a Linux build of Oodle (not distributed with the games), then call Oodle::LoadDLL() with its "
                "directory or file path, put it in your game folder, or set the FIRELINK_OODLE_PATH environment variable.");
#endif
    }

    // ========================================================================
    // Public API
    // ========================================================================

    void SetDLLSearchPath(const std::string& directory)
    {
        std::lock_guard lock(s_mutex);
        s_userSearchPath = directory;
    }

    bool LoadDLL(const std::string& directory)
    {
        std::lock_guard lock(s_mutex);
        // Force past the once_flag by doing the load directly.
        // Also ensure the once_flag won't re-run its default search.
        std::call_once(s_initFlag, [] {});

        if (TryLoadFrom(directory))
        {
            Firelink::Info(std::format("[oodle] Loaded Oodle library from: {}", s_loadedPath));
            return true;
        }
        Firelink::Warning(std::format("[oodle] Failed to load Oodle library from: {}", directory));
        return false;
    }

    bool IsAvailable() noexcept
    {
        EnsureInit();
        return s_dll != nullptr;
    }

    const std::string& LoadedPath() noexcept
    {
        EnsureInit();
        static const std::string empty;
        return s_dll ? s_loadedPath : empty;
    }

    std::vector<std::byte> Decompress(
        const std::byte* compressed, const std::size_t compressed_size,
        const std::size_t decompressed_size)
    {
        RequireDLL();

        const long bufSize = s_getDecodeSize(static_cast<long>(decompressed_size), 1);
        std::vector<std::byte> out(static_cast<std::size_t>(bufSize));

        const long actual = s_decompress(
            compressed, static_cast<long>(compressed_size),
            out.data(), static_cast<long>(decompressed_size),
            FuzzSafe_Yes, CheckCRC_No, Verbosity_None,
            nullptr, 0, nullptr, nullptr, nullptr, 0, ThreadPhase_Unthreaded);

        if (actual <= 0)
        {
            Firelink::Warning(std::format("[oodle] Decompression returned {}. Using expected size {}.", actual, decompressed_size));
            out.resize(decompressed_size);
        }
        else
        {
            out.resize(static_cast<std::size_t>(actual));
        }
        return out;
    }

    std::vector<std::byte> Compress(const std::byte* data, const std::size_t size)
    {
        RequireDLL();

        const long maxSize = s_getCompressedSize(static_cast<long>(size));
        std::vector<std::byte> out(static_cast<std::size_t>(maxSize));

        CompressOptions* opts = s_getDefaultOptions(Compressor_Kraken, Level_Optimal2);
        opts->seekChunkReset = 1;     // required for game compatibility
        opts->seekChunkLen = 0x40000; // default, but explicit for authenticity

        const long actual = s_compress(
            Compressor_Kraken, data, static_cast<long>(size),
            out.data(), Level_Optimal2,
            opts, nullptr, nullptr, nullptr, 0);

        if (actual <= 0)
            throw DCXError("Oodle compression failed (returned " + std::to_string(actual) + ").");

        out.resize(static_cast<std::size_t>(actual));
        return out;
    }

} // namespace Firelink::Oodle

