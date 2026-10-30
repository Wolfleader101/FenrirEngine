#pragma once

// FenrirSkeleton's scripting extension (ADR-0039), in its own library so FenrirSkeleton never links FenrirScripting.
// Built only with FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Skeletons::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions, after installing
// FenrirSkeleton, whose pose pipeline the extension's stages are placed in when a script loads:
//
//     auto skeletons = Fenrir::Skeletons::Install(app);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Assets::ScriptingExtension(), Fenrir::Skeletons::ScriptingExtension()}});
//
// The extension shares the Fenrir.Skeleton contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS
// Skeleton) copies next to an executable with the Fenrir.Assets contract it builds against. It declares the stages
// Fenrir.Skeleton.Animate, .Modify and .Late (C#'s PoseStages): each group of a script's stage systems is placed in its
// pose stage at its order, after C++ work of an equal order, and the changes they record play back when each pass of
// the pipeline returns. Another package's binding whose types hold skeleton types (FenrirAnimation's) uses this one's
// (api/FenrirSkeleton.binding.toml), whose generated mappings are
// "FenrirSkeletonScripting/Generated/FenrirSkeletonMappings.hpp".
//
// A native function that does nothing (an entity that is not a Ready skeleton entity, a bone out of range, a stale
// handle, a stage native outside its stage) warns in debug builds once per call site: once per Scripting given the
// extension for each place in this library's native code that notices it and each reason.

#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
