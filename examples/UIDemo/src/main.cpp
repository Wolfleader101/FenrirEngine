#include <memory>

#include "FenrirApp/App.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "UIDemo.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, UIDemo::MakeDemoConfig());
    return demo.Run();
}
