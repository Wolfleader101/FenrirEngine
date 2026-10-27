# FenrirAssets's scripting extension (ADR-0029, ADR-0043, ADR-0046): the Fenrir.Assets C# contract and the
# FenrirAssetsScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING are both on. The binding (api/FenrirAssets.binding.toml) binds
# FenrirAssets's shared types (../api/FenrirAssets.api.json) and this library's own: the handles are bound types, mapped
# by hand (include/FenrirAssetsScripting/AssetMappings.hpp), and the owner C# names and the plain data the natives take in
# C#'s form (include/FenrirAssetsScripting/ScriptAssets.hpp) are described as api/FenrirAssetsScripting.api.json. Its
# mappings header is public because FenrirRenderer's binding uses this one.
fenrir_scripting_contract(Assets)
fenrir_scripting_extension(Assets
    SOURCES
        include/FenrirAssetsScripting/AssetHandleBits.hpp
        include/FenrirAssetsScripting/AssetMappings.hpp
        include/FenrirAssetsScripting/AssetResolve.hpp
        include/FenrirAssetsScripting/AssetsScripting.hpp
        include/FenrirAssetsScripting/ScriptAssets.hpp
        src/AssetsScripting.cpp
        src/AssetsScriptingState.hpp
    LINK FenrirAssets
)
