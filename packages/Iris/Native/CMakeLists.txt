find_package(Threads REQUIRED)

add_library(Iris STATIC
    src/Host.cpp
    src/BridgeFunctions.hpp
    src/Component.cpp
    src/ComponentState.hpp
    src/HostFxrDiscovery.cpp
    src/HostFxrDiagnostics.cpp
    src/HostFxrDiagnostics.hpp
    src/HostFxrLibrary.cpp
    src/HostFxrLibrary.hpp
    src/ManagedDiagnosticBuffer.hpp
    src/StringEncoding.cpp
    src/StringEncoding.hpp
    src/Utf8Buffer.hpp
    include/Iris/Host.hpp
    include/Iris/CallResult.hpp
    include/Iris/Component.hpp
)

target_include_directories(Iris PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(Iris PUBLIC cxx_std_20)
target_link_libraries(Iris PRIVATE IrisNethost Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(Iris PROPERTIES CXX_EXTENSIONS OFF)
add_library(Iris::Iris ALIAS Iris)
