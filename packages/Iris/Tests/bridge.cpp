// Bridge handshake and response-handling checks. The test cases are hidden: a tag selects the bridge variant to run,
// one per process, because a process holds one bridge assembly. [real] runs against the real bridge; [fixture] against
// the scripted fixture bridge (see FixtureBridge/Bootstrap.cs); [legacy], [oversized], [missing] and [notbridge] check
// that an incompatible bridge is rejected. CTest names the files in the environment (IRIS_TEST_RUNTIMECONFIG,
// IRIS_TEST_BRIDGE, IRIS_TEST_SECOND_BRIDGE) and sets IRIS_TEST_BRIDGE_MODE for the fixture bridge.
#include "Iris/Host.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace
{
    using namespace Iris;

    struct Arguments
    {
        std::filesystem::path runtimeConfig;
        std::filesystem::path bridge;
        std::filesystem::path secondBridge;
    };

    std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the bridge checks need the files it built.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    const Arguments checkArguments{FromEnvironment("IRIS_TEST_RUNTIMECONFIG"), FromEnvironment("IRIS_TEST_BRIDGE"),
                                   FromEnvironment("IRIS_TEST_SECOND_BRIDGE")};

    std::unique_ptr<Host> StartHost(const Arguments& arguments)
    {
        auto host = std::make_unique<Host>();
        const auto initialized = host->Initialize({arguments.runtimeConfig, arguments.bridge, {}});
        {
            INFO("Initialization failed: " + initialized.diagnostic);
            REQUIRE(initialized.success);
        }
        return host;
    }

    // The fixture's control channel is a method name; see FixtureBridge/Bootstrap.cs.
    CallResult<std::int32_t> Channel(const Component& fixture, const std::string& name, std::int32_t left = 0,
                                     std::int32_t right = 0)
    {
        const auto type = fixture.FindType("Fixture");
        {
            INFO("Fixture type lookup failed");
            REQUIRE(type.Succeeded());
        }
        const auto method = type.value->FindMethod<std::int32_t(std::int32_t, std::int32_t)>(name);
        {
            INFO("Fixture method lookup failed: " + name);
            REQUIRE(method.Succeeded());
        }
        return method.value->Invoke(left, right);
    }
    CallResult<std::int32_t> Scenario(const Component& fixture, std::string_view name)
    {
        return Channel(fixture, "Scenario:" + std::string(name));
    }
    void Select(const Component& fixture, std::string_view mode)
    {
        {
            INFO("Selecting fixture mode failed");
            REQUIRE(Channel(fixture, "Select:" + std::string(mode)).Succeeded());
        }
    }
    std::int32_t Counter(const Component& fixture, std::string_view name)
    {
        const auto result = Channel(fixture, "Counter:" + std::string(name));
        {
            INFO("Fixture counter unavailable");
            REQUIRE(result.value.has_value());
        }
        return *result.value;
    }

    Component LoadFixture(const Host& host, const Arguments& arguments)
    {
        auto loaded = host.LoadComponent(arguments.bridge); // The fixture ignores the path.
        {
            INFO("Fixture component load failed");
            REQUIRE(loaded.Succeeded());
        }
        // The fixture's mode is process-wide; reset it so an earlier failing case cannot leak into this one.
        Select(*loaded.value, "None");
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

    // A bridge that must be refused: the diagnostic names `expectedText`, and nothing is left ready.
    void ExpectRejected(const Arguments& arguments, const char* expectedText,
                        std::optional<std::int32_t> expectedVersion, bool expectAssemblyLoaded)
    {
        Host host;
        const auto initialized = host.Initialize({arguments.runtimeConfig, arguments.bridge, {}});
        {
            INFO("The bridge was accepted");
            REQUIRE((!initialized.success && !host.IsInitialized()));
        }
        {
            INFO("Diagnostic does not mention '" + std::string(expectedText) + "': " + initialized.diagnostic);
            REQUIRE(initialized.diagnostic.find(expectedText) != std::string::npos);
        }
        {
            INFO("Unexpected reported bridge version");
            REQUIRE(initialized.bridgeVersion == expectedVersion);
        }
        {
            INFO("Unexpected bridge assembly progress");
            REQUIRE(initialized.bridgeAssemblyLoaded == expectAssemblyLoaded);
        }
        {
            INFO("A rejected bridge published readiness");
            REQUIRE(host.LoadComponent(arguments.bridge).status == CallStatus::NotInitialized);
        }
    }

    const std::string truncatedPi = TruncatedPi();

    TEST_CASE("the real bridge completes the handshake", "[.real]")
    {
        Host host;
        {
            INFO("An uninitialized host loaded a component");
            REQUIRE(host.LoadComponent(checkArguments.bridge).status == CallStatus::NotInitialized);
        }
        const auto initialized = host.Initialize({checkArguments.runtimeConfig, checkArguments.bridge, {}});
        {
            INFO(initialized.diagnostic);
            REQUIRE((initialized.success && initialized.bridgeAssemblyLoaded && initialized.bridgeVersion == 22));
        }
    }

    TEST_CASE("a second bridge file is rejected in the same process", "[.real]")
    {
        auto host = StartHost(checkArguments);
        const auto second = host->Initialize({checkArguments.runtimeConfig, checkArguments.secondBridge, {}});
        {
            INFO("A second bridge file was loaded");
            REQUIRE((!second.success && !host->IsInitialized() && !second.bridgeAssemblyLoaded &&
                     second.diagnostic.find("load_assembly") != std::string::npos));
        }
        {
            INFO("The resident bridge did not reinitialize");
            REQUIRE(host->Initialize({checkArguments.runtimeConfig, checkArguments.bridge, {}}).success);
        }
    }

    TEST_CASE("failures carry bounded, owning managed details", "[.fixture]")
    {
        CallResult<std::int32_t> retained;
        {
            auto host = StartHost(checkArguments);
            const auto fixture = LoadFixture(*host, checkArguments);
            retained = Scenario(fixture, "TruncatedUnicode");
        }
        {
            INFO("Truncated Unicode diagnostic incorrect or did not outlive the host");
            REQUIRE((retained.status == CallStatus::ManagedException && !retained.value &&
                     retained.diagnosticTruncated && retained.diagnostic == truncatedPi));
        }
    }

    TEST_CASE("invalid managed details fall back without changing the status", "[.fixture]")
    {
        auto host = StartHost(checkArguments);
        const auto fixture = LoadFixture(*host, checkArguments);
        const auto broken = Scenario(fixture, "BrokenFormatter");
        {
            INFO("A throwing formatter replaced the original status");
            REQUIRE((broken.status == CallStatus::InvalidArgument && !broken.value && !broken.diagnostic.empty() &&
                     !broken.diagnosticTruncated));
        }
        for (const char* scenario : {"NegativeCount", "OversizedCount", "InvalidFlag", "InvalidUtf8"})
        {
            const auto invalid = Scenario(fixture, scenario);
            {
                INFO(std::string("Invalid diagnostic metadata trusted: ") + scenario);
                REQUIRE((invalid.status == CallStatus::ManagedException && !invalid.value &&
                         invalid.diagnostic.find("Managed diagnostic") != std::string::npos &&
                         !invalid.diagnosticTruncated));
            }
        }
        const auto emptyTruncated = Scenario(fixture, "EmptyTruncated");
        {
            INFO("An empty truncated diagnostic lost its fallback or flag");
            REQUIRE((emptyTruncated.status == CallStatus::ManagedException && emptyTruncated.diagnosticTruncated &&
                     !emptyTruncated.diagnostic.empty()));
        }
        {
            INFO("An embedded null in the diagnostic was lost");
            REQUIRE(Scenario(fixture, "EmbeddedNull").diagnostic == std::string("A\0B", 3));
        }
    }

    TEST_CASE("malformed responses never publish a value", "[.fixture]")
    {
        auto host = StartHost(checkArguments);
        const auto fixture = LoadFixture(*host, checkArguments);
        const auto withMetadata = Scenario(fixture, "SuccessWithMetadata");
        {
            INFO("Success with diagnostic metadata accepted");
            REQUIRE((withMetadata.status == CallStatus::InvalidResponse && !withMetadata.value));
        }
        const auto unknown = Scenario(fixture, "UnknownStatus");
        {
            INFO("Unknown status accepted");
            REQUIRE((unknown.status == CallStatus::UnexpectedStatus && !unknown.value));
        }
        {
            INFO("The bridge's invalid-handle status was lost");
            REQUIRE(Scenario(fixture, "InvalidHandle").status == CallStatus::InvalidHandle);
        }
        const auto recovered = Channel(fixture, "Add", 20, 22);
        {
            INFO("The next call did not recover");
            REQUIRE((recovered.value == 42 && recovered.diagnostic.empty()));
        }
    }

    TEST_CASE("a failing call is never repeated to produce its diagnostic", "[.fixture]")
    {
        auto host = StartHost(checkArguments);
        const auto fixture = LoadFixture(*host, checkArguments);
        const auto before = Counter(fixture, "CountedFailures");
        const auto failed = Scenario(fixture, "CountedFailure");
        {
            INFO("The failing call ran more than once");
            REQUIRE((failed.status == CallStatus::ManagedException && !failed.value &&
                     Counter(fixture, "CountedFailures") == before + 1));
        }
    }

    TEST_CASE("string reads: failed queries never copy and failed copies never retry", "[.fixture]")
    {
        struct StringCase
        {
            const char* mode;
            CallStatus status;
            int calls;
        };
        const StringCase cases[] = {{"QueryTruncated", CallStatus::ManagedException, 1},
                                    {"CopyTruncated", CallStatus::ManagedException, 2},
                                    {"QueryBrokenFormatter", CallStatus::ManagedException, 1},
                                    {"CopyBrokenFormatter", CallStatus::ManagedException, 2},
                                    {"QueryNegativeCount", CallStatus::ManagedException, 1},
                                    {"CopyInvalidUtf8", CallStatus::ManagedException, 2},
                                    {"QuerySuccessWithMetadata", CallStatus::InvalidResponse, 1},
                                    {"CopySuccessWithMetadata", CallStatus::InvalidResponse, 2},
                                    {"QueryUnknownStatus", CallStatus::UnexpectedStatus, 1},
                                    {"CopyUnknownStatus", CallStatus::UnexpectedStatus, 2},
                                    {"CopyBufferTooSmall", CallStatus::InvalidResponse, 2},
                                    {"QueryInvalidArgument", CallStatus::InvalidArgument, 1},
                                    {"QueryEmptyTruncated", CallStatus::ManagedException, 1},
                                    {"QueryNegativeLength", CallStatus::InvalidResponse, 1},
                                    {"CopyChangedLength", CallStatus::InvalidResponse, 2},
                                    {"QuerySuccessTruncated", CallStatus::InvalidResponse, 1},
                                    {"CopyOversizedCount", CallStatus::ManagedException, 2},
                                    {"CopyInvalidFlag", CallStatus::ManagedException, 2},
                                    {"CopyEmbeddedNull", CallStatus::ManagedException, 2}};
        auto host = StartHost(checkArguments);
        const auto fixture = LoadFixture(*host, checkArguments);
        for (const auto& entry : cases)
        {
            Select(fixture, entry.mode);
            const auto failed = fixture.GetInfo();
            const std::string context = std::string(" (") + entry.mode + ")";
            {
                INFO("Status, diagnostic or partial output incorrect" + context);
                REQUIRE((failed.status == entry.status && !failed.value && !failed.diagnostic.empty()));
            }
            {
                INFO("A failed read was retried, or a failed query copied" + context);
                REQUIRE(Counter(fixture, "NameCalls") == entry.calls);
            }
            const std::string_view mode = entry.mode;
            if (mode == "QueryTruncated" || mode == "CopyTruncated")
            {
                INFO("Truncation lost" + context);
                REQUIRE((failed.diagnostic == truncatedPi && failed.diagnosticTruncated));
            }
            if (mode == "QueryNegativeCount" || mode == "CopyInvalidUtf8" || mode == "CopyOversizedCount" ||
                mode == "CopyInvalidFlag")
            {
                INFO("Invalid diagnostic trusted" + context);
                REQUIRE(
                    (failed.diagnostic.find("Managed diagnostic") != std::string::npos && !failed.diagnosticTruncated));
            }
            if (mode == "CopyBufferTooSmall")
            {
                INFO("Details lost" + context);
                REQUIRE(failed.diagnostic == "Copy buffer too small");
            }
            if (mode == "QueryEmptyTruncated")
            {
                INFO("Truncation flag lost" + context);
                REQUIRE(failed.diagnosticTruncated);
            }
            if (mode == "CopyEmbeddedNull")
            {
                INFO("Embedded null lost" + context);
                REQUIRE(failed.diagnostic == std::string("A\0B", 3));
            }
        }
        Select(fixture, "None");
        const auto recovered = fixture.GetInfo();
        {
            INFO("String recovery kept diagnostics or changed the call count");
            REQUIRE((recovered.value && recovered.value->assemblyName == std::string("A\0B", 3) &&
                     recovered.diagnostic.empty() && !recovered.diagnosticTruncated &&
                     Counter(fixture, "NameCalls") == 2));
        }
    }

    TEST_CASE("unload observation rejects malformed responses", "[.fixture]")
    {
        auto host = StartHost(checkArguments);
        auto fixture = LoadFixture(*host, checkArguments);
        {
            INFO("A loaded owner did not report NotRequested");
            REQUIRE(fixture.GetUnloadState().value == ComponentUnloadState::NotRequested);
        }
        const std::pair<const char*, CallStatus> cases[] = {
            {"UnloadOutOfRange", CallStatus::InvalidResponse},   {"UnloadNegative", CallStatus::InvalidResponse},
            {"UnloadCollected", CallStatus::InvalidResponse}, // Impossible while loaded.
            {"UnloadWithMetadata", CallStatus::InvalidResponse}, {"UnloadUnknownStatus", CallStatus::UnexpectedStatus},
            {"UnloadInvalidHandle", CallStatus::InvalidHandle}};
        for (const auto& [mode, status] : cases)
        {
            Select(fixture, mode);
            const auto query = fixture.GetUnloadState();
            {
                INFO(std::string("Malformed unload response accepted: ") + mode);
                REQUIRE((query.status == status && !query.value && !query.diagnostic.empty()));
            }
        }
        Select(fixture, "None");

        // A released owner cannot invoke, so a second registration selects modes.
        const auto control = LoadFixture(*host, checkArguments);
        {
            INFO("Release failed");
            REQUIRE(fixture.Release().Succeeded());
        }
        {
            INFO("A released owner did not report Pending");
            REQUIRE(fixture.GetUnloadState().value == ComponentUnloadState::Pending);
        }
        Select(control, "UnloadCollected");
        {
            INFO("A collected observation was rejected");
            REQUIRE(fixture.GetUnloadState().value == ComponentUnloadState::Collected);
        }
        Select(control, "UnloadNotRequested");
        {
            INFO("A released owner accepted NotRequested");
            REQUIRE(fixture.GetUnloadState().status == CallStatus::InvalidResponse);
        }
        Select(control, "None");
    }

    TEST_CASE("owner destruction releases and forgets exactly once", "[.fixture]")
    {
        auto host = StartHost(checkArguments);
        const auto control = LoadFixture(*host, checkArguments);
        const auto releases = Counter(control, "Releases");
        const auto forgets = Counter(control, "Forgets");
        {
            auto released = LoadFixture(*host, checkArguments);
            {
                INFO("Release failed");
                REQUIRE(released.Release().Succeeded());
            }
        }
        {
            INFO("A released owner's destruction repeated release or kept its observation");
            REQUIRE((Counter(control, "Releases") == releases + 1 && Counter(control, "Forgets") == forgets + 1));
        }
        {
            const auto live = LoadFixture(*host, checkArguments);
        }
        {
            INFO("A live owner's destruction skipped release or observation cleanup");
            REQUIRE((Counter(control, "Releases") == releases + 2 && Counter(control, "Forgets") == forgets + 2));
        }
        {
            INFO("Call failures reset readiness");
            REQUIRE(host->IsInitialized());
        }
    }

    TEST_CASE("an older bridge version is rejected", "[.legacy]")
    {
        ExpectRejected(checkArguments, "expected 22, received 21", 21, true);
    }

    TEST_CASE("a bridge table of another size is rejected", "[.oversized]")
    {
        ExpectRejected(checkArguments, "table size", 22, true);
    }

    TEST_CASE("a bridge table with a null entry is rejected", "[.missing]")
    {
        ExpectRejected(checkArguments, "GetComponentUnloadState", 22, true);
    }

    TEST_CASE("an assembly without the bridge entry point is rejected", "[.notbridge]")
    {
        ExpectRejected(checkArguments, "GetBridgeFunctions", std::nullopt, true);
    }
} // namespace
