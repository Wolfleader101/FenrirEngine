# FenrirPainter's scripting extension (ADR-0029, ADR-0046, ADR-0061): the Fenrir.Painter C# contract and the
# FenrirPainterScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when the
# painter (with the renderer and text) and FENRIR_ENABLE_SCRIPTING are on, after the input, assets and text
# extensions, whose contracts it builds on. It links FenrirRenderer for the window overlay its painters submit to;
# FenrirPainter itself knows no renderer. The binding (api/FenrirPainter.binding.toml) binds this library's own
# description, api/FenrirPainterScripting.api.json: the structs C# passes (include/.../ScriptPainter.hpp) and the
# painter's plain data they name. FenrirUI's binding is to build on this one, so the mappings header is public.
fenrir_scripting_contract(Painter USES Input Assets Text)
fenrir_scripting_extension(Painter
    SOURCES
        include/FenrirPainterScripting/PainterScripting.hpp
        include/FenrirPainterScripting/ScriptPainter.hpp
        src/PainterScripting.cpp
        include/FenrirPainterScripting/PainterScriptingState.hpp
    LINK FenrirPainter FenrirRenderer FenrirTextScripting FenrirAssetsScripting FenrirInputScripting
    DESCRIPTIONS api/FenrirPainterScripting.api.json
)
