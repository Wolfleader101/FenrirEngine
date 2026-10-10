cmake_minimum_required(VERSION 3.20)
project(Iris VERSION 0.1.0 LANGUAGES CXX)

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
if(IRIS_BUILD_BRIDGE)
    include(cmake/Bridge.cmake)
endif()
include(cmake/IrisDeploy.cmake)
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
    get_filename_component(iris_nethost_runtime_name "${iris_nethost_runtime}" NAME)
    set(IRIS_INSTALL_NETHOST_RUNTIME "${CMAKE_INSTALL_BINDIR}/${iris_nethost_runtime_name}")
    set(IRIS_INSTALL_NETHOST_IMPLIB "${CMAKE_INSTALL_LIBDIR}/nethost.lib")

    install(TARGETS Iris EXPORT IrisTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY Native/include/Iris DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT IrisTargets NAMESPACE Iris:: DESTINATION "${IRIS_INSTALL_CMAKE_DIR}")
    install(FILES "${iris_nethost_runtime}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    if(WIN32)
        install(FILES "${iris_nethost_import_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
    if(IRIS_BUILD_BRIDGE)
        install(FILES
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.dll"
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.runtimeconfig.json"
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.deps.json"
            DESTINATION "${IRIS_INSTALL_BRIDGE_DIR}")
    endif()

    configure_package_config_file(cmake/IrisConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake"
        INSTALL_DESTINATION "${IRIS_INSTALL_CMAKE_DIR}"
        PATH_VARS IRIS_INSTALL_BRIDGE_DIR IRIS_INSTALL_NETHOST_RUNTIME IRIS_INSTALL_NETHOST_IMPLIB)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/IrisConfigVersion.cmake"
        COMPATIBILITY SameMinorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/IrisConfigVersion.cmake"
        cmake/IrisDeploy.cmake
        DESTINATION "${IRIS_INSTALL_CMAKE_DIR}")
endif()
