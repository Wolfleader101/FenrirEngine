#pragma once

// FenrirWindow's scripting extension (ADR-0029), in its own library so FenrirWindow never links FenrirScripting. Built
// only with FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Window::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions after input's, whose contract
// the window's uses:
//
//     auto window = Fenrir::Window::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Input::ScriptingExtension(), Fenrir::Window::ScriptingExtension()}});
//
// The extension shares the Fenrir.Window contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Window)
// copies next to an executable (with Fenrir.Input, which it builds against, beside the scripting contract, where
// Install looks for them).
//
// A native function that does nothing (no window, a window id that names none) warns in debug builds "once per call
// site": once per Scripting given the extension for each place in this library's native code that notices it (its
// std::source_location), however many C# call sites or scripts reach it.

#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirWindow/Window.hpp"
