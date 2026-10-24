#include <memory>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "Showcase.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());

    FenrirDemo::DemoConfig config;
    config.appName = "ScriptingDemo";
    config.scenes = {ScriptingDemo::Showcase()};
    config.scriptAssembly = FENRIR_DEMO_SCRIPT_ASSEMBLY;
    // Bob and Burst, which C++ and C# share.
    config.scriptingExtensions = {ScriptingDemo::Extension()};

    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    // After Install, which installs Scripting: these C++ systems run after the C# ones and see what they wrote.
    ScriptingDemo::AddSystems(app);
    return demo.Run();
}
