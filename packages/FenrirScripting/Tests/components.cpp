#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
    constexpr std::string_view CalculationType = "Fenrir.HostingComponent.Calculations";
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
    void CheckCalculation(const Fenrir::Scripting::ManagedComponent& owner)
    {
        const auto result = owner.InvokeInt32(CalculationType, "Add", 20, 22);
        Check(result.status == Fenrir::Scripting::ManagedCallStatus::Success && result.value == 42 &&
                  result.diagnostic.empty() && !result.diagnosticTruncated,
              "Component calculation failed");
    }
    void CheckUnload(const Fenrir::Scripting::ManagedComponent& owner, bool released)
    {
        using namespace Fenrir::Scripting;
        const auto result = owner.GetUnloadStatus();
        Check(result.status == ManagedCallStatus::Success && result.value && result.diagnostic.empty() &&
                  !result.diagnosticTruncated &&
                  (released ? (*result.value == ComponentUnloadState::Pending ||
                               *result.value == ComponentUnloadState::Collected)
                            : *result.value == ComponentUnloadState::NotRequested),
              "Unload observation did not match the owner lifecycle");
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
        Int32CallResult retainedFailure;
        {
            DotNetHost host;
            Check(host.LoadComponent(componentPath).status == ManagedCallStatus::NotInitialized,
                  "Unready host loaded component");
            Check(host.Initialize(configuration).success, "Real bridge initialization failed");
            auto loaded = host.LoadComponent(componentPath);
            Check(loaded.status == ManagedCallStatus::Success && loaded.value && loaded.value->IsLoaded(),
                  "Component load failed");
            CheckInfo(*loaded.value);
            CheckUnload(*loaded.value, false);
            snapshot = *loaded.value->GetInfo().value;
            retained = std::move(loaded.value);
            CheckCalculation(*retained);
            for (const auto method : {"Missing", "WrongReturn", "WrongParameters", "Generic", "UnmanagedOnly"})
            {
                const auto failed = retained->InvokeInt32(CalculationType, method, 20, 22);
                Check(failed.status == ManagedCallStatus::ManagedException && !failed.value &&
                          failed.diagnostic.find("MissingMethodException") != std::string::npos,
                      "Method lookup/signature failure escaped or published output");
            }
            retainedFailure = retained->InvokeInt32(CalculationType, "Throw", 20, 22);
            Check(retainedFailure.status == ManagedCallStatus::ManagedException && !retainedFailure.value &&
                      retainedFailure.diagnostic.find("InvalidOperationException") != std::string::npos &&
                      retainedFailure.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos,
                  "Component exception details or Unicode lost");
            const auto argumentThrow = retained->InvokeInt32(CalculationType, "ThrowArgument", 20, 22);
            Check(argumentThrow.status == ManagedCallStatus::ManagedException && !argumentThrow.value,
                  "Component ArgumentException was confused with invalid ABI arguments");
            const auto overflow =
                retained->InvokeInt32(CalculationType, "Add", std::numeric_limits<std::int32_t>::max(), 1);
            Check(overflow.status == ManagedCallStatus::Overflow && !overflow.value &&
                      overflow.diagnostic.find("OverflowException") != std::string::npos,
                  "Component overflow contract failed");
            const auto bounded = retained->InvokeInt32(CalculationType, "LongError", 0, 0);
            Check(bounded.status == ManagedCallStatus::ManagedException && !bounded.value &&
                      bounded.diagnosticTruncated,
                  "Component diagnostic was not bounded");
            const auto malformedType = retained->InvokeInt32("\xC3(", "Add", 20, 22);
            const auto malformedMethod = retained->InvokeInt32(CalculationType, "\xFF", 20, 22);
            Check(malformedType.status == ManagedCallStatus::InvalidArgument && !malformedType.value &&
                      malformedType.diagnostic.find("DecoderFallbackException") != std::string::npos &&
                      malformedMethod.status == ManagedCallStatus::InvalidArgument && !malformedMethod.value,
                  "Malformed UTF-8 names accepted");
            Check(retained->InvokeInt32({}, "Add", 20, 22).status == ManagedCallStatus::InvalidArgument &&
                      retained->InvokeInt32(CalculationType, {}, 20, 22).status == ManagedCallStatus::InvalidArgument &&
                      retained->InvokeInt32(CalculationType, std::string("Add\0suffix", 10), 20, 22).status ==
                          ManagedCallStatus::InvalidArgument,
                  "Empty/embedded-null native names accepted");
            CheckCalculation(*retained);

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
            // This copied DLL lacks its dependency. Loading is lazy; invocation must
            // contain that failure while preserving calls which need no dependency.
            const auto noDependency = unicode.value->InvokeInt32(CalculationType, "Add", 20, 22);
            Check(noDependency.status == ManagedCallStatus::ManagedException && !noDependency.value &&
                      noDependency.diagnostic.find("FileNotFoundException") != std::string::npos,
                  "Missing invocation dependency escaped or omitted diagnostics");
            ManagedComponent moved(std::move(*unicode.value));
            Check(!unicode.value->IsLoaded() && unicode.value->GetInfo().status == ManagedCallStatus::InvalidHandle &&
                      unicode.value->GetUnloadStatus().status == ManagedCallStatus::InvalidHandle &&
                      unicode.value->InvokeInt32(CalculationType, "LocalAdd", 20, 22).status ==
                          ManagedCallStatus::InvalidHandle,
                  "Move left usable old owner");
            CheckInfo(moved);
            Check(moved.InvokeInt32(CalculationType, "LocalAdd", 20, 22).value == 42, "Moved owner invocation failed");
            Check(moved.Release().status == ManagedCallStatus::Success && !moved.IsLoaded(), "Release failed");
            CheckUnload(moved, true);
            Check(moved.Release().status == ManagedCallStatus::Success &&
                      moved.GetInfo().status == ManagedCallStatus::InvalidHandle &&
                      moved.InvokeInt32(CalculationType, "Add", 20, 22).status == ManagedCallStatus::InvalidHandle,
                  "Repeated release or stale owner contract failed");
            // A released owner still owns its weak observation. Both forms of move
            // transfer it; move-assignment also cleans up the replaced registration.
            ManagedComponent observation(std::move(moved));
            CheckUnload(observation, true);
            Check(moved.GetUnloadStatus().status == ManagedCallStatus::InvalidHandle,
                  "Move retained the old observation");
            auto replacement = host.LoadComponent(componentPath);
            Check(replacement.status == ManagedCallStatus::Success && replacement.value,
                  "Move-assignment fixture load failed");
            *replacement.value = std::move(observation);
            CheckUnload(*replacement.value, true);
            Check(observation.GetUnloadStatus().status == ManagedCallStatus::InvalidHandle,
                  "Move-assignment retained the old observation");

            const auto incomplete = host.Initialize({argv[1], argv[4], {}});
            Check(!incomplete.success && !incomplete.componentMethodsResolved && !host.IsInitialized() &&
                      incomplete.echoMethodResolved &&
                      incomplete.diagnostic.find("GetComponentUnloadState") != std::string::npos,
                  "Missing component exports published readiness");
            CheckInfo(*retained); // Captured functions survive host reset/reinitialization.
            CheckUnload(*retained, false);
            CheckCalculation(*retained);
            Check(host.Initialize(configuration).success, "Host did not recover after incomplete bridge");
            for (int i = 0; i < 16; ++i)
            {
                auto scoped = host.LoadComponent(componentPath);
                Check(scoped.status == ManagedCallStatus::Success && scoped.value, "Repeated scoped load failed");
                CheckInfo(*scoped.value); // Destruction performs release; no forced GC here.
                CheckCalculation(*scoped.value);
            }
        }
        CheckInfo(*retained);
        CheckCalculation(*retained);
        CheckUnload(*retained, false);
        Check(retained->Release().status == ManagedCallStatus::Success && !retained->IsLoaded(),
              "Release after host destruction failed");
        CheckUnload(*retained, true);
        const auto stale = retained->InvokeInt32(CalculationType, "Add", 20, 22);
        Check(stale.status == ManagedCallStatus::InvalidHandle && !stale.value &&
                  retainedFailure.diagnostic.find("InvalidOperationException") != std::string::npos,
              "Released owner remained callable or native diagnostic ownership ended");
        Check(snapshot.assemblyName == "Fenrir.HostingComponent" && snapshot.isCollectible,
              "Native snapshot lifetime ended with owner");
        std::cout << "Native component loading, invocation, failures, moves, release/unload observation, host "
                     "reset/destruction and snapshot "
                     "ownership passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
