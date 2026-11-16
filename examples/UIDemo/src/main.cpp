#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "UIDemo.hpp"

#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUIScripting/UIScripting.hpp"
#endif

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    FenrirDemo::DemoConfig config = UIDemo::MakeDemoConfig();
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    // The C# variant (README, "The C# variant"): the speedometer on the HUD, painted in the Fenrir.UI.Paint stage, and
    // the menu's clicks read through EventReader<UIEvent>. The UI is installed with the app's packages, before
    // Scripting, so the script's paint stage system finds the stage when it loads.
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    Fenrir::ScriptingExtension text = Fenrir::Text::ScriptingExtension();
    Fenrir::ScriptingExtension painter = Fenrir::Painting::ScriptingExtension(text);
    Fenrir::ScriptingExtension ui = Fenrir::UI::ScriptingExtension(painter);
    config.scriptingExtensions = {std::move(text), std::move(painter), std::move(ui)};
#endif
    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
