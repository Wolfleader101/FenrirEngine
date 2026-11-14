# FenrirUI's scripting extension (ADR-0029, ADR-0039, ADR-0046, ADR-0061): the Fenrir.UI C# contract and the
# FenrirUIScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when the UI
# and FENRIR_ENABLE_SCRIPTING are on, after the input, assets, text and painter extensions, whose contracts it builds
# on. The binding (api/FenrirUI.binding.toml) binds this library's own description, api/FenrirUIScripting.api.json:
# the settings C# makes a root with (include/.../ScriptUI.hpp) and FenrirUI's plain data they and the natives name.
fenrir_scripting_contract(UI USES Input Assets Text Painter)
fenrir_scripting_extension(UI
    SOURCES
        include/FenrirUIScripting/ScriptUI.hpp
        include/FenrirUIScripting/UIScripting.hpp
        src/UIScripting.cpp
        src/UIScriptingState.hpp
    LINK FenrirUI FenrirPainterScripting FenrirTextScripting FenrirAssetsScripting FenrirInputScripting
    DESCRIPTIONS api/FenrirUIScripting.api.json
)
