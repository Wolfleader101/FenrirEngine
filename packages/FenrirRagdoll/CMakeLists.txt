# FenrirRagdoll (ADR-0053, ADR-0054): ragdolls as ordinary bodies and joints mapped to bones by name, with the glue that
# follows and drives a skeleton. Depends on FenrirPhysics, FenrirSkeleton and the core; built only with physics and
# assets on. SPIKE: the physics map's ticket 55, thrown away once the ragdoll spec is written.
add_library(FenrirRagdoll STATIC
    include/FenrirRagdoll/Ragdoll.hpp
    include/FenrirRagdoll/RagdollDescription.hpp
    include/FenrirRagdoll/Ragdolls.hpp

    src/Describe.cpp
    src/Build.cpp
    src/Glue.cpp
    src/Frames.hpp
)

target_link_libraries(FenrirRagdoll PUBLIC FenrirPhysics FenrirSkeleton FenrirAssets FenrirApp FenrirScene FenrirECS FenrirMath)

target_include_directories(FenrirRagdoll PUBLIC include PRIVATE src)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
