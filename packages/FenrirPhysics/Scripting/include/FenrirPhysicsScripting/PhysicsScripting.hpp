#pragma once

// FenrirPhysics's scripting extension (ADR-0009), in its own library so FenrirPhysics never links FenrirScripting.
// Built only with FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Physics::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions:
//
//     auto physics = Fenrir::Physics::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Physics::ScriptingExtension()}});
//
// The extension shares the Fenrir.Physics contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Physics)
// copies next to an executable (beside the scripting contract, where Install looks for it).
//
// Its native functions act on the active scene's physics world, collision meshes included (they are the Physics
// owner's, reached through the world), so they need no owner. With no world, no body or a released mesh they do nothing
// and return false or zero, and an output they write reads as its default. In debug builds each such case is warned
// about once per extension made here, for each native function that notices it and each reason, under the native's name
// (`[physics scripting] PhysicsNative.Raycast: ...`): not once per C# call site, so a second script calling the same
// function the same wrong way is not warned about again. Each call makes a new extension with its own record, which
// lasts as long as the Scripting given it.

#include "FenrirPhysics/Physics.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
