# FenrirECS: entities, components and the entity list, with the components every entity has.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
add_library(FenrirECS STATIC
    include/FenrirECS/Entity.hpp
    src/Entity.cpp

    include/FenrirECS/EntityList.hpp
    src/EntityList.cpp

    include/FenrirECS/DefaultComponents.hpp

    include/FenrirECS/WorldTransform.hpp
    src/WorldTransform.cpp

    include/FenrirECS/ComponentType.hpp
    include/FenrirECS/TypeAttachments.hpp
    src/TypeAttachments.cpp

    include/FenrirECS/ComponentWatch.hpp
    src/ComponentWatch.cpp

    include/FenrirECS/TrackedComponents.hpp
    src/TrackedComponents.cpp

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

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirECS.api.json.
fenrir_describe_api(FenrirECS api/FenrirECS.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirECS/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirECS api/FenrirECS.api.toml)
target_sources(FenrirECS PRIVATE include/FenrirECS/Generated/FenrirECSTypes.hpp
                                 src/Generated/FenrirECSTypes.cpp)
target_link_libraries(FenrirECS PUBLIC FenrirTypes)
