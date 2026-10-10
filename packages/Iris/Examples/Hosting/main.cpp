#include "Iris/Host.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    const char* UnloadStateName(Iris::ComponentUnloadState state)
    {
        switch (state)
        {
        case Iris::ComponentUnloadState::NotRequested:
            return "NotRequested";
        case Iris::ComponentUnloadState::Pending:
            return "Pending";
        case Iris::ComponentUnloadState::Collected:
            return "Collected";
        }
        return "Unknown";
    }
} // namespace

// Starts .NET, loads a collectible component, calls into it, then releases it.
#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    using namespace Iris;

    HostConfiguration configuration;
    int firstPath = 1;
    if (argc >= 3 && std::filesystem::path(argv[1]) == "--dotnet-root")
    {
        configuration.dotnetRoot = argv[2];
        firstPath = 3;
    }
    // Without paths, use the bridge and component the build deployed next to the executable.
    const auto here = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path componentPath = here / "component" / "Iris.Examples.Component.dll";
    configuration.runtimeConfigPath = here / "Iris.Bridge.runtimeconfig.json";
    configuration.bridgeAssemblyPath = here / "Iris.Bridge.dll";
    if (argc - firstPath == 3)
    {
        configuration.runtimeConfigPath = argv[firstPath];
        configuration.bridgeAssemblyPath = argv[firstPath + 1];
        componentPath = argv[firstPath + 2];
    }
    else if (argc != firstPath)
    {
        std::cerr << "Usage: IrisHostingExample [--dotnet-root absolute-directory] "
                     "[runtimeconfig.json bridge.dll component.dll]\n";
        return EXIT_FAILURE;
    }

    std::optional<Component> component;
    {
        Host host;
        const auto initialized = host.Initialize(configuration);
        std::cout << "Initialize: " << (initialized.success ? "ok" : "failed") << '\n'
                  << initialized.diagnostic << '\n';
        if (!initialized.success)
        {
            return EXIT_FAILURE;
        }

        auto loaded = host.LoadComponent(componentPath);
        if (loaded.status != CallStatus::Success)
        {
            std::cerr << "LoadComponent failed: " << loaded.diagnostic << '\n';
            return EXIT_FAILURE;
        }
        component = std::move(loaded.value);
        // The host is destroyed at the end of this scope; the component owner keeps working.
    }

    const auto info = component->GetInfo();
    std::cout << "Loaded " << (info.value ? info.value->assemblyName : info.diagnostic) << '\n';

    constexpr std::string_view calculations = "Iris.Examples.Component.Calculations";
    const auto sum = component->InvokeInt32(calculations, "Add", 20, 22);
    std::cout << "Calculations.Add(20, 22) = " << (sum.value ? std::to_string(*sum.value) : sum.diagnostic) << '\n';

    // Failures return a status and the managed exception's details; the next call still works.
    const auto thrown = component->InvokeInt32(calculations, "Throw", 0, 0);
    std::cout << "Calculations.Throw: status " << static_cast<std::int32_t>(thrown.status) << '\n'
              << thrown.diagnostic.substr(0, thrown.diagnostic.find('\n')) << '\n';

    const auto released = component->Release();
    const auto unload = component->GetUnloadStatus();
    std::cout << "Release: status " << static_cast<std::int32_t>(released.status) << ", unload "
              << (unload.value ? UnloadStateName(*unload.value) : "unknown") << '\n';
    const auto stale = component->InvokeInt32(calculations, "Add", 1, 2);
    std::cout << "Call after release: status " << static_cast<std::int32_t>(stale.status) << '\n';

    const bool succeeded = sum.value == 42 && thrown.status == CallStatus::ManagedException &&
                           released.status == CallStatus::Success && stale.status == CallStatus::InvalidHandle;
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
