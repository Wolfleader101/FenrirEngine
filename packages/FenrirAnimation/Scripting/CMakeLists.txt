# FenrirAnimation's scripting extension (ADR-0039, ADR-0046): the Fenrir.Animation C# contract and the
# FenrirAnimationScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING are both on, after FenrirAssets's and FenrirSkeleton's extensions,
# whose contracts (asset handles, PoseState) this one builds against. The binding (api/FenrirAnimation.binding.toml)
# binds FenrirAnimation's components, events and playback info (../api/FenrirAnimation.api.json); this library
# describes nothing of its own.
fenrir_scripting_contract(Animation USES Assets Skeleton)
fenrir_scripting_extension(Animation
    SOURCES
        include/FenrirAnimationScripting/AnimationScripting.hpp
        src/AnimationScripting.cpp
        src/AnimationScriptingState.hpp
    LINK FenrirAnimation FenrirAssetsScripting FenrirSkeletonScripting
    NO_SELECTION
)
