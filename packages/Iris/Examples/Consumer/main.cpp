// Starts .NET through an installed Iris package and calls a component's static Add(int, int).
// Usage: IrisConsumer component.dll
#include <Iris/Host.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc != 2)
    {
        std::cerr << "Usage: IrisConsumer component.dll\n";
        return EXIT_FAILURE;
    }
    // iris_deploy put the bridge and its runtime configuration next to this executable.
    const auto here = std::filesystem::absolute(argv[0]).parent_path();

    Iris::Host host;
    const auto initialized = host.Initialize(
        {.runtimeConfigPath = here / "Iris.Bridge.runtimeconfig.json", .bridgeAssemblyPath = here / "Iris.Bridge.dll"});
    if (!initialized.success)
    {
        std::cerr << "Initialize failed: " << initialized.diagnostic << '\n';
        return EXIT_FAILURE;
    }

    auto loaded = host.LoadComponent(argv[1]);
    if (!loaded.Succeeded())
    {
        std::cerr << "LoadComponent failed: " << loaded.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    Iris::Component component = std::move(*loaded.value);
    const auto type = component.FindType("Iris.Examples.Component.Calculations");
    if (!type.Succeeded())
    {
        std::cerr << "FindType failed: " << type.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    const auto add = type.value->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
    if (!add.Succeeded())
    {
        std::cerr << "FindMethod failed: " << add.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    const auto sum = add.value->Invoke(20, 22);
    if (!sum.Succeeded())
    {
        std::cerr << "Invoke failed: " << sum.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Iris consumer: Add(20, 22) = " << *sum.value << '\n';
    return *sum.value == 42 && component.Release().Succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}
