#pragma once

// FenrirText's scripting extension (ADR-0029, ADR-0061), in its own library so FenrirText never links FenrirScripting.
// Built only with FENRIR_ENABLE_SCRIPTING and FENRIR_ENABLE_TEXT on. Add the app's FontLibrary as an app resource over
// its asset store, then add what Text::ScriptingExtension() returns to ScriptingConfig::extensions:
//
//     auto assets = Fenrir::Assets::Install(app, {});
//     app.AddResource<Fenrir::FontLibrary>(assets.Store());
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Text::ScriptingExtension()}});
//
// The extension shares the Fenrir.Text contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Text)
// copies next to an executable. C# registers font faces by path and family (Fonts.AddFace) into the app's FontLibrary,
// the one C++ lays its own text out in, and makes paragraphs (Paragraph.Create) that the extension keeps until a
// script destroys them: it sets their text and styles, lays them out to a width and reads their size, lines and hit
// positions. FenrirPainter's extension draws them (Painter.DrawText).
//
// A native function that does nothing (no FontLibrary or AssetStore in the app, a paragraph destroyed or never made)
// returns zero and warns in debug builds once per call site: once for each place in this library's native code that
// notices it and each reason, however many C# call sites or scripts reach it. The record is the extension's state, so
// it lasts as long as the Scripting given the extension, and so do the paragraphs.

#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir::Text
{
    /** @brief The text extension, with no paragraphs yet. */
    [[nodiscard]] Fenrir::ScriptingExtension ScriptingExtension();
} // namespace Fenrir::Text
