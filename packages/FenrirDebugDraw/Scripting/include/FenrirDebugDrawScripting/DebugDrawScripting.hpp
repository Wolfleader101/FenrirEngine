#pragma once

// FenrirDebugDraw's scripting extension (ADR-0029), in its own library so FenrirDebugDraw never links FenrirScripting.
// Built only with FENRIR_ENABLE_SCRIPTING on (FenrirDebugDraw itself is always built). Include this header to call
// DebugDrawing::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions:
//
//     auto debugDrawing = Fenrir::DebugDrawing::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::DebugDrawing::ScriptingExtension()}});
//
// The extension shares the Fenrir.DebugDraw contract assembly, which fenrir_deploy_debugdraw_scripting(<target>) copies
// next to an executable (beside the scripting contract, where Install looks for it). Its static `DebugDraw` facade is
// the only path from C# to debug drawing: each call is the C++ DebugDraw call of the same name on the active scene's
// DebugDraw.
//
// A native function that does nothing (no DebugDraw in the active scene, a relativeTo entity that is not alive) warns
// in debug builds "once per call site": once for each place in this library's native code that notices it (its
// std::source_location) and each reason, however many C# call sites or scripts reach it. The record is the extension's
// state, so it lasts as long as the Scripting given the extension.

#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
