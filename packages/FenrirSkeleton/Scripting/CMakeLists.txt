# FenrirSkeleton's scripting extension (ADR-0039, ADR-0046): the Fenrir.Skeleton C# contract and the
# FenrirSkeletonScripting library holding the native functions behind it and the pose stages it hosts, built by
# FenrirScripting's extension module (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds
# this directory only when FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING are both on, after FenrirAssets's extension,
# whose contract (asset handles) this one builds against. The binding (api/FenrirSkeleton.binding.toml) binds
# FenrirSkeleton's components, shared struct and enums (../api/FenrirSkeleton.api.json); this library describes nothing
# of its own. Its mappings header is public because FenrirAnimation's binding uses this one.
fenrir_scripting_contract(Skeleton USES Assets)
fenrir_scripting_extension(Skeleton
    SOURCES
        include/FenrirSkeletonScripting/SkeletonScripting.hpp
        src/SkeletonScripting.cpp
        src/SkeletonScriptingState.hpp
    LINK FenrirSkeleton FenrirAssetsScripting
    NO_SELECTION
)
