#pragma once

// FenrirAnimation's scripting extension (ADR-0039), in its own library so FenrirAnimation never links FenrirScripting.
// Built only with FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING on. Include this header to call
// Animation::ScriptingExtension(), and add what it returns to ScriptingConfig::extensions, after the assets and
// skeleton extensions whose contracts its own builds against:
//
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Assets::ScriptingExtension(), Fenrir::Skeletons::ScriptingExtension(),
//      Fenrir::Animation::ScriptingExtension()}});
//
// The extension shares the Fenrir.Animation contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS
// Animation) copies next to an executable with the contracts it builds against. Its mirrors are the Animator and
// AnimationLayer components and the AnimationEvent, ClipFinished and AnimationEventsDropped events; its natives the
// playback reads and seeks, the clip event reads and edits, and the bone mask edits.
//
// A native function that does nothing (no asset store, a stale handle, an entity that is not alive, an event index out
// of range) warns in debug builds once per call site: once per Scripting given the extension for each place in this
// library's native code that notices it and each reason.

#include "FenrirAnimation/Animation.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
