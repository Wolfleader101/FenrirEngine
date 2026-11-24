// The outside project's executable. It links every package; it runs headless (no window, no GPU), installing physics
// and running a few frames, so it starts and exits wherever it is built.
#include <iostream>
#include <memory>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirUI/UI.hpp"

int main()
{
    // The renderer, the UI and scripting are not installed headless; taking their Install's address links them anyway.
    [[maybe_unused]] auto* volatile rendererInstall = &Fenrir::Renderer::Install;
    [[maybe_unused]] auto* volatile uiInstall = &Fenrir::UI::Install;
    [[maybe_unused]] auto* volatile scriptingInstall = &Fenrir::Scripting::Install;

    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    auto physics = Fenrir::Physics::Install(app);

    constexpr int frames = 3;
    for (int frame = 0; frame < frames && app.IsRunning(); ++frame)
    {
        app.RunFrame(1.0 / 60.0);
    }
    app.RunExit();

    std::cout << "outside-project: ran " << frames << " frames\n";
    return 0;
}
