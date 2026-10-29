# --------------------------------------------------------------
# Add a Firelink library, appending '_static' suffix for static
# builds to avoid .lib collision with DLL import libs on MSVC.
# --------------------------------------------------------------
function(add_firelink_library target_name)
    add_library(${target_name} ${ARGN})
    if(MSVC AND NOT BUILD_SHARED_LIBS)
        set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${target_name}_static")
    endif()
endfunction()

# --------------------------------------------------------------
# Generate and include export header for a target.
# --------------------------------------------------------------
function(generate_firelink_export_header target_name api_macro_name)
    include(GenerateExportHeader)
    generate_export_header(${target_name}
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/${target_name}/Export.h
        EXPORT_MACRO_NAME ${api_macro_name}
    )
    target_compile_definitions(${target_name} PRIVATE "${target_name}_EXPORTS")
    target_sources(${target_name}
        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
        "${CMAKE_CURRENT_BINARY_DIR}/include"
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/include/${target_name}/Export.h"
    )
endfunction()

# --------------------------------------------------------------
# Configure installation for a pybind11 module.
#
# Sets output name to '_bindings' and installs the module to
# the specified path under 'pyrelink'. A PYI stub called '_bindings'
# should already be present in the install path.
# --------------------------------------------------------------
macro(configure_install_pybind_module target_name install_path)
    set_target_properties(${target_name} PROPERTIES
        OUTPUT_NAME _bindings
    )

    if(UNIX AND NOT APPLE)
        # Make the extension module self-contained so one wheel works across distributions and inside a host application:
        #  - static libstdc++/libgcc: do not require the system's libstdc++ to be at least as new as our compiler's
        #  - --exclude-libs ALL: do not re-export symbols from the static libraries we link (zlib, zstd, ...)
        #  - -Bsymbolic: resolve our own symbols inside the module, never to a same-named symbol elsewhere in the process
        target_link_options(${target_name} PRIVATE
            -static-libstdc++ -static-libgcc
            -Wl,--exclude-libs,ALL
            -Wl,-Bsymbolic
        )
    endif()

    # Install the module
    install(TARGETS ${target_name}
            RUNTIME DESTINATION pyrelink/${install_path}
            LIBRARY DESTINATION pyrelink/${install_path}
    )
endmacro()
