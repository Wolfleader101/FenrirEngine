add_library(FenrirECS STATIC
    include/FenrirECS/Entity.hpp
    src/Entity.cpp

    include/FenrirECS/EntityList.hpp
    src/EntityList.cpp

    include/FenrirECS/DefaultComponents.hpp

    include/FenrirECS/WorldTransform.hpp
    src/WorldTransform.cpp

    include/FenrirECS/ComponentType.hpp

    include/FenrirECS/ComponentWatch.hpp
    src/ComponentWatch.cpp

    include/FenrirECS/DynamicComponent.hpp
    src/DynamicComponent.cpp
    src/ByteStorage.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirECS PUBLIC FenrirMath)

target_include_directories(FenrirECS PUBLIC include)
# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON, which needs the libs/catch2 submodule at the repository root (git submodule update --init).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
