set(FENRIR_DOTNET_NATIVE_DIR "" CACHE PATH
    "Target SDK host pack's runtimes/<rid>/native directory containing nethost"
)

if(NOT IS_DIRECTORY "${FENRIR_DOTNET_NATIVE_DIR}")
    message(FATAL_ERROR
        "Set FENRIR_DOTNET_NATIVE_DIR to the target .NET SDK host pack's native directory. "
        "See packages/FenrirScripting/README.md for its layout."
    )
endif()

get_filename_component(fenrir_dotnet_native_dir "${FENRIR_DOTNET_NATIVE_DIR}" ABSOLUTE)
set(fenrir_nethost_runtime
    "${fenrir_dotnet_native_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}nethost${CMAKE_SHARED_LIBRARY_SUFFIX}"
)

foreach(fenrir_nethost_file IN ITEMS "${fenrir_dotnet_native_dir}/nethost.h" "${fenrir_nethost_runtime}")
    if(NOT EXISTS "${fenrir_nethost_file}" OR IS_DIRECTORY "${fenrir_nethost_file}")
        message(FATAL_ERROR "Required nethost file is missing: ${fenrir_nethost_file}")
    endif()
endforeach()

add_library(FenrirScriptingNethost SHARED IMPORTED)
set_target_properties(FenrirScriptingNethost PROPERTIES
    IMPORTED_LOCATION "${fenrir_nethost_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${fenrir_dotnet_native_dir}"
)

if(WIN32)
    # The SDK ships a DLL import library named nethost.lib on Windows.
    set(fenrir_nethost_import_library "${fenrir_dotnet_native_dir}/nethost.lib")
    if(NOT EXISTS "${fenrir_nethost_import_library}" OR IS_DIRECTORY "${fenrir_nethost_import_library}")
        message(FATAL_ERROR "Required nethost import library is missing: ${fenrir_nethost_import_library}")
    endif()
    set_target_properties(FenrirScriptingNethost PROPERTIES
        IMPORTED_IMPLIB "${fenrir_nethost_import_library}"
    )
endif()
