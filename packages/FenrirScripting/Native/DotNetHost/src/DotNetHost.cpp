#include "FenrirDotNetHost/DotNetHost.hpp"

#include "HostFxrDiagnostics.hpp"
#include "HostFxrLibrary.hpp"
#include "ManagedComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "Utf8Buffer.hpp"

#include <coreclr_delegates.h>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>

namespace Fenrir::Scripting
{
    namespace
    {
#if defined(_WIN32)
        using GetBridgeVersionFn = std::int32_t(__cdecl*)();
        using TryAddInt32Fn = std::int32_t(__cdecl*)(std::int32_t, std::int32_t, std::int32_t*, std::uint8_t*,
                                                     std::int32_t, std::int32_t*, std::int32_t*);
        using CopyGreetingUtf8Fn = std::int32_t(__cdecl*)(std::uint8_t*, std::int32_t, std::int32_t*, std::uint8_t*,
                                                          std::int32_t, std::int32_t*, std::int32_t*);
        using EchoUtf8Fn = std::int32_t(__cdecl*)(const std::uint8_t*, std::int32_t, std::uint8_t*, std::int32_t,
                                                  std::int32_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                  std::int32_t*);
        constexpr char_t BridgeTypeName[] = L"Fenrir.ManagedHost.Bootstrap, Fenrir.ManagedHost";
        constexpr char_t BridgeVersionMethod[] = L"GetBridgeVersion";
        constexpr char_t BridgeAddMethod[] = L"TryAddInt32";
        constexpr char_t BridgeGreetingMethod[] = L"CopyGreetingUtf8";
        constexpr char_t BridgeEchoMethod[] = L"EchoUtf8";
#else
        using GetBridgeVersionFn = std::int32_t (*)();
        using TryAddInt32Fn = std::int32_t (*)(std::int32_t, std::int32_t, std::int32_t*, std::uint8_t*, std::int32_t,
                                               std::int32_t*, std::int32_t*);
        using CopyGreetingUtf8Fn = std::int32_t (*)(std::uint8_t*, std::int32_t, std::int32_t*, std::uint8_t*,
                                                    std::int32_t, std::int32_t*, std::int32_t*);
        using EchoUtf8Fn = std::int32_t (*)(const std::uint8_t*, std::int32_t, std::uint8_t*, std::int32_t,
                                            std::int32_t*, std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        constexpr char_t BridgeTypeName[] = "Fenrir.ManagedHost.Bootstrap, Fenrir.ManagedHost";
        constexpr char_t BridgeVersionMethod[] = "GetBridgeVersion";
        constexpr char_t BridgeAddMethod[] = "TryAddInt32";
        constexpr char_t BridgeGreetingMethod[] = "CopyGreetingUtf8";
        constexpr char_t BridgeEchoMethod[] = "EchoUtf8";
#endif
        constexpr std::int32_t ExpectedBridgeVersion = 6;

        std::string StatusDiagnostic(const char* operation, std::int32_t status)
        {
            std::ostringstream text;
            text << operation << " returned status 0x" << std::hex << std::uppercase << std::setfill('0')
                 << std::setw(8) << static_cast<std::uint32_t>(status) << '.';
            return text.str();
        }

        // Own only the temporary hosting context, never the runtime or OS module.
        class HostContext
        {
          public:
            HostContext(hostfxr_handle contextHandle, hostfxr_close_fn closeFunction) noexcept
                : handle(contextHandle), close(closeFunction)
            {
            }
            ~HostContext() noexcept
            {
                if (handle != nullptr)
                {
                    close(handle);
                }
            }
            HostContext(const HostContext&) = delete;
            HostContext& operator=(const HostContext&) = delete;

            std::int32_t Close() noexcept
            {
                return close(std::exchange(handle, nullptr));
            }

          private:
            hostfxr_handle handle;
            hostfxr_close_fn close;
        };
    } // namespace

    struct DotNetHost::State
    {
        std::shared_ptr<const Detail::HostFxrLibrary> hostFxr;
        // CoreCLR owns this delegate; closing a host context does not invalidate it.
        load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointer = nullptr;
        // The process runtime owns this managed entry point. Publish after the handshake.
        GetBridgeVersionFn getBridgeVersion = nullptr;
        TryAddInt32Fn tryAddInt32 = nullptr;
        CopyGreetingUtf8Fn copyGreetingUtf8 = nullptr;
        EchoUtf8Fn echoUtf8 = nullptr;
        std::shared_ptr<const Detail::ComponentBridge> componentBridge;
    };

    DotNetHost::DotNetHost() noexcept = default;

    DotNetHost::~DotNetHost() noexcept = default;

    InitializationResult DotNetHost::Initialize(const HostConfiguration& configuration)
    {
        static_assert(std::is_same_v<std::filesystem::path::value_type, char_t>);
        if (state != nullptr)
        {
            // A failed new attempt must not retain a delegate from an earlier configuration.
            state->loadAssemblyAndGetFunctionPointer = nullptr;
            state->getBridgeVersion = nullptr;
            state->tryAddInt32 = nullptr;
            state->copyGreetingUtf8 = nullptr;
            state->echoUtf8 = nullptr;
            state->componentBridge.reset();
        }
        if (configuration.runtimeConfigPath.empty())
        {
            return {false, "A runtime configuration path is required."};
        }
        std::error_code pathError;
        const auto runtimeConfig = std::filesystem::absolute(configuration.runtimeConfigPath, pathError);
        if (pathError || !std::filesystem::is_regular_file(runtimeConfig, pathError))
        {
            return {false, "The runtime configuration must name an accessible regular file."};
        }
        if (configuration.bridgeAssemblyPath.empty())
        {
            return {false, "A managed bridge assembly path is required."};
        }
        const auto bridgeAssembly = std::filesystem::absolute(configuration.bridgeAssemblyPath, pathError);
        if (pathError || !std::filesystem::is_regular_file(bridgeAssembly, pathError))
        {
            return {false, "The managed bridge assembly must name an accessible regular file."};
        }

        const auto discovery = DiscoverHostFxr(configuration.dotnetRoot);
        if (!discovery.success)
        {
            return {false, discovery.diagnostic};
        }

        const auto loaded = Detail::LoadHostFxr(discovery.hostFxrPath);
        if (loaded.library == nullptr)
        {
            return {false, loaded.diagnostic, loaded.moduleLoaded, false};
        }

        if (state == nullptr)
        {
            state = std::make_unique<State>();
        }
        state->hostFxr = loaded.library;

        InitializationResult result{false, {}, true, true};
        // Declared before HostContext so unwind cleanup runs while capture is active.
        Detail::HostFxrDiagnostics diagnostics(loaded.library->setErrorWriter);
        const auto finish = [&diagnostics](InitializationResult outcome) {
            outcome.diagnostic = diagnostics.AppendTo(std::move(outcome.diagnostic));
            return outcome;
        };
        hostfxr_handle handle = nullptr;
        const hostfxr_initialize_parameters parameters{sizeof(hostfxr_initialize_parameters), nullptr,
                                                       configuration.dotnetRoot.c_str()};
        const auto* initializationParameters = configuration.dotnetRoot.empty() ? nullptr : &parameters;
        const std::int32_t status =
            loaded.library->initializeForRuntimeConfig(runtimeConfig.c_str(), initializationParameters, &handle);
        HostContext context(handle, loaded.library->close);
        load_assembly_and_get_function_pointer_fn assemblyLoader = nullptr;

        // 0 = Success, 1 = Success_HostAlreadyInitialized. Status 2 is successful
        // preparation with differing runtime properties, which we do not accept yet.
        if ((status == 0 || status == 1) && handle != nullptr)
        {
            result.runtimeConfigInitialized = true;
            void* delegate = nullptr;
            const std::int32_t delegateStatus =
                loaded.library->getRuntimeDelegate(handle, hdt_load_assembly_and_get_function_pointer, &delegate);
            if (delegateStatus == 0 && delegate != nullptr)
            {
                assemblyLoader = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegate);
                result.runtimeDelegateObtained = true;
                result.diagnostic = "Assembly-loading runtime delegate obtained; "
                                    "bridge handshake has not completed.";
            }
            else
            {
                result.diagnostic = StatusDiagnostic("hostfxr_get_runtime_delegate", delegateStatus);
                if (delegateStatus == 0)
                {
                    result.diagnostic += " No runtime delegate was returned.";
                }
            }
        }
        else
        {
            result.diagnostic = StatusDiagnostic("hostfxr_initialize_for_runtime_config", status);
            if (status == 2)
            {
                result.diagnostic += " Existing runtime properties differ; compatibility has not been verified.";
            }
            else if (status >= 0 && handle == nullptr)
            {
                result.diagnostic += " No host context was returned.";
            }
        }

        if (handle != nullptr)
        {
            const std::int32_t closeStatus = context.Close();
            result.hostContextClosed = closeStatus == 0;
            if (!result.hostContextClosed)
            {
                result.diagnostic = StatusDiagnostic("hostfxr_close", closeStatus) + " " + result.diagnostic;
            }
            else if (result.runtimeDelegateObtained)
            {
                state->loadAssemblyAndGetFunctionPointer = assemblyLoader;
                result.diagnostic = "Runtime delegate obtained and host context closed; "
                                    "bridge handshake has not completed.";
            }
        }
        if (!result.runtimeDelegateObtained || !result.hostContextClosed)
        {
            return finish(std::move(result));
        }

        void* method = nullptr;
        const int bridgeStatus = state->loadAssemblyAndGetFunctionPointer(
            bridgeAssembly.c_str(), BridgeTypeName, BridgeVersionMethod, UNMANAGEDCALLERSONLY_METHOD, nullptr, &method);
        if (bridgeStatus != 0 || method == nullptr)
        {
            result.diagnostic =
                StatusDiagnostic("load_assembly_and_get_function_pointer(GetBridgeVersion)", bridgeStatus);
            if (bridgeStatus == 0)
            {
                result.diagnostic += " No managed method pointer was returned.";
            }
            return finish(std::move(result));
        }
        result.bridgeMethodResolved = true;

        // This method is int32_t() with Cdecl, not the SDK's default component-entry ABI.
        const auto getBridgeVersion = reinterpret_cast<GetBridgeVersionFn>(method);
        result.bridgeVersion = getBridgeVersion();
        if (*result.bridgeVersion != ExpectedBridgeVersion)
        {
            result.diagnostic = "Managed bridge version mismatch: expected " + std::to_string(ExpectedBridgeVersion) +
                                ", received " + std::to_string(*result.bridgeVersion) + '.';
            return finish(std::move(result));
        }
        void* addMethod = nullptr;
        const int addStatus = state->loadAssemblyAndGetFunctionPointer(
            bridgeAssembly.c_str(), BridgeTypeName, BridgeAddMethod, UNMANAGEDCALLERSONLY_METHOD, nullptr, &addMethod);
        if (addStatus != 0 || addMethod == nullptr)
        {
            result.diagnostic = StatusDiagnostic("load_assembly_and_get_function_pointer(TryAddInt32)", addStatus);
            if (addStatus == 0)
            {
                result.diagnostic += " No managed method pointer was returned.";
            }
            return finish(std::move(result));
        }
        result.interopMethodResolved = true;
        void* greetingMethod = nullptr;
        const int greetingStatus =
            state->loadAssemblyAndGetFunctionPointer(bridgeAssembly.c_str(), BridgeTypeName, BridgeGreetingMethod,
                                                     UNMANAGEDCALLERSONLY_METHOD, nullptr, &greetingMethod);
        if (greetingStatus != 0 || greetingMethod == nullptr)
        {
            result.diagnostic =
                StatusDiagnostic("load_assembly_and_get_function_pointer(CopyGreetingUtf8)", greetingStatus);
            if (greetingStatus == 0)
            {
                result.diagnostic += " No managed method pointer was returned.";
            }
            return finish(std::move(result));
        }
        result.greetingMethodResolved = true;
        void* echoMethod = nullptr;
        const int echoStatus =
            state->loadAssemblyAndGetFunctionPointer(bridgeAssembly.c_str(), BridgeTypeName, BridgeEchoMethod,
                                                     UNMANAGEDCALLERSONLY_METHOD, nullptr, &echoMethod);
        if (echoStatus != 0 || echoMethod == nullptr)
        {
            result.diagnostic = StatusDiagnostic("load_assembly_and_get_function_pointer(EchoUtf8)", echoStatus);
            if (echoStatus == 0)
            {
                result.diagnostic += " No managed method pointer was returned.";
            }
            return finish(std::move(result));
        }
        result.echoMethodResolved = true;
        auto componentBridge = std::make_shared<Detail::ComponentBridge>();
        const char_t* componentNames[] = {
#if defined(_WIN32)
            L"LoadComponent",           L"CopyComponentNameUtf8", L"ReleaseComponent", L"InvokeComponentInt32",
            L"GetComponentUnloadState", L"ForgetComponentUnload"
#else
            "LoadComponent",           "CopyComponentNameUtf8", "ReleaseComponent", "InvokeComponentInt32",
            "GetComponentUnloadState", "ForgetComponentUnload"
#endif
        };
        void* componentMethods[6]{};
        for (std::size_t index = 0; index < 6; ++index)
        {
            const auto componentStatus = state->loadAssemblyAndGetFunctionPointer(
                bridgeAssembly.c_str(), BridgeTypeName, componentNames[index], UNMANAGEDCALLERSONLY_METHOD, nullptr,
                &componentMethods[index]);
            if (componentStatus != 0 || componentMethods[index] == nullptr)
            {
                result.diagnostic = StatusDiagnostic("Component bridge method resolution", componentStatus) +
                                    " Method: " + Detail::HostStringToUtf8(componentNames[index]);
                return finish(std::move(result));
            }
        }
        componentBridge->load = reinterpret_cast<Detail::ComponentBridge::LoadFn>(componentMethods[0]);
        componentBridge->copyName = reinterpret_cast<Detail::ComponentBridge::NameFn>(componentMethods[1]);
        componentBridge->release = reinterpret_cast<Detail::ComponentBridge::ReleaseFn>(componentMethods[2]);
        componentBridge->invokeInt32 = reinterpret_cast<Detail::ComponentBridge::InvokeFn>(componentMethods[3]);
        componentBridge->getUnloadState = reinterpret_cast<Detail::ComponentBridge::UnloadStateFn>(componentMethods[4]);
        componentBridge->forgetUnload = reinterpret_cast<Detail::ComponentBridge::ReleaseFn>(componentMethods[5]);
        result.componentMethodsResolved = true;
        state->componentBridge = std::move(componentBridge);
        state->getBridgeVersion = getBridgeVersion;
        state->tryAddInt32 = reinterpret_cast<TryAddInt32Fn>(addMethod);
        state->copyGreetingUtf8 = reinterpret_cast<CopyGreetingUtf8Fn>(greetingMethod);
        state->echoUtf8 = reinterpret_cast<EchoUtf8Fn>(echoMethod);
        result.success = true;
        result.diagnostic = "Managed bridge initialized; bridge version " + std::to_string(*result.bridgeVersion) + '.';
        return finish(std::move(result));
    }

    bool DotNetHost::IsInitialized() const noexcept
    {
        return state != nullptr && state->getBridgeVersion != nullptr && state->tryAddInt32 != nullptr &&
               state->copyGreetingUtf8 != nullptr && state->echoUtf8 != nullptr && state->componentBridge != nullptr;
    }

    Int32CallResult DotNetHost::AddInt32(std::int32_t left, std::int32_t right) const
    {
        if (!IsInitialized())
        {
            return {ManagedCallStatus::NotInitialized, {}, "The managed bridge is not initialized."};
        }
        std::int32_t value = 0;
        Detail::ManagedDiagnosticBuffer diagnostic;
        const std::int32_t status = state->tryAddInt32(left, right, &value, diagnostic.bytes.data(),
                                                       diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        Int32CallResult result;
        // Positive bridge error codes are distinct from hostfxr/HRESULT conventions.
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success:
            return diagnostic.ApplyTo(Int32CallResult{ManagedCallStatus::Success, value, {}});
        case ManagedCallStatus::InvalidArgument:
            result = {ManagedCallStatus::InvalidArgument, {}, "The managed call rejected an argument."};
            break;
        case ManagedCallStatus::Overflow:
            result = {ManagedCallStatus::Overflow, {}, "The managed addition overflowed a 32-bit integer."};
            break;
        case ManagedCallStatus::ManagedException:
            result = {ManagedCallStatus::ManagedException, {}, "The managed call caught an exception."};
            break;
        default:
            return {ManagedCallStatus::UnexpectedStatus,
                    {},
                    "The managed call returned an unknown status: " + std::to_string(status) + '.'};
        }
        return diagnostic.ApplyTo(std::move(result));
    }

    Utf8StringResult DotNetHost::GetGreeting() const
    {
        if (!IsInitialized())
        {
            return {ManagedCallStatus::NotInitialized, {}, "The managed bridge is not initialized."};
        }
        return Detail::ReadUtf8Buffer(state->copyGreetingUtf8);
    }

    Utf8StringResult DotNetHost::EchoUtf8(std::string_view input) const
    {
        if (!IsInitialized())
        {
            return {ManagedCallStatus::NotInitialized, {}, "The managed bridge is not initialized."};
        }
        if (input.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return {ManagedCallStatus::InvalidArgument,
                    {},
                    "The UTF-8 input exceeds the bridge's 32-bit byte length limit."};
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(input.data());
        const auto length = static_cast<std::int32_t>(input.size());
        return Detail::ReadUtf8Buffer([this, bytes, length](std::uint8_t* destination, std::int32_t capacity,
                                                            std::int32_t* requiredBytes, std::uint8_t* diagnostic,
                                                            std::int32_t diagnosticCapacity, std::int32_t* written,
                                                            std::int32_t* truncated) {
            return state->echoUtf8(bytes, length, destination, capacity, requiredBytes, diagnostic, diagnosticCapacity,
                                   written, truncated);
        });
    }
    ComponentLoadResult DotNetHost::LoadComponent(const std::filesystem::path& assemblyPath) const
    {
        if (!IsInitialized())
            return {ManagedCallStatus::NotInitialized, {}, "The managed bridge is not initialized."};
        if (assemblyPath.empty())
            return {ManagedCallStatus::InvalidArgument, {}, "A component assembly path is required."};
        std::error_code error;
        const auto absolute = std::filesystem::absolute(assemblyPath, error);
        if (error)
            return {ManagedCallStatus::InvalidArgument,
                    {},
                    "Cannot resolve the component assembly path: " + error.message()};
        const auto path = Detail::HostStringToUtf8(absolute.native());
        if (path.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return {ManagedCallStatus::InvalidArgument,
                    {},
                    "Component path exceeds the bridge's 32-bit byte length limit."};

        // Allocate all ownership storage before entering managed code. The owner
        // immediately guards any returned token, including malformed responses.
        auto componentState = std::make_unique<ManagedComponent::State>();
        componentState->bridge = state->componentBridge;
        auto component = std::unique_ptr<ManagedComponent>(new ManagedComponent(std::move(componentState)));
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t collectible = 0;
        const auto status = state->componentBridge->load(
            reinterpret_cast<const std::uint8_t*>(path.data()), static_cast<std::int32_t>(path.size()),
            &component->state->token, &collectible, diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written,
            &diagnostic.truncated);
        ComponentLoadResult result;
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success:
            if (component->state->token == 0 || collectible != 1)
                return {ManagedCallStatus::InvalidResponse,
                        {},
                        "Component load returned an invalid token or collectible flag."};
            component->state->isCollectible = true;
            result = {ManagedCallStatus::Success, std::move(component), {}};
            break;
        case ManagedCallStatus::InvalidArgument:
            result = {ManagedCallStatus::InvalidArgument, {}, "The managed component loader rejected an argument."};
            break;
        case ManagedCallStatus::ManagedException:
            result = {ManagedCallStatus::ManagedException, {}, "The managed component loader caught an exception."};
            break;
        default:
            result = {ManagedCallStatus::UnexpectedStatus,
                      {},
                      "Component load returned an unexpected status: " + std::to_string(status)};
            break;
        }
        return diagnostic.ApplyTo(std::move(result));
    }
} // namespace Fenrir::Scripting
