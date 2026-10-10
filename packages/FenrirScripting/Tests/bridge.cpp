// Bridge handshake and response-handling checks. The first argument selects the bridge variant:
// "real" runs against the real bridge; "fixture" runs against the scripted fixture bridge (see
// FixtureBridge/Bootstrap.cs); "legacy", "oversized", "missing" and "notbridge" check that an
// incompatible bridge is rejected. Each variant runs in its own process because a process holds
// one bridge assembly.
#include "FenrirDotNetHost/DotNetHost.hpp"
#include "TestHarness.hpp"

#include <memory>
#include <string>

namespace
{
    using namespace Fenrir::Scripting;
    using Fenrir::Scripting::Tests::Check;

    struct Arguments
    {
        std::filesystem::path runtimeConfig;
        std::filesystem::path bridge;
        std::filesystem::path secondBridge;
    };

    std::unique_ptr<DotNetHost> StartHost(const Arguments& arguments)
    {
        auto host = std::make_unique<DotNetHost>();
        const auto initialized = host->Initialize({arguments.runtimeConfig, arguments.bridge, {}});
        Check(initialized.success, "Initialization failed: " + initialized.diagnostic);
        return host;
    }

    // The fixture's control channel; see FixtureBridge/Bootstrap.cs.
    ManagedCallResult<std::int32_t> Scenario(const ManagedComponent& fixture, std::string_view name)
    {
        return fixture.InvokeInt32("Scenario", name, 0, 0);
    }
    void Select(const ManagedComponent& fixture, std::string_view mode)
    {
        Check(fixture.InvokeInt32("Select", mode, 0, 0).Succeeded(), "Selecting fixture mode failed");
    }
    std::int32_t Counter(const ManagedComponent& fixture, std::string_view name)
    {
        const auto result = fixture.InvokeInt32("Counter", name, 0, 0);
        Check(result.value.has_value(), "Fixture counter unavailable");
        return *result.value;
    }

    ManagedComponent LoadFixture(const DotNetHost& host, const Arguments& arguments)
    {
        auto loaded = host.LoadComponent(arguments.bridge); // The fixture ignores the path.
        Check(loaded.Succeeded(), "Fixture component load failed");
        return std::move(*loaded.value);
    }

    std::string TruncatedPi()
    {
        std::string expected;
        for (int i = 0; i < 2048; ++i)
        {
            expected += "\xCF\x80";
        }
        return expected;
    }

    int RunRejected(const Arguments& arguments, const char* name, const char* expectedText,
                    std::optional<std::int32_t> expectedVersion, bool expectAssemblyLoaded)
    {
        return Tests::RunCases(
            {{name, [&] {
                  DotNetHost host;
                  const auto initialized = host.Initialize({arguments.runtimeConfig, arguments.bridge, {}});
                  Check(!initialized.success && !host.IsInitialized(), "The bridge was accepted");
                  Check(initialized.diagnostic.find(expectedText) != std::string::npos,
                        "Diagnostic does not mention '" + std::string(expectedText) + "': " + initialized.diagnostic);
                  Check(initialized.bridgeVersion == expectedVersion, "Unexpected reported bridge version");
                  Check(initialized.bridgeAssemblyLoaded == expectAssemblyLoaded,
                        "Unexpected bridge assembly progress");
                  Check(host.LoadComponent(arguments.bridge).status == ManagedCallStatus::NotInitialized,
                        "A rejected bridge published readiness");
              }}});
    }

    int RunReal(const Arguments& arguments)
    {
        return Tests::RunCases({
            {"the real bridge completes the handshake",
             [&] {
                 DotNetHost host;
                 Check(host.LoadComponent(arguments.bridge).status == ManagedCallStatus::NotInitialized,
                       "An uninitialized host loaded a component");
                 const auto initialized = host.Initialize({arguments.runtimeConfig, arguments.bridge, {}});
                 Check(initialized.success && initialized.bridgeAssemblyLoaded && initialized.bridgeVersion == 9,
                       initialized.diagnostic);
             }},
            {"a second bridge file is rejected in the same process",
             [&] {
                 auto host = StartHost(arguments);
                 const auto second = host->Initialize({arguments.runtimeConfig, arguments.secondBridge, {}});
                 Check(!second.success && !host->IsInitialized() && !second.bridgeAssemblyLoaded &&
                           second.diagnostic.find("load_assembly") != std::string::npos,
                       "A second bridge file was loaded");
                 Check(host->Initialize({arguments.runtimeConfig, arguments.bridge, {}}).success,
                       "The resident bridge did not reinitialize");
             }},
        });
    }

    int RunFixture(const Arguments& arguments)
    {
        const auto expected = TruncatedPi();
        return Tests::RunCases({
            {"failures carry bounded, owning managed details",
             [&] {
                 ManagedCallResult<std::int32_t> retained;
                 {
                     auto host = StartHost(arguments);
                     const auto fixture = LoadFixture(*host, arguments);
                     retained = Scenario(fixture, "TruncatedUnicode");
                 }
                 Check(retained.status == ManagedCallStatus::ManagedException && !retained.value &&
                           retained.diagnosticTruncated && retained.diagnostic == expected,
                       "Truncated Unicode diagnostic incorrect or did not outlive the host");
             }},
            {"invalid managed details fall back without changing the status",
             [&] {
                 auto host = StartHost(arguments);
                 const auto fixture = LoadFixture(*host, arguments);
                 const auto broken = Scenario(fixture, "BrokenFormatter");
                 Check(broken.status == ManagedCallStatus::Overflow && !broken.value && !broken.diagnostic.empty() &&
                           !broken.diagnosticTruncated,
                       "A throwing formatter replaced the original status");
                 for (const char* scenario : {"NegativeCount", "OversizedCount", "InvalidFlag", "InvalidUtf8"})
                 {
                     const auto invalid = Scenario(fixture, scenario);
                     Check(invalid.status == ManagedCallStatus::ManagedException && !invalid.value &&
                               invalid.diagnostic.find("Managed diagnostic") != std::string::npos &&
                               !invalid.diagnosticTruncated,
                           std::string("Invalid diagnostic metadata trusted: ") + scenario);
                 }
                 const auto emptyTruncated = Scenario(fixture, "EmptyTruncated");
                 Check(emptyTruncated.status == ManagedCallStatus::ManagedException &&
                           emptyTruncated.diagnosticTruncated && !emptyTruncated.diagnostic.empty(),
                       "An empty truncated diagnostic lost its fallback or flag");
                 Check(Scenario(fixture, "EmbeddedNull").diagnostic == std::string("A\0B", 3),
                       "An embedded null in the diagnostic was lost");
             }},
            {"malformed responses never publish a value",
             [&] {
                 auto host = StartHost(arguments);
                 const auto fixture = LoadFixture(*host, arguments);
                 const auto withMetadata = Scenario(fixture, "SuccessWithMetadata");
                 Check(withMetadata.status == ManagedCallStatus::InvalidResponse && !withMetadata.value,
                       "Success with diagnostic metadata accepted");
                 const auto unknown = Scenario(fixture, "UnknownStatus");
                 Check(unknown.status == ManagedCallStatus::UnexpectedStatus && !unknown.value,
                       "Unknown status accepted");
                 Check(Scenario(fixture, "InvalidHandle").status == ManagedCallStatus::InvalidHandle,
                       "The bridge's invalid-handle status was lost");
                 const auto recovered = fixture.InvokeInt32("Fixture", "Add", 20, 22);
                 Check(recovered.value == 42 && recovered.diagnostic.empty(), "The next call did not recover");
             }},
            {"a failing call is never repeated to produce its diagnostic",
             [&] {
                 auto host = StartHost(arguments);
                 const auto fixture = LoadFixture(*host, arguments);
                 const auto before = Counter(fixture, "CountedFailures");
                 const auto failed = Scenario(fixture, "CountedFailure");
                 Check(failed.status == ManagedCallStatus::ManagedException && !failed.value &&
                           Counter(fixture, "CountedFailures") == before + 1,
                       "The failing call ran more than once");
             }},
            {"string reads: failed queries never copy and failed copies never retry",
             [&] {
                 struct StringCase
                 {
                     const char* mode;
                     ManagedCallStatus status;
                     int calls;
                 };
                 const StringCase cases[] = {{"QueryTruncated", ManagedCallStatus::ManagedException, 1},
                                             {"CopyTruncated", ManagedCallStatus::ManagedException, 2},
                                             {"QueryBrokenFormatter", ManagedCallStatus::ManagedException, 1},
                                             {"CopyBrokenFormatter", ManagedCallStatus::ManagedException, 2},
                                             {"QueryNegativeCount", ManagedCallStatus::ManagedException, 1},
                                             {"CopyInvalidUtf8", ManagedCallStatus::ManagedException, 2},
                                             {"QuerySuccessWithMetadata", ManagedCallStatus::InvalidResponse, 1},
                                             {"CopySuccessWithMetadata", ManagedCallStatus::InvalidResponse, 2},
                                             {"QueryUnknownStatus", ManagedCallStatus::UnexpectedStatus, 1},
                                             {"CopyUnknownStatus", ManagedCallStatus::UnexpectedStatus, 2},
                                             {"CopyBufferTooSmall", ManagedCallStatus::BufferTooSmall, 2},
                                             {"QueryInvalidArgument", ManagedCallStatus::InvalidArgument, 1},
                                             {"QueryEmptyTruncated", ManagedCallStatus::ManagedException, 1},
                                             {"QueryNegativeLength", ManagedCallStatus::InvalidResponse, 1},
                                             {"CopyChangedLength", ManagedCallStatus::InvalidResponse, 2},
                                             {"QuerySuccessTruncated", ManagedCallStatus::InvalidResponse, 1},
                                             {"CopyOversizedCount", ManagedCallStatus::ManagedException, 2},
                                             {"CopyInvalidFlag", ManagedCallStatus::ManagedException, 2},
                                             {"CopyEmbeddedNull", ManagedCallStatus::ManagedException, 2}};
                 auto host = StartHost(arguments);
                 const auto fixture = LoadFixture(*host, arguments);
                 for (const auto& entry : cases)
                 {
                     Select(fixture, entry.mode);
                     const auto failed = fixture.GetInfo();
                     const std::string context = std::string(" (") + entry.mode + ")";
                     Check(failed.status == entry.status && !failed.value && !failed.diagnostic.empty(),
                           "Status, diagnostic or partial output incorrect" + context);
                     Check(Counter(fixture, "NameCalls") == entry.calls,
                           "A failed read was retried, or a failed query copied" + context);
                     const std::string_view mode = entry.mode;
                     if (mode == "QueryTruncated" || mode == "CopyTruncated")
                         Check(failed.diagnostic == expected && failed.diagnosticTruncated,
                               "Truncation lost" + context);
                     if (mode == "QueryNegativeCount" || mode == "CopyInvalidUtf8" || mode == "CopyOversizedCount" ||
                         mode == "CopyInvalidFlag")
                         Check(failed.diagnostic.find("Managed diagnostic") != std::string::npos &&
                                   !failed.diagnosticTruncated,
                               "Invalid diagnostic trusted" + context);
                     if (mode == "CopyBufferTooSmall")
                         Check(failed.diagnostic == "Copy buffer too small", "Details lost" + context);
                     if (mode == "QueryEmptyTruncated")
                         Check(failed.diagnosticTruncated, "Truncation flag lost" + context);
                     if (mode == "CopyEmbeddedNull")
                         Check(failed.diagnostic == std::string("A\0B", 3), "Embedded null lost" + context);
                 }
                 Select(fixture, "None");
                 const auto recovered = fixture.GetInfo();
                 Check(recovered.value && recovered.value->assemblyName == std::string("A\0B", 3) &&
                           recovered.diagnostic.empty() && !recovered.diagnosticTruncated &&
                           Counter(fixture, "NameCalls") == 2,
                       "String recovery kept diagnostics or changed the call count");
             }},
            {"unload observation rejects malformed responses",
             [&] {
                 auto host = StartHost(arguments);
                 auto fixture = LoadFixture(*host, arguments);
                 Check(fixture.GetUnloadStatus().value == ComponentUnloadState::NotRequested,
                       "A loaded owner did not report NotRequested");
                 const std::pair<const char*, ManagedCallStatus> cases[] = {
                     {"UnloadOutOfRange", ManagedCallStatus::InvalidResponse},
                     {"UnloadNegative", ManagedCallStatus::InvalidResponse},
                     {"UnloadCollected", ManagedCallStatus::InvalidResponse}, // Impossible while loaded.
                     {"UnloadWithMetadata", ManagedCallStatus::InvalidResponse},
                     {"UnloadUnknownStatus", ManagedCallStatus::UnexpectedStatus},
                     {"UnloadInvalidHandle", ManagedCallStatus::InvalidHandle}};
                 for (const auto& [mode, status] : cases)
                 {
                     Select(fixture, mode);
                     const auto query = fixture.GetUnloadStatus();
                     Check(query.status == status && !query.value && !query.diagnostic.empty(),
                           std::string("Malformed unload response accepted: ") + mode);
                 }
                 Select(fixture, "None");

                 // A released owner cannot invoke, so a second registration selects modes.
                 const auto control = LoadFixture(*host, arguments);
                 Check(fixture.Release().Succeeded(), "Release failed");
                 Check(fixture.GetUnloadStatus().value == ComponentUnloadState::Pending,
                       "A released owner did not report Pending");
                 Select(control, "UnloadCollected");
                 Check(fixture.GetUnloadStatus().value == ComponentUnloadState::Collected,
                       "A collected observation was rejected");
                 Select(control, "UnloadNotRequested");
                 Check(fixture.GetUnloadStatus().status == ManagedCallStatus::InvalidResponse,
                       "A released owner accepted NotRequested");
                 Select(control, "None");
             }},
            {"owner destruction releases and forgets exactly once",
             [&] {
                 auto host = StartHost(arguments);
                 const auto control = LoadFixture(*host, arguments);
                 const auto releases = Counter(control, "Releases");
                 const auto forgets = Counter(control, "Forgets");
                 {
                     auto released = LoadFixture(*host, arguments);
                     Check(released.Release().Succeeded(), "Release failed");
                 }
                 Check(Counter(control, "Releases") == releases + 1 && Counter(control, "Forgets") == forgets + 1,
                       "A released owner's destruction repeated release or kept its observation");
                 {
                     const auto live = LoadFixture(*host, arguments);
                 }
                 Check(Counter(control, "Releases") == releases + 2 && Counter(control, "Forgets") == forgets + 2,
                       "A live owner's destruction skipped release or observation cleanup");
                 Check(host->IsInitialized(), "Call failures reset readiness");
             }},
        });
    }
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc != 5)
    {
        std::cerr << "Usage: FenrirBridgeChecks mode runtimeconfig.json bridge.dll second-bridge.dll\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path mode = argv[1];
    const Arguments arguments{argv[2], argv[3], argv[4]};
    if (mode == "real")
        return RunReal(arguments);
    if (mode == "fixture")
        return RunFixture(arguments);
    if (mode == "legacy")
        return RunRejected(arguments, "an older bridge version is rejected", "expected 9, received 8", 8, true);
    if (mode == "oversized")
        return RunRejected(arguments, "a bridge table of another size is rejected", "table size", 9, true);
    if (mode == "missing")
        return RunRejected(arguments, "a bridge table with a null entry is rejected", "GetComponentUnloadState", 9,
                           true);
    if (mode == "notbridge")
        return RunRejected(arguments, "an assembly without the bridge entry point is rejected", "GetBridgeFunctions",
                           std::nullopt, true);
    std::cerr << "Unknown mode.\n";
    return EXIT_FAILURE;
}
