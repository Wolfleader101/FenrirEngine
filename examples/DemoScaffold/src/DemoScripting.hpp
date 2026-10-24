#pragma once

// The demo scaffold's scripting, built only with FENRIR_ENABLE_SCRIPTING (FENRIR_DEMO_SCRIPTING): the extensions a demo
// app's C# scripts run with.

#include <vector>

#include "FenrirScripting/ScriptingExtension.hpp"

namespace FenrirDemo::Detail
{
    /**
     * @brief Every scripting extension of the packages the scaffold installs (Input, Window, Assets, Renderer), in the
     * order their contracts build on each other, then the scaffold's own: the Fenrir.Demo assembly, with the
     * CSharpScene marker as a read-only scene resource and DemoSceneClickEvent as a native event.
     */
    [[nodiscard]] std::vector<Fenrir::ScriptingExtension> DemoScriptingExtensions();
} // namespace FenrirDemo::Detail
