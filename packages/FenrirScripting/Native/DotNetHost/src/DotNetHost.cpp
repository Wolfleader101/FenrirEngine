#include "FenrirDotNetHost/DotNetHost.hpp"

#include "HostFxrLibrary.hpp"

#include <coreclr_delegates.h>

#include <cstdint>
#include <iomanip>
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
        constexpr char_t BridgeTypeName[] = L"Fenrir.ManagedHost.Bootstrap, Fenrir.ManagedHost";
        constexpr char_t BridgeVersionMethod[] = L"GetBridgeVersion";
#else
        using GetBridgeVersionFn = std::int32_t (*)();
        constexpr char_t BridgeTypeName[] = "Fenrir.ManagedHost.Bootstrap, Fenrir.ManagedHost";
        constexpr char_t BridgeVersionMethod[] = "GetBridgeVersion";
#endif
        constexpr std::int32_t ExpectedBridgeVersion = 1;

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
        hostfxr_handle handle = nullptr;
        const hostfxr_initialize_parameters parameters{
            sizeof(hostfxr_initialize_parameters), nullptr, configuration.dotnetRoot.c_str()};
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
            const std::int32_t delegateStatus = loaded.library->getRuntimeDelegate(
                handle, hdt_load_assembly_and_get_function_pointer, &delegate);
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
            return result;
        }

        void* method = nullptr;
        const int bridgeStatus = state->loadAssemblyAndGetFunctionPointer(
            bridgeAssembly.c_str(), BridgeTypeName, BridgeVersionMethod, UNMANAGEDCALLERSONLY_METHOD, nullptr,
            &method);
        if (bridgeStatus != 0 || method == nullptr)
        {
            result.diagnostic = StatusDiagnostic("load_assembly_and_get_function_pointer(GetBridgeVersion)", bridgeStatus);
            if (bridgeStatus == 0)
            {
                result.diagnostic += " No managed method pointer was returned.";
            }
            return result;
        }
        result.bridgeMethodResolved = true;

        // This method is int32_t() with Cdecl, not the SDK's default component-entry ABI.
        const auto getBridgeVersion = reinterpret_cast<GetBridgeVersionFn>(method);
        result.bridgeVersion = getBridgeVersion();
        if (*result.bridgeVersion != ExpectedBridgeVersion)
        {
            result.diagnostic = "Managed bridge version mismatch: expected " + std::to_string(ExpectedBridgeVersion) +
                                ", received " + std::to_string(*result.bridgeVersion) + '.';
            return result;
        }
        state->getBridgeVersion = getBridgeVersion;
        result.success = true;
        result.diagnostic = "Managed bridge initialized; bridge version " + std::to_string(*result.bridgeVersion) + '.';
        return result;
    }

    bool DotNetHost::IsInitialized() const noexcept
    {
        return state != nullptr && state->getBridgeVersion != nullptr;
    }
} // namespace Fenrir::Scripting
