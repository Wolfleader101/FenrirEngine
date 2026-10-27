# FenrirWindow's scripting extension (ADR-0029, ADR-0046): the Fenrir.Window C# contract and the FenrirWindowScripting
# library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). The root build adds this directory only when
# FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING are both on, after FenrirInput's extension, whose contract (WindowId,
# the focus event) the window's builds against. The binding (api/FenrirWindow.binding.toml) binds the window's events and
# size (../api/FenrirWindow.api.json) alone, naming FenrirInput's WindowId through FenrirInput's binding: this library
# describes nothing of its own.
fenrir_scripting_contract(Window USES Input)
fenrir_scripting_extension(Window
    SOURCES
        include/FenrirWindowScripting/WindowScripting.hpp
        src/WindowScripting.cpp
        src/WindowScriptingState.hpp
    LINK FenrirWindow FenrirInputScripting
    NO_SELECTION
)
