#pragma once

// FenrirUI's scripting extension (ADR-0029, ADR-0039, ADR-0061), in its own library so FenrirUI never links
// FenrirScripting. Built only with FENRIR_ENABLE_SCRIPTING and the UI on. Install the UI (UI::Install) before
// Scripting, and add what UI::ScriptingExtension(painter) returns to ScriptingConfig::extensions after the extensions
// whose contracts its contract builds on, with the painter extension it lends painters through:
//
//     auto ui = Fenrir::UI::Install(app);
//     auto text = Fenrir::Text::ScriptingExtension();
//     auto painter = Fenrir::Painting::ScriptingExtension(text);
//     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
//     {Fenrir::Input::ScriptingExtension(), Fenrir::Assets::ScriptingExtension(), text, painter,
//      Fenrir::UI::ScriptingExtension(painter)}});
//
// The extension shares the Fenrir.UI contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS UI) copies
// next to an executable with the contracts it builds on. C# names UI roots by UIRoot (the mirror of UIRootId,
// the root's id) and their elements by ElementId, through natives on UIRoot for every UIRoot call a script needs:
// making, removing and moving elements, names, classes, inline style, text, control values, rectangles, queries,
// focus, and loading documents and stylesheets by path. UIEvent is an engine event, read through EventReader<UIEvent>.
// The extension hosts the stage Fenrir.UI.Paint, UIRoots' paint stage: a C# system there gets a painter for an
// element (UIRoot.PaintInto), lent through the painter extension and taken back as the stage ends, and paints into the
// element through the Painter facade. All of the contract is generated.
//
// A native that does nothing (no UI installed, a root destroyed or never made, a stale element) returns false, zero or
// an id that names nothing, and warns in debug builds once per call site.

#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirUI/UI.hpp"
