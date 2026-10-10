#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    using namespace Fenrir::Scripting;
    try
    {
        Check(argc == 4 || argc == 5, "Expected mode, runtime configuration, bridge, and optional second bridge");
        const auto mode = std::filesystem::path(argv[1]);
        Int32CallResult retained;
        ComponentInfoResult retainedString;
        {
            DotNetHost host;
            Check(host.LoadComponent(argv[3]).status == ManagedCallStatus::NotInitialized, "Unready call accepted");
            const auto initialized = host.Initialize({argv[2], argv[3], {}});
            // Handshake failures: the fixture selects its response from FENRIR_TEST_BRIDGE_MODE.
            const auto checkRejected = [&](const char* expectedText, const char* message) {
                Check(!initialized.success && !host.IsInitialized() &&
                          initialized.diagnostic.find(expectedText) != std::string::npos,
                      message);
                Check(host.LoadComponent(argv[3]).status == ManagedCallStatus::NotInitialized,
                      "A rejected bridge published readiness");
            };
            if (mode == "legacy")
            {
                Check(initialized.bridgeAssemblyLoaded && initialized.bridgeVersion == 7,
                      "Mismatched version was not reported");
                checkRejected("expected 8, received 7", "Version mismatch accepted or undiagnosed");
                std::cout << "Mismatched bridge version rejected before any table entry was used.\n";
                return EXIT_SUCCESS;
            }
            if (mode == "oversized")
            {
                Check(initialized.bridgeVersion == 8, "Matching version was not reported");
                checkRejected("table size", "Table size mismatch accepted or undiagnosed");
                std::cout << "Mismatched bridge table size rejected.\n";
                return EXIT_SUCCESS;
            }
            if (mode == "missing")
            {
                Check(initialized.bridgeVersion == 8, "Matching version was not reported");
                checkRejected("GetComponentUnloadState", "Null table entry accepted or not named");
                std::cout << "Bridge table with a missing entry rejected.\n";
                return EXIT_SUCCESS;
            }
            if (mode == "notbridge")
            {
                Check(initialized.runtimeDelegateObtained && !initialized.bridgeVersion,
                      "A non-bridge assembly reported a handshake");
                checkRejected("GetBridgeFunctions", "Non-bridge assembly accepted or undiagnosed");
                std::cout << "Assembly without the bridge entry point rejected.\n";
                return EXIT_SUCCESS;
            }
            Check(initialized.success && initialized.bridgeAssemblyLoaded && initialized.bridgeVersion == 8,
                  initialized.diagnostic.c_str());
            if (mode == "real")
            {
                // The bridge lives in the default load context: one bridge assembly per process.
                Check(argc == 5, "Real mode needs a second bridge file");
                const auto second = host.Initialize({argv[2], argv[4], {}});
                Check(!second.success && !host.IsInitialized() && !second.bridgeAssemblyLoaded &&
                          second.diagnostic.find("load_assembly") != std::string::npos,
                      "A second bridge file was loaded into the same process");
                Check(host.Initialize({argv[2], argv[3], {}}).success, "The resident bridge did not reinitialize");
                std::cout << "Real bridge handshake passed; a second bridge file is rejected in-process.\n";
                return EXIT_SUCCESS;
            }

            // The fixture bridge answers every call with a scripted response. Its synthetic component
            // routes InvokeInt32 to integer responses selected by `left`, and GetInfo (the component
            // name) to the string query/copy responses. Values 100+ read counters; 200+ select a mode.
            auto loaded = host.LoadComponent(argv[3]);
            Check(loaded.status == ManagedCallStatus::Success && loaded.value, "Fixture component load failed");
            auto& fixture = *loaded.value;
            const auto invoke = [&fixture](std::int32_t left, std::int32_t right = 0) {
                return fixture.InvokeInt32("Fixture", "Method", left, right);
            };
            const auto counter = [&invoke](std::int32_t which) { return *invoke(which).value; };
            const auto selectMode = [&invoke](std::int32_t selector) {
                Check(invoke(200 + selector).status == ManagedCallStatus::Success, "Selecting a fixture mode failed");
            };

            const auto once = invoke(12);
            Check(once.status == ManagedCallStatus::ManagedException && !once.value && counter(100) == 1,
                  "Operation was repeated to obtain its diagnostic");
            retained = invoke(1);
            std::string expected;
            for (int i = 0; i < 2048; ++i)
                expected += "\xCF\x80";
            Check(retained.status == ManagedCallStatus::ManagedException && !retained.value &&
                      retained.diagnosticTruncated && retained.diagnostic == expected,
                  "Bounded Unicode diagnostic failed");
            const auto fallback = invoke(2);
            Check(fallback.status == ManagedCallStatus::Overflow && !fallback.value && !fallback.diagnostic.empty() &&
                      !fallback.diagnosticTruncated,
                  "Formatting failure replaced original status");
            for (int input : {3, 4, 5, 6})
            {
                const auto invalid = invoke(input);
                Check(invalid.status == ManagedCallStatus::ManagedException && !invalid.value &&
                          invalid.diagnostic.find("Managed diagnostic") != std::string::npos &&
                          !invalid.diagnosticTruncated,
                      "Invalid diagnostic trusted or changed operation status");
            }
            Check(invoke(7).status == ManagedCallStatus::InvalidResponse && !invoke(7).value,
                  "Success with diagnostic metadata accepted");
            Check(invoke(8).status == ManagedCallStatus::UnexpectedStatus && !invoke(8).value,
                  "Unknown status accepted");
            const auto noSpace = invoke(9);
            Check(noSpace.status == ManagedCallStatus::ManagedException && noSpace.diagnosticTruncated &&
                      !noSpace.diagnostic.empty(),
                  "Empty truncated diagnostic lost fallback/flag");
            Check(invoke(10).diagnostic == std::string("A\0B", 3), "Embedded null lost");
            Check(invoke(11).status == ManagedCallStatus::InvalidHandle, "Managed invalid token status lost");
            Check(invoke(20, 22).value == 42 && invoke(20, 22).diagnostic.empty(), "Fixture recovery failed");

            // String adapter: query then copy, each with its own diagnostics. `calls` counts how many
            // managed invocations ran, proving failed queries never copy and failed copies never retry.
            struct StringCase
            {
                int mode;
                ManagedCallStatus status;
                int calls;
            };
            const StringCase cases[] = {
                {1, ManagedCallStatus::ManagedException, 1},  {2, ManagedCallStatus::ManagedException, 2},
                {3, ManagedCallStatus::ManagedException, 1},  {4, ManagedCallStatus::ManagedException, 2},
                {5, ManagedCallStatus::ManagedException, 1},  {6, ManagedCallStatus::ManagedException, 2},
                {7, ManagedCallStatus::InvalidResponse, 1},   {8, ManagedCallStatus::InvalidResponse, 2},
                {9, ManagedCallStatus::UnexpectedStatus, 1},  {10, ManagedCallStatus::UnexpectedStatus, 2},
                {11, ManagedCallStatus::BufferTooSmall, 2},   {12, ManagedCallStatus::InvalidArgument, 1},
                {13, ManagedCallStatus::ManagedException, 1}, {14, ManagedCallStatus::InvalidResponse, 1},
                {15, ManagedCallStatus::InvalidResponse, 2},  {16, ManagedCallStatus::InvalidResponse, 1},
                {17, ManagedCallStatus::ManagedException, 2}, {18, ManagedCallStatus::ManagedException, 2},
                {19, ManagedCallStatus::ManagedException, 2}};
            for (const auto& entry : cases)
            {
                selectMode(entry.mode);
                const auto failed = fixture.GetInfo();
                Check(failed.status == entry.status && !failed.value && !failed.diagnostic.empty(),
                      "String failure status, diagnostic or partial output incorrect");
                Check(counter(101) == entry.calls, "Failed string operation was retried or query failure copied");
                if (entry.mode == 1 || entry.mode == 2)
                {
                    Check(failed.diagnostic == expected && failed.diagnosticTruncated, "String truncation failed");
                    retainedString = failed;
                }
                if (entry.mode == 5 || entry.mode == 6 || entry.mode == 17 || entry.mode == 18)
                    Check(failed.diagnostic.find("Managed diagnostic") != std::string::npos &&
                              !failed.diagnosticTruncated,
                          "Invalid string diagnostic trusted");
                if (entry.mode == 11)
                    Check(failed.diagnostic == "Copy buffer too small", "Buffer status lost details");
                if (entry.mode == 13)
                    Check(failed.diagnosticTruncated, "Empty truncated error lost flag");
                if (entry.mode == 19)
                    Check(failed.diagnostic == std::string("A\0B", 3), "String error lost embedded null");
            }
            selectMode(0);
            const auto recovered = fixture.GetInfo();
            Check(recovered.value && recovered.value->assemblyName == std::string("A\0B", 3) &&
                      recovered.diagnostic.empty() && !recovered.diagnosticTruncated && counter(101) == 2,
                  "String recovery retained diagnostics or changed call count");

            Check(fixture.GetUnloadStatus().value == ComponentUnloadState::NotRequested,
                  "Live observation did not report NotRequested");
            for (int selector : {20, 21, 22, 23, 24, 25})
            {
                selectMode(selector);
                const auto query = fixture.GetUnloadStatus();
                const auto expectedStatus = selector == 24   ? ManagedCallStatus::UnexpectedStatus
                                            : selector == 25 ? ManagedCallStatus::InvalidHandle
                                                             : ManagedCallStatus::InvalidResponse;
                Check(query.status == expectedStatus && !query.value && !query.diagnostic.empty(),
                      "Malformed unload state/status/diagnostic metadata accepted");
            }
            selectMode(0);
            Check(fixture.Release().status == ManagedCallStatus::Success &&
                      fixture.GetUnloadStatus().value == ComponentUnloadState::Pending,
                  "Released owner lost its observation");

            // A released owner can no longer invoke, so counters come from a second fixture registration.
            auto control = host.LoadComponent(argv[3]);
            Check(control.value != nullptr, "Control fixture load failed");
            const auto controlInvoke = [&control](std::int32_t left) {
                return control.value->InvokeInt32("Fixture", "Method", left, 0);
            };
            Check(controlInvoke(222).status == ManagedCallStatus::Success &&
                      fixture.GetUnloadStatus().value == ComponentUnloadState::Collected,
                  "Collected observation was rejected");
            Check(controlInvoke(226).status == ManagedCallStatus::Success &&
                      fixture.GetUnloadStatus().status == ManagedCallStatus::InvalidResponse,
                  "Released owner accepted NotRequested");
            Check(controlInvoke(200).status == ManagedCallStatus::Success, "Resetting unload fixture failed");
            const auto releaseCount = *controlInvoke(102).value;
            const auto forgetCount = *controlInvoke(103).value;
            loaded.value.reset();
            Check(*controlInvoke(102).value == releaseCount && *controlInvoke(103).value == forgetCount + 1,
                  "Released owner destruction repeated release or leaked observation");
            auto scoped = host.LoadComponent(argv[3]);
            Check(scoped.value != nullptr, "Scoped cleanup fixture load failed");
            scoped.value.reset();
            Check(*controlInvoke(102).value == releaseCount + 1 && *controlInvoke(103).value == forgetCount + 2,
                  "Live owner destruction omitted release or observation cleanup");
            Check(host.IsInitialized(), "Call failure reset readiness");
        }
        Check(!retained.diagnostic.empty(), "Diagnostic lifetime ended with wrapper");
        Check(!retainedString.diagnostic.empty() && !retainedString.value,
              "String diagnostic lifetime ended with wrapper");
        std::cout << "Native diagnostic ownership, containment and recovery passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
