# FenrirRagdoll (ADR-0053, ADR-0054): ragdolls as ordinary bodies and joints mapped to bones by name, the description
# and builder that make them, and the glue that joins them to a skeleton each tick. It depends on FenrirPhysics,
# FenrirSkeleton and the core, so neither of those knows the other, and reaches physics only through FenrirPhysics's
# public headers. Built with FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_ASSETS (FenrirSkeleton's), with no option of its
# own.
add_library(FenrirRagdoll STATIC
    include/FenrirRagdoll/Components.hpp
    include/FenrirRagdoll/Description.hpp
    include/FenrirRagdoll/Builder.hpp
    include/FenrirRagdoll/Ragdolls.hpp
    include/FenrirRagdoll/DebugLines.hpp

    src/Frames.hpp
    src/Overrides.hpp
    src/Describe.cpp
    src/Build.cpp
    src/Glue.cpp
)

target_link_libraries(FenrirRagdoll PUBLIC FenrirPhysics FenrirSkeleton FenrirAssets FenrirApp FenrirScene FenrirECS
                                           FenrirMath FenrirLogger FenrirTime FenrirDebugDraw)

target_include_directories(FenrirRagdoll PUBLIC include PRIVATE src)

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirRagdoll.api.json,
# the artefact a scripting binding binds, whether or not scripting is built.
fenrir_describe_api(FenrirRagdoll api/FenrirRagdoll.api.toml)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
