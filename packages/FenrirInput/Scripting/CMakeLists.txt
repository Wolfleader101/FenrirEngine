# FenrirInput's scripting extension (ADR-0029, ADR-0046): the Fenrir.Input C# contract and the FenrirInputScripting
# library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_SCRIPTING is on (FenrirInput itself is always built). The binding (api/FenrirInput.binding.toml) binds
# FenrirInput's events and ids (../api/FenrirInput.api.json) and the InputClaims copy C# reads
# (include/FenrirInputScripting/ScriptInputClaims.hpp, api/FenrirInputScripting.api.json). Its mappings header is public
# because FenrirWindow's and FenrirRenderer's bindings use this one.
fenrir_scripting_contract(Input)
fenrir_scripting_extension(Input
    SOURCES
        include/FenrirInputScripting/InputMappings.hpp
        include/FenrirInputScripting/InputScripting.hpp
        include/FenrirInputScripting/ScriptInputClaims.hpp
        src/InputClaimsResource.cpp
        src/InputClaimsResource.hpp
        src/InputScripting.cpp
        src/InputScriptingState.hpp
    LINK FenrirInput
)
