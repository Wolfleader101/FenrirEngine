#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "UIDemo.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    FenrirDemo::DemoConfig config = UIDemo::MakeDemoConfig();
#ifdef FENRIR_DEMO_SCRIPT_ASSEMBLY
    // The C# variant (README, "The C# variant"): the speedometer on the HUD, painted in the Fenrir.UI.Paint stage, and
    // the menu's clicks read through EventReader<UIEvent>. The scaffold installs the UI before Scripting, and runs the
    // script with the text, painter and UI extensions, so the script's paint stage system finds the stage when it
    // loads.
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
#endif
    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
