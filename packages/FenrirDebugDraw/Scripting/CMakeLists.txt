# FenrirDebugDraw's scripting extension (ADR-0029, ADR-0046): the Fenrir.DebugDraw C# contract and the
# FenrirDebugDrawScripting library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_SCRIPTING is on (FenrirDebugDraw itself is always built). The binding (api/FenrirDebugDraw.binding.toml)
# binds nothing of FenrirDebugDraw's own headers: C# reaches debug drawing only through the natives, and the options they
# take (src/ScriptOptions.hpp) are described by this library as api/FenrirDebugDrawScripting.api.json, the one
# description it binds.
fenrir_scripting_contract(DebugDraw)
fenrir_scripting_extension(DebugDraw
    SOURCES
        include/FenrirDebugDrawScripting/DebugDrawScripting.hpp
        src/DebugDrawScripting.cpp
        src/DebugDrawScriptingState.hpp
        src/ScriptOptions.hpp
    LINK FenrirDebugDraw
    DESCRIPTIONS api/FenrirDebugDrawScripting.api.json
)
