#pragma once

// FenrirPainter's scripting extension (ADR-0029, ADR-0061), in its own library so FenrirPainter never links
// FenrirScripting. Built only with FENRIR_ENABLE_SCRIPTING and the painter (FENRIR_ENABLE_RENDERER and
// FENRIR_ENABLE_TEXT) on. Install the renderer (for the window overlay) and the painter (so the asset store has
// `builtin:painter`), then add what Painting::ScriptingExtension(text) returns to ScriptingConfig::extensions, after
// the extensions whose contracts its contract builds on, and with the text extension it shares paragraphs with:
//
//     auto painting = Fenrir::Painting::Install(app);
//     auto text = Fenrir::Text::ScriptingExtension();
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Input::ScriptingExtension(), Fenrir::Assets::ScriptingExtension(), text,
//      Fenrir::Painting::ScriptingExtension(text)}});
//
// The extension shares the Fenrir.Painter contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Painter)
// copies next to an executable with the contracts it builds on. Its static `Painter` facade is one native per Painter
// call. A C# system is given a painter over a window's overlay at one layer (Painter.Overlay); the extension keeps the
// draw list it records into, and when that system returns (its changes play back) submits the list to the app's
// Overlay resource and drops it. Paths are made and built through natives (Path2D) or passed as spans of points, and
// paints and stroke styles are plain structs. All of the contract but the facade's class doc is generated.
//
// A native function that does nothing (no Overlay in the app or no `builtin:painter` in its asset store, a painter
// whose system returned or whose frame is over, a path destroyed or never made, a paragraph the text extension does not
// have) returns zero and warns in debug builds once per call site: once for each place in this library's native code
// that notices it and each reason. The record is the extension's state, so it lasts as long as the Scripting given the
// extension, and so do the paths.

#include "FenrirPainter/Painting.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
