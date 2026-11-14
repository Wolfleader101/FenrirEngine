#pragma once

// FenrirRagdoll's scripting extension (the ragdolls spec, ticket 56), in its own library so FenrirRagdoll never links
// FenrirScripting. Built only with FENRIR_ENABLE_PHYSICS, FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING on. Include
// this header to call Ragdolls::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions after the
// skeleton extension, whose Fenrir.Skeleton contract (BoneName) this one builds against:
//
//     auto ragdolls = Fenrir::Ragdolls::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Assets::ScriptingExtension(), Fenrir::Skeletons::ScriptingExtension(),
//      Fenrir::Physics::ScriptingExtension(), Fenrir::Ragdolls::ScriptingExtension()}});
//
// The extension shares the Fenrir.Ragdoll contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Ragdoll)
// copies next to an executable with the Fenrir.Skeleton and Fenrir.Assets contracts it builds against. Its mirrors are
// Ragdoll and RagdollBody (writable, their states read-only), the modes and HumanoidBones; its natives describe a
// humanoid into a description the extension keeps for the scene (C# holds a RagdollDescriptionHandle), build it,
// destroy a ragdoll and clear its overrides. A description described and never built goes when its scene does.
//
// A native function that does nothing (an entity that is not alive or not a Ready skeleton entity, a refused bone map,
// a stale handle, a buffer too small) warns in debug builds once per call site: once per Scripting given the extension
// for each place in this library's native code that notices it and each reason.

#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
