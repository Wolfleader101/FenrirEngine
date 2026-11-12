# FenrirText's scripting extension (ADR-0029, ADR-0046, ADR-0061): the Fenrir.Text C# contract and the
# FenrirTextScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_TEXT and FENRIR_ENABLE_SCRIPTING are both on. The binding (api/FenrirText.binding.toml) binds this
# library's own description, api/FenrirTextScripting.api.json: the structs C# passes (include/.../ScriptText.hpp) and
# the plain data of FenrirText's they name. FenrirPainter's binding uses this one, so the mappings header is public.
fenrir_scripting_contract(Text)
fenrir_scripting_extension(Text
    SOURCES
        include/FenrirTextScripting/ScriptText.hpp
        include/FenrirTextScripting/TextScripting.hpp
        include/FenrirTextScripting/TextScriptingState.hpp
        src/TextScripting.cpp
    LINK FenrirText
    DESCRIPTIONS api/FenrirTextScripting.api.json
)
