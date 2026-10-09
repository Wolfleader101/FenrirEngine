#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void CheckInfo(const Fenrir::Scripting::ManagedComponent& owner)
    {
        const auto info = owner.GetInfo();
        Check(info.status == Fenrir::Scripting::ManagedCallStatus::Success && info.value &&
                  info.value->assemblyName == "Fenrir.HostingComponent" && info.value->isCollectible,
              "Component identity/collectibility incorrect");
    }
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    using namespace Fenrir::Scripting;
    static_assert(!std::is_copy_constructible_v<ManagedComponent> &&
                  std::is_nothrow_move_constructible_v<ManagedComponent>);
    try
    {
        Check(argc == 5, "Expected runtimeconfig, real bridge, component, and incomplete bridge");
        HostConfiguration configuration{argv[1], argv[2], {}};
        const auto componentPath = std::filesystem::absolute(argv[3]);
        std::unique_ptr<ManagedComponent> retained;
        ComponentInfo snapshot;
        {
            DotNetHost host;
            Check(host.LoadComponent(componentPath).status == ManagedCallStatus::NotInitialized,
                  "Unready host loaded component");
            Check(host.Initialize(configuration).success, "Real bridge initialization failed");
            auto loaded = host.LoadComponent(componentPath);
            Check(loaded.status == ManagedCallStatus::Success && loaded.value && loaded.value->IsLoaded(),
                  "Component load failed");
            CheckInfo(*loaded.value);
            snapshot = *loaded.value->GetInfo().value;
            retained = std::move(loaded.value);

            auto missingPath = componentPath;
            missingPath += ".missing";
            const auto missing = host.LoadComponent(missingPath);
            Check(missing.status == ManagedCallStatus::ManagedException && !missing.value &&
                      missing.diagnostic.find("FileNotFoundException") != std::string::npos,
                  "Missing assembly diagnostic lost");
            Check(host.LoadComponent({}).status == ManagedCallStatus::InvalidArgument, "Empty native path accepted");
            const auto invalid = host.LoadComponent(argv[1]); // Existing JSON, not a managed assembly.
            Check(invalid.status == ManagedCallStatus::ManagedException && !invalid.value &&
                      invalid.diagnostic.find("BadImageFormatException") != std::string::npos,
                  "Invalid assembly diagnostic lost");
            Check(host.AddInt32(20, 22).value == 42 &&
                      host.EchoUtf8("after component failures").value == "after component failures",
                  "Component failure damaged bridge readiness");

            // UTF-8 path crossing and component metadata are independent of filename.
            const auto unicodeDirectory =
                componentPath.parent_path() / std::filesystem::path(u8"component-\u03C0-\U0001F43A");
            std::filesystem::create_directories(unicodeDirectory);
            const auto unicodePath = unicodeDirectory / componentPath.filename();
            std::filesystem::copy_file(componentPath, unicodePath, std::filesystem::copy_options::overwrite_existing);
            auto unicode = host.LoadComponent(unicodePath);
            Check(unicode.status == ManagedCallStatus::Success && unicode.value, "Unicode component path failed");
            CheckInfo(*unicode.value);
            ManagedComponent moved(std::move(*unicode.value));
            Check(!unicode.value->IsLoaded() && unicode.value->GetInfo().status == ManagedCallStatus::InvalidHandle,
                  "Move left usable old owner");
            CheckInfo(moved);
            Check(moved.Release().status == ManagedCallStatus::Success && !moved.IsLoaded(), "Release failed");
            Check(moved.Release().status == ManagedCallStatus::Success &&
                      moved.GetInfo().status == ManagedCallStatus::InvalidHandle,
                  "Repeated release or stale owner contract failed");

            const auto incomplete = host.Initialize({argv[1], argv[4], {}});
            Check(!incomplete.success && !incomplete.componentMethodsResolved && !host.IsInitialized() &&
                      incomplete.diagnostic.find("LoadComponent") != std::string::npos,
                  "Missing component exports published readiness");
            CheckInfo(*retained); // Captured functions survive host reset/reinitialization.
            Check(host.Initialize(configuration).success, "Host did not recover after incomplete bridge");
            for (int i = 0; i < 16; ++i)
            {
                auto scoped = host.LoadComponent(componentPath);
                Check(scoped.status == ManagedCallStatus::Success && scoped.value, "Repeated scoped load failed");
                CheckInfo(*scoped.value); // Destruction performs release; no forced GC here.
            }
        }
        CheckInfo(*retained);
        Check(retained->Release().status == ManagedCallStatus::Success && !retained->IsLoaded(),
              "Release after host destruction failed");
        Check(snapshot.assemblyName == "Fenrir.HostingComponent" && snapshot.isCollectible,
              "Native snapshot lifetime ended with owner");
        std::cout << "Native component loading, failures, moves, release, host reset/destruction and snapshot "
                     "ownership passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
