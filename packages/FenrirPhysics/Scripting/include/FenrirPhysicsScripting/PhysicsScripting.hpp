#pragma once

// FenrirPhysics's scripting extension (ADR-0009), in its own library so FenrirPhysics never links FenrirScripting.
// Built only with FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Physics::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions:
//
//     auto physics = Fenrir::Physics::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {physics.ScriptingExtension()}});
//
// The extension shares the Fenrir.Physics contract assembly, which fenrir_deploy_physics_scripting(<target>) copies
// next to an executable (beside the scripting contract, where Install looks for it).
//
// A native function that does nothing (no physics world, no body, a released mesh) warns in debug builds "once per
// call site": once per process for each place in this library's native code that notices it (its std::source_location)
// and each reason, however many C# call sites or scripts reach it. The record lives as long as the process, across
// Apps and Physics owners.

#include "FenrirPhysics/Physics.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
