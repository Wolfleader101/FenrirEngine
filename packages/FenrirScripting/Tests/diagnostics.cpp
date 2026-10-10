#include "FenrirDotNetHost/DotNetHost.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
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
        Check(argc == 4, "Expected mode, runtime configuration, and bridge");
        const auto mode = std::filesystem::path(argv[1]);
        Int32CallResult retained;
        Utf8StringResult retainedString;
        {
            DotNetHost host;
            Check(host.AddInt32(1, 2).status == ManagedCallStatus::NotInitialized, "Unready call accepted");
            const auto initialized = host.Initialize({argv[2], argv[3], {}});
            if (mode == "legacy")
            {
                Check(!initialized.success && initialized.bridgeVersion == 5 && !initialized.interopMethodResolved &&
                          !host.IsInitialized(),
                      "Old ABI was resolved or published");
                Check(initialized.diagnostic.find("expected 6, received 5") != std::string::npos,
                      "Version diagnostic missing");
                Check(host.AddInt32(1, 2).status == ManagedCallStatus::NotInitialized, "Old ABI was invoked");
                std::cout << "Legacy bridge rejected before addition resolution.\n";
                return EXIT_SUCCESS;
            }
            Check(initialized.success && initialized.bridgeVersion == 6 && initialized.componentMethodsResolved,
                  initialized.diagnostic.c_str());
            if (mode == "real")
            {
                retained = host.AddInt32(std::numeric_limits<std::int32_t>::max(), 1);
                Check(retained.status == ManagedCallStatus::Overflow && !retained.value &&
                          retained.diagnostic.find("OverflowException") != std::string::npos,
                      "Real exception details missing");
                const auto recovered = host.AddInt32(20, 22);
                Check(recovered.value == 42 && recovered.diagnostic.empty() && !recovered.diagnosticTruncated,
                      "Recovery retained an error");
                Check(host.EchoUtf8("after overflow").value == "after overflow", "String interop regressed");
                retainedString = host.EchoUtf8("\xC3(");
                Check(retainedString.status == ManagedCallStatus::InvalidArgument && !retainedString.value &&
                          retainedString.diagnostic.find("DecoderFallbackException") != std::string::npos,
                      "Decoder details missing");
                const auto echoRecovery = host.EchoUtf8("\xCF\x80\xF0\x9F\x90\xBA");
                Check(echoRecovery.value == "\xCF\x80\xF0\x9F\x90\xBA" && echoRecovery.diagnostic.empty() &&
                          !echoRecovery.diagnosticTruncated,
                      "Echo recovery retained diagnostics");
                Check(host.EchoUtf8({}).value == "" &&
                          host.EchoUtf8(std::string("A\0B", 3)).value == std::string("A\0B", 3),
                      "Empty or embedded-null payload changed");
                const auto greeting = host.GetGreeting();
                Check(greeting.value && greeting.value->size() == 27 && greeting.diagnostic.empty() &&
                          !greeting.diagnosticTruncated,
                      "Greeting success retained diagnostics");
            }
            else
            {
                const auto once = host.AddInt32(12, 0);
                Check(once.status == ManagedCallStatus::ManagedException && !once.value &&
                          host.AddInt32(100, 0).value == 1,
                      "Operation was repeated to obtain its diagnostic");
                retained = host.AddInt32(1, 0);
                std::string expected;
                for (int i = 0; i < 2048; ++i)
                    expected += "\xCF\x80";
                Check(retained.status == ManagedCallStatus::ManagedException && !retained.value &&
                          retained.diagnosticTruncated && retained.diagnostic == expected,
                      "Bounded Unicode diagnostic failed");
                const auto fallback = host.AddInt32(2, 0);
                Check(fallback.status == ManagedCallStatus::Overflow && !fallback.value &&
                          !fallback.diagnostic.empty() && !fallback.diagnosticTruncated,
                      "Formatting failure replaced original status");
                for (int input : {3, 4, 5, 6})
                {
                    const auto invalid = host.AddInt32(input, 0);
                    Check(invalid.status == ManagedCallStatus::ManagedException && !invalid.value &&
                              invalid.diagnostic.find("Managed diagnostic") != std::string::npos &&
                              !invalid.diagnosticTruncated,
                          "Invalid diagnostic trusted or changed operation status");
                }
                const auto invalidSuccess = host.AddInt32(7, 0);
                Check(invalidSuccess.status == ManagedCallStatus::InvalidResponse && !invalidSuccess.value,
                      "Success with diagnostic metadata accepted");
                Check(host.AddInt32(8, 0).status == ManagedCallStatus::UnexpectedStatus, "Unknown status accepted");
                const auto noSpace = host.AddInt32(9, 0);
                Check(noSpace.status == ManagedCallStatus::ManagedException && noSpace.diagnosticTruncated &&
                          !noSpace.diagnostic.empty(),
                      "Empty truncated diagnostic lost fallback/flag");
                Check(host.AddInt32(10, 0).diagnostic == std::string("A\0B", 3), "Embedded null lost");
                Check(host.AddInt32(20, 22).value == 42 && host.AddInt32(20, 22).diagnostic.empty(),
                      "Fixture recovery failed");
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
                for (bool greeting : {false, true})
                {
                    for (const auto& entry : cases)
                    {
                        Check(host.AddInt32(200 + entry.mode, 0).status == ManagedCallStatus::Success,
                              "Selecting mode failed");
                        const auto failed = greeting ? host.GetGreeting() : host.EchoUtf8("test");
                        Check(failed.status == entry.status && !failed.value && !failed.diagnostic.empty(),
                              "String failure status, diagnostic or partial output incorrect");
                        Check(host.AddInt32(101, 0).value == entry.calls,
                              "Failed string operation was retried or query failure copied");
                        if (entry.mode == 1 || entry.mode == 2)
                        {
                            Check(failed.diagnostic == expected && failed.diagnosticTruncated,
                                  "String truncation failed");
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
                    Check(host.AddInt32(200, 0).status == ManagedCallStatus::Success, "Resetting fixture failed");
                    const auto recovered = greeting ? host.GetGreeting() : host.EchoUtf8("test");
                    Check(recovered.value == std::string("A\0B", 3) && recovered.diagnostic.empty() &&
                              !recovered.diagnosticTruncated && host.AddInt32(101, 0).value == 2,
                          "String recovery retained diagnostics or changed call count");
                }
                auto loaded = host.LoadComponent(argv[3]); // Controlled synthetic registration in the fixture.
                Check(loaded.status == ManagedCallStatus::Success && loaded.value, "Invocation fixture load failed");
                const auto before = host.AddInt32(100, 0).value;
                const auto invokeOnce = loaded.value->InvokeInt32("Fixture", "Method", 12, 0);
                Check(invokeOnce.status == ManagedCallStatus::ManagedException && !invokeOnce.value && before &&
                          host.AddInt32(100, 0).value == *before + 1,
                      "Component operation was repeated to obtain diagnostics");
                Check(loaded.value->InvokeInt32("Fixture", "Method", 1, 0).diagnostic == expected &&
                          loaded.value->InvokeInt32("Fixture", "Method", 1, 0).diagnosticTruncated,
                      "Component fixture truncation failed");
                for (int input : {3, 4, 5, 6})
                {
                    const auto bad = loaded.value->InvokeInt32("Fixture", "Method", input, 0);
                    Check(bad.status == ManagedCallStatus::ManagedException && !bad.value &&
                              bad.diagnostic.find("Managed diagnostic") != std::string::npos,
                          "Malformed component diagnostic trusted");
                }
                const auto badSuccess = loaded.value->InvokeInt32("Fixture", "Method", 7, 0);
                Check(badSuccess.status == ManagedCallStatus::InvalidResponse && !badSuccess.value,
                      "Component success with diagnostics accepted");
                const auto unknown = loaded.value->InvokeInt32("Fixture", "Method", 8, 0);
                Check(unknown.status == ManagedCallStatus::UnexpectedStatus && !unknown.value,
                      "Unknown component status accepted");
                Check(loaded.value->InvokeInt32("Fixture", "Method", 11, 0).status == ManagedCallStatus::InvalidHandle,
                      "Managed invalid token status lost");
                Check(loaded.value->InvokeInt32("Fixture", "Method", 20, 22).value == 42,
                      "Component fixture recovery failed");
                Check(loaded.value->GetUnloadStatus().value == ComponentUnloadState::NotRequested,
                      "Live observation did not report NotRequested");
                for (int selector : {20, 21, 22, 23, 24, 25})
                {
                    Check(host.AddInt32(200 + selector, 0).status == ManagedCallStatus::Success,
                          "Selecting unload fixture mode failed");
                    const auto query = loaded.value->GetUnloadStatus();
                    const auto expectedStatus = selector == 24   ? ManagedCallStatus::UnexpectedStatus
                                                : selector == 25 ? ManagedCallStatus::InvalidHandle
                                                                 : ManagedCallStatus::InvalidResponse;
                    Check(query.status == expectedStatus && !query.value && !query.diagnostic.empty(),
                          "Malformed unload state/status/diagnostic metadata accepted");
                }
                Check(host.AddInt32(200, 0).status == ManagedCallStatus::Success, "Resetting unload fixture failed");
                Check(loaded.value->Release().status == ManagedCallStatus::Success &&
                          loaded.value->GetUnloadStatus().value == ComponentUnloadState::Pending,
                      "Released owner lost its observation");
                Check(host.AddInt32(222, 0).status == ManagedCallStatus::Success &&
                          loaded.value->GetUnloadStatus().value == ComponentUnloadState::Collected,
                      "Collected observation was rejected");
                Check(host.AddInt32(226, 0).status == ManagedCallStatus::Success &&
                          loaded.value->GetUnloadStatus().status == ManagedCallStatus::InvalidResponse,
                      "Released owner accepted NotRequested");
                Check(host.AddInt32(200, 0).status == ManagedCallStatus::Success, "Resetting unload fixture failed");
                const auto releaseCount = host.AddInt32(102, 0).value;
                const auto forgetCount = host.AddInt32(103, 0).value;
                Check(releaseCount && forgetCount, "Cleanup counters missing");
                loaded.value.reset();
                Check(host.AddInt32(102, 0).value == *releaseCount && host.AddInt32(103, 0).value == *forgetCount + 1,
                      "Released owner destruction repeated release or leaked observation");
                auto scoped = host.LoadComponent(argv[3]);
                Check(scoped.value != nullptr, "Scoped cleanup fixture load failed");
                scoped.value.reset();
                Check(host.AddInt32(102, 0).value == *releaseCount + 1 &&
                          host.AddInt32(103, 0).value == *forgetCount + 2,
                      "Live owner destruction omitted release or observation cleanup");
            }
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
