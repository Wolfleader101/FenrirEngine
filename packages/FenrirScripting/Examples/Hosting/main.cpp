#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <iostream>

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    std::filesystem::path dotnetRoot;
    int firstPathArgument = 1;
    if (argc >= 3 && std::filesystem::path(argv[1]) == "--dotnet-root")
    {
        dotnetRoot = argv[2];
        firstPathArgument = 3;
    }

    const int pathArgumentCount = argc - firstPathArgument;
    if (pathArgumentCount != 2)
    {
        std::cerr << "Usage: FenrirHostingExample [--dotnet-root absolute-directory] runtimeconfig.json bridge.dll\n";
        return EXIT_FAILURE;
    }

    Fenrir::Scripting::HostConfiguration configuration;
    configuration.dotnetRoot = dotnetRoot;
    configuration.runtimeConfigPath = argv[firstPathArgument];
    configuration.bridgeAssemblyPath = argv[firstPathArgument + 1];

    Fenrir::Scripting::DotNetHost host;
    std::cout << std::boolalpha << "Initialized before discovery: " << host.IsInitialized() << '\n';

    const auto discovery = host.DiscoverHostFxr(dotnetRoot);
    std::cout << "Hostfxr discovered: " << discovery.success << '\n';
    std::cout << "Initialized after discovery: " << host.IsInitialized() << '\n';
    if (!discovery.success)
    {
        std::cerr << "Discovery diagnostic: " << discovery.diagnostic << '\n';
        return EXIT_FAILURE;
    }

    const auto utf8Path = discovery.hostFxrPath.u8string();
    std::cout << "Hostfxr path: ";
    std::cout.write(reinterpret_cast<const char*>(utf8Path.data()), static_cast<std::streamsize>(utf8Path.size()));
    std::cout << '\n';

    const auto result = host.Initialize(configuration);
    std::cout << "Hostfxr loaded: " << result.hostFxrLoaded << '\n';
    std::cout << "Required exports resolved: " << result.requiredExportsResolved << '\n';
    std::cout << "Runtime configuration initialized: " << result.runtimeConfigInitialized << '\n';
    std::cout << "Runtime delegate obtained: " << result.runtimeDelegateObtained << '\n';
    std::cout << "Host context closed: " << result.hostContextClosed << '\n';
    std::cout << "Bridge method resolved: " << result.bridgeMethodResolved << '\n';
    if (result.bridgeVersion.has_value())
    {
        std::cout << "Bridge version: " << *result.bridgeVersion << '\n';
    }
    std::cout << "Initialization succeeded: " << result.success << '\n';
    std::cout << "Diagnostic: " << result.diagnostic << '\n';
    std::cout << "Initialized after attempt: " << host.IsInitialized() << '\n';

    return result.success && host.IsInitialized() ? EXIT_SUCCESS : EXIT_FAILURE;
}
