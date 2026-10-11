find_package(Threads REQUIRED)

add_library(Iris STATIC
    src/Host.cpp
    src/BridgeFunctions.hpp
    src/Component.cpp
    src/ComponentState.hpp
    src/LogSink.cpp
    src/LogSink.hpp
    src/Member.cpp
    src/MemberState.hpp
    src/Method.cpp
    src/Object.cpp
    src/StringBlock.cpp
    src/StructRegistration.cpp
    src/StructRegistration.hpp
    src/ObjectState.hpp
    src/MethodState.hpp
    src/Names.hpp
    src/Type.cpp
    src/TypeState.hpp
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
    include/Iris/Log.hpp
    include/Iris/Member.hpp
    include/Iris/Method.hpp
    include/Iris/Object.hpp
    include/Iris/Type.hpp
)

target_include_directories(Iris PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(Iris PUBLIC cxx_std_20)
target_link_libraries(Iris PRIVATE Iris::Nethost Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(Iris PROPERTIES CXX_EXTENSIONS OFF)
add_library(Iris::Iris ALIAS Iris)
