#include "DemoScripting.hpp"

#include <string_view>
#include <utility>

#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirRendererScripting/RendererScripting.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"
#include "FenrirWindowScripting/WindowScripting.hpp"

#ifdef FENRIR_DEMO_UI_SCRIPTING
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUIScripting/UIScripting.hpp"
#endif

// How the scaffold's types look to C#: each mapping is checked against its mirror in the Fenrir.Demo assembly
// (examples/DemoScaffold/Managed/Fenrir.Demo) whenever a script assembly loads, so a drift fails the load.

template <>
struct Iris::StructMapping<FenrirDemo::CSharpScene>
{
    static constexpr std::string_view typeName = "Fenrir.Demo.CSharpScene";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(FenrirDemo::CSharpScene, scene, "Scene"),
                                                      IRIS_FIELD_AS(FenrirDemo::CSharpScene, entry, "Entry"));
};

template <>
struct Iris::StructMapping<FenrirDemo::DemoSceneClickEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Demo.DemoSceneClickEvent";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(FenrirDemo::DemoSceneClickEvent, ray, "Ray"),
                           IRIS_FIELD_AS(FenrirDemo::DemoSceneClickEvent, position, "Position"));
};

namespace FenrirDemo::Detail
{
    std::vector<Fenrir::ScriptingExtension> DemoScriptingExtensions()
    {
        Fenrir::ScriptingExtension scaffold;
        scaffold.name = "FenrirDemoScaffold";
        scaffold.sharedAssemblies = {"Fenrir.Demo.dll"};
        scaffold.resources = {Fenrir::MakeReadOnlySceneResource<CSharpScene>("Fenrir.Demo.CSharpScene")};
        scaffold.events = {Fenrir::MakeEvent<DemoSceneClickEvent>("Fenrir.Demo.DemoSceneClickEvent")};
        std::vector<Fenrir::ScriptingExtension> extensions{
            Fenrir::Input::ScriptingExtension(), Fenrir::Window::ScriptingExtension(),
            Fenrir::Assets::ScriptingExtension(), Fenrir::Renderer::ScriptingExtension(),
            Fenrir::DebugDrawing::ScriptingExtension()};
#ifdef FENRIR_DEMO_UI_SCRIPTING
        // The scaffold installs FenrirUI, so its extension is the scaffold's too, with the text and painter ones it
        // shares state with.
        Fenrir::ScriptingExtension text = Fenrir::Text::ScriptingExtension();
        Fenrir::ScriptingExtension painter = Fenrir::Painting::ScriptingExtension(text);
        Fenrir::ScriptingExtension ui = Fenrir::UI::ScriptingExtension(painter);
        extensions.push_back(std::move(text));
        extensions.push_back(std::move(painter));
        extensions.push_back(std::move(ui));
#endif
        extensions.push_back(std::move(scaffold));
        return extensions;
    }
} // namespace FenrirDemo::Detail
