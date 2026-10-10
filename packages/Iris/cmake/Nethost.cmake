set(IRIS_DOTNET_NATIVE_DIR "" CACHE PATH
    "Target SDK host pack's native directory; empty enables automatic discovery"
)

if(IRIS_DOTNET_NATIVE_DIR STREQUAL "")
    include("${CMAKE_CURRENT_LIST_DIR}/DiscoverDotNetNative.cmake")
    iris_discover_dotnet_native(iris_dotnet_native_dir)
elseif(IS_DIRECTORY "${IRIS_DOTNET_NATIVE_DIR}")
    get_filename_component(iris_dotnet_native_dir "${IRIS_DOTNET_NATIVE_DIR}" ABSOLUTE)
else()
    message(FATAL_ERROR
        "Set IRIS_DOTNET_NATIVE_DIR to the target .NET SDK host pack's native directory. "
        "See packages/Iris/README.md for its layout."
    )
endif()

message(STATUS "Iris .NET native directory: ${iris_dotnet_native_dir}")
set(iris_nethost_runtime
    "${iris_dotnet_native_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}nethost${CMAKE_SHARED_LIBRARY_SUFFIX}"
)

foreach(iris_nethost_file IN ITEMS
    "${iris_dotnet_native_dir}/nethost.h"
    "${iris_dotnet_native_dir}/hostfxr.h"
    "${iris_dotnet_native_dir}/coreclr_delegates.h"
    "${iris_nethost_runtime}"
)
    if(NOT EXISTS "${iris_nethost_file}" OR IS_DIRECTORY "${iris_nethost_file}")
        message(FATAL_ERROR "Required native hosting file is missing: ${iris_nethost_file}")
    endif()
endforeach()

add_library(IrisNethost SHARED IMPORTED)
set_target_properties(IrisNethost PROPERTIES
    IMPORTED_LOCATION "${iris_nethost_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${iris_dotnet_native_dir}"
)

if(WIN32)
    # The SDK ships a DLL import library named nethost.lib on Windows.
    set(iris_nethost_import_library "${iris_dotnet_native_dir}/nethost.lib")
    if(NOT EXISTS "${iris_nethost_import_library}" OR IS_DIRECTORY "${iris_nethost_import_library}")
        message(FATAL_ERROR "Required nethost import library is missing: ${iris_nethost_import_library}")
    endif()
    set_target_properties(IrisNethost PROPERTIES
        IMPORTED_IMPLIB "${iris_nethost_import_library}"
    )
endif()
