# FenrirRagdoll's scripting extension (the ragdolls spec, ticket 56; ADR-0046): the Fenrir.Ragdoll C# contract and the
# FenrirRagdollScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_PHYSICS, FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING are all on, after the skeleton extension,
# whose contract (BoneName) this one builds against, with the assets contract that one uses. The binding
# (api/FenrirRagdoll.binding.toml) binds FenrirRagdoll's components, modes and bone map (../api/FenrirRagdoll.api.json)
# and this library's description handle (include/FenrirRagdollScripting/RagdollDescriptionHandle.hpp,
# api/FenrirRagdollScripting.api.json).
fenrir_scripting_contract(Ragdoll USES Assets Skeleton)
fenrir_scripting_extension(Ragdoll
    SOURCES
        include/FenrirRagdollScripting/RagdollDescriptionHandle.hpp
        include/FenrirRagdollScripting/RagdollMappings.hpp
        include/FenrirRagdollScripting/RagdollScripting.hpp
        src/RagdollScripting.cpp
        src/RagdollScriptingState.hpp
    LINK FenrirRagdoll FenrirSkeletonScripting
)
