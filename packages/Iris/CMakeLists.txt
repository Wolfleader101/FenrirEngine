cmake_minimum_required(VERSION 3.20)
project(Iris VERSION 0.3.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(iris_is_top_level ON)
else()
    set(iris_is_top_level OFF)
endif()

option(IRIS_BUILD_BRIDGE "Build the managed bridge with the .NET SDK" ON)
option(IRIS_BUILD_EXAMPLES "Build the hosting example" ${iris_is_top_level})
option(IRIS_BUILD_TESTS "Build the hosting checks" OFF)
option(IRIS_INSTALL "Generate install rules and a find_package(Iris) package" ${iris_is_top_level})

# A parent project adds Iris only when it wants .NET, so pack discovery runs unconditionally here.
include(cmake/Nethost.cmake)
# IRIS_BRIDGE_DIR holds Iris.Bridge.dll with its .runtimeconfig.json and .deps.json.
if(IRIS_BUILD_BRIDGE)
    include(cmake/Bridge.cmake)
else()
    set(IRIS_PREBUILT_BRIDGE_DIR "" CACHE PATH "Directory holding a prebuilt Iris bridge (when IRIS_BUILD_BRIDGE is OFF)")
    if(NOT EXISTS "${IRIS_PREBUILT_BRIDGE_DIR}/Iris.Bridge.dll")
        message(FATAL_ERROR "IRIS_BUILD_BRIDGE is OFF: set IRIS_PREBUILT_BRIDGE_DIR to a directory holding "
            "Iris.Bridge.dll, Iris.Bridge.runtimeconfig.json and Iris.Bridge.deps.json.")
    endif()
    set(IRIS_BRIDGE_DIR "${IRIS_PREBUILT_BRIDGE_DIR}")
endif()
include(cmake/IrisDeploy.cmake)
if(NOT iris_is_top_level)
    # A parent project that adds Iris needs the bridge location to call iris_deploy.
    set(IRIS_BRIDGE_DIR "${IRIS_BRIDGE_DIR}" PARENT_SCOPE)
endif()
include(GNUInstallDirs)

add_subdirectory(Native)

if(IRIS_BUILD_EXAMPLES)
    add_subdirectory(Examples/Hosting)
endif()

if(IRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(IRIS_INSTALL)
    include(CMakePackageConfigHelpers)

    set(IRIS_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Iris")
    set(IRIS_INSTALL_BRIDGE_DIR "${CMAKE_INSTALL_DATADIR}/Iris/bridge")
    # nethost sits with executables on Windows and with libraries elsewhere.
    get_filename_component(iris_nethost_runtime_name "${iris_nethost_runtime}" NAME)
    if(WIN32)
        set(iris_nethost_runtime_destination "${CMAKE_INSTALL_BINDIR}")
        set(IRIS_INSTALL_NETHOST_IMPLIB "${CMAKE_INSTALL_LIBDIR}/nethost.lib")
    else()
        set(iris_nethost_runtime_destination "${CMAKE_INSTALL_LIBDIR}")
        set(IRIS_INSTALL_NETHOST_IMPLIB "")
    endif()
    set(IRIS_INSTALL_NETHOST_RUNTIME "${iris_nethost_runtime_destination}/${iris_nethost_runtime_name}")

    install(TARGETS Iris EXPORT IrisTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY Native/include/Iris DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT IrisTargets NAMESPACE Iris:: DESTINATION "${IRIS_INSTALL_CMAKE_DIR}")
    install(FILES "${iris_nethost_runtime}" DESTINATION "${iris_nethost_runtime_destination}")
    if(WIN32)
        install(FILES "${iris_nethost_import_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(FILES
        "${IRIS_BRIDGE_DIR}/Iris.Bridge.dll"
        "${IRIS_BRIDGE_DIR}/Iris.Bridge.runtimeconfig.json"
        "${IRIS_BRIDGE_DIR}/Iris.Bridge.deps.json"
        DESTINATION "${IRIS_INSTALL_BRIDGE_DIR}")

    configure_package_config_file(cmake/IrisConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake"
        INSTALL_DESTINATION "${IRIS_INSTALL_CMAKE_DIR}"
        PATH_VARS IRIS_INSTALL_BRIDGE_DIR IRIS_INSTALL_NETHOST_RUNTIME)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/IrisConfigVersion.cmake"
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/IrisConfigVersion.cmake"
        cmake/IrisDeploy.cmake
        DESTINATION "${IRIS_INSTALL_CMAKE_DIR}")
endif()
