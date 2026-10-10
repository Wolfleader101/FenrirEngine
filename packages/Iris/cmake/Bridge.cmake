# Builds the managed bridge (Iris.Bridge) into IRIS_BRIDGE_DIR with the .NET SDK. The bridge is always
# built in Release: it is one process-wide assembly shared by every configuration of the native code.
# It rebuilds only when its sources change.

if(NOT IRIS_DOTNET_EXECUTABLE)
    find_program(IRIS_DOTNET_EXECUTABLE NAMES dotnet HINTS "$ENV{DOTNET_ROOT}"
        DOC "dotnet executable used to build the managed bridge")
endif()
if(NOT IRIS_DOTNET_EXECUTABLE)
    message(FATAL_ERROR "dotnet was not found. Install the .NET 10 SDK, set IRIS_DOTNET_EXECUTABLE, "
        "or set IRIS_BUILD_BRIDGE=OFF and IRIS_PREBUILT_BRIDGE_DIR to a directory holding a built bridge.")
endif()

set(IRIS_BRIDGE_DIR "${CMAKE_BINARY_DIR}/iris/bridge")
set(iris_bridge_project "${CMAKE_CURRENT_LIST_DIR}/../Managed/Iris.Bridge/Iris.Bridge.csproj")
file(GLOB iris_bridge_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../Managed/Iris.Bridge/*.cs")

add_custom_command(
    OUTPUT "${IRIS_BRIDGE_DIR}/Iris.Bridge.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${iris_bridge_project}" --configuration Release
        --output "${IRIS_BRIDGE_DIR}" -nologo -v:q
    DEPENDS "${iris_bridge_project}" ${iris_bridge_sources}
    COMMENT "Building the Iris managed bridge"
    VERBATIM
)
add_custom_target(IrisBridge ALL DEPENDS "${IRIS_BRIDGE_DIR}/Iris.Bridge.dll")
