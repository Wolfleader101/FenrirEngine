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

#include "FenrirPhysics/Physics.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
