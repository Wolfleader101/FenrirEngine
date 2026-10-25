#pragma once

// FenrirInput's scripting extension (ADR-0029), in its own library so FenrirInput never links FenrirScripting. Built
// only with FENRIR_ENABLE_SCRIPTING on (FenrirInput itself is always built). Include this header to call
// Input::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions:
//
//     auto input = Fenrir::Input::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Input::ScriptingExtension()}});
//
// The extension shares the Fenrir.Input contract assembly, which fenrir_deploy_input_scripting(<target>) copies next to
// an executable (beside the scripting contract, where Install looks for it).
//
// A native function that does nothing (no input in the app, a pad that is not connected) warns in debug builds "once
// per call site": once per Scripting given the extension for each place in this library's native code that notices it
// (its std::source_location), however many C# call sites or scripts reach it.

#include "FenrirInput/Input.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
