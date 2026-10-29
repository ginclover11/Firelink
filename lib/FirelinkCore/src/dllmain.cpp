#include "pch.h"

// Windows-only DLL entry point. On other platforms this translation unit is empty (and is not even compiled; see
// `lib/FirelinkCore/CMakeLists.txt`).
#ifdef _WIN32

// Entry point for the DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            // DLL is being loaded into the virtual address space of the current process
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            // DLL is being unloaded
            break;
        default:
            break;
    }
    return TRUE;
}

#endif // _WIN32
