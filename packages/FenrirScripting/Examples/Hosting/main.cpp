#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

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

    Fenrir::Scripting::Utf8StringResult greeting;
    Fenrir::Scripting::Utf8StringResult echo;
    {
        Fenrir::Scripting::DotNetHost host;
        std::cout << std::boolalpha << "Initialized before discovery: " << host.IsInitialized() << '\n';
        const auto printCall = [](const char* label, const Fenrir::Scripting::Int32CallResult& call)
        {
            std::cout << label << ": status=" << static_cast<std::int32_t>(call.status) << ", value=";
            if (call.value.has_value())
            {
                std::cout << *call.value;
            }
            else
            {
                std::cout << "none";
            }
            std::cout << '\n';
            if (!call.diagnostic.empty())
            {
                std::cout << "Call diagnostic: " << call.diagnostic << '\n';
            }
        };
        const auto beforeInitialization = host.AddInt32(1, 2);
        printCall("Add before initialization", beforeInitialization);
        const auto greetingBeforeInitialization = host.GetGreeting();
        std::cout << "Greeting before initialization: status="
                  << static_cast<std::int32_t>(greetingBeforeInitialization.status) << '\n';
        const auto echoBeforeInitialization = host.EchoUtf8("before initialization");
        std::cout << "Echo before initialization: status="
                  << static_cast<std::int32_t>(echoBeforeInitialization.status) << '\n';

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
        std::cout << "Interop method resolved: " << result.interopMethodResolved << '\n';
        std::cout << "Greeting method resolved: " << result.greetingMethodResolved << '\n';
        std::cout << "Echo method resolved: " << result.echoMethodResolved << '\n';
        if (result.bridgeVersion.has_value())
        {
            std::cout << "Bridge version: " << *result.bridgeVersion << '\n';
        }
        std::cout << "Initialization succeeded: " << result.success << '\n';
        std::cout << "Diagnostic: " << result.diagnostic << '\n';
        std::cout << "Initialized after attempt: " << host.IsInitialized() << '\n';

        if (!result.success || !host.IsInitialized())
        {
            return EXIT_FAILURE;
        }

        const auto sum = host.AddInt32(20, 22);
        printCall("Add(20, 22)", sum);
        const auto overflow = host.AddInt32(std::numeric_limits<std::int32_t>::max(), 1);
        printCall("Add(INT32_MAX, 1)", overflow);
        const auto recovery = host.AddInt32(7, -3);
        printCall("Add after overflow", recovery);

        using Fenrir::Scripting::ManagedCallStatus;
        const bool integerChecksPassed =
            beforeInitialization.status == ManagedCallStatus::NotInitialized && !beforeInitialization.value &&
            sum.status == ManagedCallStatus::Success && sum.value == 42 &&
            overflow.status == ManagedCallStatus::Overflow && !overflow.value &&
            recovery.status == ManagedCallStatus::Success && recovery.value == 4 && host.IsInitialized();
        if (!integerChecksPassed || greetingBeforeInitialization.status != ManagedCallStatus::NotInitialized ||
            greetingBeforeInitialization.value || echoBeforeInitialization.status != ManagedCallStatus::NotInitialized ||
            echoBeforeInitialization.value)
        {
            return EXIT_FAILURE;
        }
        greeting = host.GetGreeting();

        struct EchoCase
        {
            const char* label;
            std::string input;
        };
        const EchoCase cases[] = {
            {"Echo ASCII", "Hello from native"},
            {"Echo Unicode", "\xCF\x80 \xF0\x9F\x90\xBA"},
            {"Echo empty", {}},
            {"Echo embedded null", std::string("A\0B", 3)}
        };
        for (const auto& entry : cases)
        {
            echo = host.EchoUtf8(entry.input);
            const bool matches = echo.status == ManagedCallStatus::Success && echo.value == entry.input;
            std::cout << entry.label << ": status=" << static_cast<std::int32_t>(echo.status)
                      << ", bytes match=" << matches << '\n';
            if (!matches)
            {
                std::cerr << "Echo diagnostic: " << echo.diagnostic << '\n';
                return EXIT_FAILURE;
            }
        }
        const auto invalid = host.EchoUtf8("\xC3(");
        std::cout << "Echo malformed UTF-8: status=" << static_cast<std::int32_t>(invalid.status)
                  << ", value=" << (invalid.value ? "present" : "none") << '\n';
        const auto recovered = host.EchoUtf8("after invalid input");
        const bool recoveredMatches = recovered.status == ManagedCallStatus::Success &&
                                      recovered.value == "after invalid input";
        std::cout << "Echo after invalid input: status=" << static_cast<std::int32_t>(recovered.status)
                  << ", bytes match=" << recoveredMatches << '\n';
        if (invalid.status != ManagedCallStatus::InvalidArgument || invalid.value || !recoveredMatches ||
            !host.IsInitialized())
        {
            return EXIT_FAILURE;
        }
    } // Input storage and the wrapper are destroyed; returned strings own their bytes.

    const bool echoMatches = echo.status == Fenrir::Scripting::ManagedCallStatus::Success &&
                             echo.value == std::string("A\0B", 3);
    std::cout << "Echo bytes after wrapper destruction: " << echoMatches << '\n';

    std::cout << "Greeting after wrapper destruction: status=" << static_cast<std::int32_t>(greeting.status) << '\n';
    if (greeting.status != Fenrir::Scripting::ManagedCallStatus::Success || !greeting.value)
    {
        std::cerr << "Greeting diagnostic: " << greeting.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Greeting UTF-8 bytes: " << greeting.value->size() << '\n';
    std::cout << "Greeting: " << *greeting.value << '\n';
    const auto expected = u8"Hello from .NET \u2014 \u03C0 \U0001F43A";
    const std::u8string_view expectedBytes(expected);
    const std::string_view actualBytes(*greeting.value);
    const bool matches = actualBytes == std::string_view(
        reinterpret_cast<const char*>(expectedBytes.data()), expectedBytes.size());
    std::cout << "Greeting bytes match: " << matches << '\n';
    return matches && echoMatches ? EXIT_SUCCESS : EXIT_FAILURE;
}
