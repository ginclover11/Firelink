# Core
add_subdirectory(FirelinkCore)
add_subdirectory(FirelinkFLVER)

# Game memory hooks: these attach to a running (Windows) game process, so they only exist on Windows.
if(WIN32)
    # Dark Souls: Remastered
    add_subdirectory(FirelinkDSRHook)

    # Elden Ring
    add_subdirectory(FirelinkERHook)
endif()

# Elden Ring
#add_subdirectory(FirelinkERHavok)  # TODO: Not ready.
add_subdirectory(FirelinkERMaps)
