#pragma once

// FenrirRenderer's scripting extension (ADR-0029), in its own library so FenrirRenderer never links FenrirScripting.
// Built only with FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Renderer::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions after Input's and Assets's,
// whose contracts (WindowId, asset handles) the renderer's builds on:
//
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Input::ScriptingExtension(), Fenrir::Assets::ScriptingExtension(),
//      Fenrir::Renderer::ScriptingExtension()}});
//
// The extension shares the Fenrir.Renderer contract assembly, which fenrir_deploy_renderer_scripting(<target>) copies
// next to an executable (beside the scripting contract, where Install looks for it). It needs no Renderer installed:
// its components and resources are plain data, SpawnModel needs only the asset store, and picking, bounds and stats do
// nothing without a renderer.
//
// A native function that does nothing (no renderer or asset store in the app, an entity that is not alive or has no
// Camera, a stale model) warns in debug builds "once per call site": once per process for each place in this
// library's native code that notices it (its std::source_location) and each reason, however many C# call sites or
// scripts reach it.

#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
