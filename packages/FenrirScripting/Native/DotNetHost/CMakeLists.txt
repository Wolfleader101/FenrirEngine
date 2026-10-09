find_package(Threads REQUIRED)

add_library(FenrirDotNetHost STATIC
    src/DotNetHost.cpp
    src/HostFxrDiscovery.cpp
    src/HostFxrLibrary.cpp
    src/HostFxrLibrary.hpp
    include/FenrirDotNetHost/DotNetHost.hpp
)

target_include_directories(FenrirDotNetHost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(FenrirDotNetHost PUBLIC cxx_std_20)
target_link_libraries(FenrirDotNetHost PRIVATE FenrirScriptingNethost Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(FenrirDotNetHost PROPERTIES CXX_EXTENSIONS OFF)
