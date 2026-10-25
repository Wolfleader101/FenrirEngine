#pragma once

// FenrirAssets's scripting extension (ADR-0029), in its own library so FenrirAssets never links FenrirScripting. Built
// only with FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Assets::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions:
//
//     auto assets = Fenrir::Assets::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Assets::ScriptingExtension()}});
//
// The extension shares the Fenrir.Assets contract assembly, which fenrir_deploy_assets_scripting(<target>) copies next
// to an executable (beside the scripting contract, where Install looks for it). Another package's binding whose
// components or resources hold asset handles (FenrirRenderer's) uses this one's (api/FenrirAssets.binding.toml), whose
// generated mappings are "FenrirAssetsScripting/Generated/FenrirAssetsMappings.hpp".
//
// A native function that does nothing (no asset store in the app, a stale or empty handle) warns in debug builds "once
// per call site": once per Scripting given the extension for each place in this library's native code that notices it
// (its std::source_location) and each reason, however many C# call sites or scripts reach it.

#include "FenrirAssets/Assets.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
