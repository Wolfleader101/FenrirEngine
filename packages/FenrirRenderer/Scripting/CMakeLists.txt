# FenrirRenderer's scripting extension (ADR-0029, ADR-0046): the Fenrir.Renderer C# contract and the
# FenrirRendererScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING are both on, after FenrirInput's and FenrirAssets's extensions, whose
# contracts (WindowId, asset handles) the renderer's builds against. The binding (api/FenrirRenderer.binding.toml) binds
# the renderer's components, resources and stats (../api/FenrirRenderer.api.json) alone, naming the entity value the
# natives take through the built-in binding and the math, input and assets bindings' types through theirs: this library
# describes nothing of its own.
fenrir_scripting_contract(Renderer USES Input Assets)
fenrir_scripting_extension(Renderer
    SOURCES
        include/FenrirRendererScripting/RendererScripting.hpp
        src/RendererScripting.cpp
        src/RendererScriptingState.hpp
        src/DeprecatedAmbientLight.hpp
        src/DeprecatedAmbientLight.cpp
    LINK FenrirRenderer FenrirAssetsScripting FenrirInputScripting
    NO_SELECTION
)
