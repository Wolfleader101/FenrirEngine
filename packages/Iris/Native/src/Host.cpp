#include "Iris/Host.hpp"

#include "BridgeFunctions.hpp"
#include "ComponentState.hpp"
#include "HostFxrDiagnostics.hpp"
#include "HostFxrLibrary.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "StringEncoding.hpp"

#include <coreclr_delegates.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace Iris
{
    namespace
    {
        constexpr const char* BridgeTypeName = "Iris.Bridge.Bootstrap, Iris.Bridge";
        constexpr const char* BridgeEntryPoint = "GetBridgeFunctions";

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

        // CoreCLR owns these delegates; closing the host context does not invalidate them.
        struct RuntimeDelegates
        {
            load_assembly_fn loadAssembly = nullptr;
            get_function_pointer_fn getFunctionPointer = nullptr;
        };

        // Loads the bridge into the default load context and fills its function table. Returns
        // the table only when the version and size match and every slot is non-null.
        std::shared_ptr<const Detail::BridgeFunctions> LoadBridge(const RuntimeDelegates& runtime,
                                                                  const std::filesystem::path& bridgeAssembly,
                                                                  InitializationResult& result)
        {
            const int loadStatus = runtime.loadAssembly(bridgeAssembly.c_str(), nullptr, nullptr);
            if (loadStatus != 0)
            {
                result.diagnostic = StatusDiagnostic("load_assembly(bridge)", loadStatus) +
                                    " The file may be missing or invalid, or a different bridge assembly may"
                                    " already be loaded; a process holds only one.";
                return {};
            }
            result.bridgeAssemblyLoaded = true;

            void* entryPoint = nullptr;
            const auto typeName = Detail::Utf8ToHostString(BridgeTypeName);
            const auto methodName = Detail::Utf8ToHostString(BridgeEntryPoint);
            const int entryStatus = runtime.getFunctionPointer(
                typeName.c_str(), methodName.c_str(), UNMANAGEDCALLERSONLY_METHOD, nullptr, nullptr, &entryPoint);
            if (entryStatus != 0 || entryPoint == nullptr)
            {
                result.diagnostic = StatusDiagnostic("get_function_pointer(GetBridgeFunctions)", entryStatus);
                if (entryStatus == 0)
                {
                    result.diagnostic += " No managed method pointer was returned.";
                }
                return {};
            }

            const auto getBridgeFunctions = reinterpret_cast<Detail::GetBridgeFunctionsFn>(entryPoint);
            auto table = std::make_shared<Detail::BridgeFunctions>();
            constexpr auto tableSize = static_cast<std::int32_t>(sizeof(Detail::BridgeFunctions));
            std::int32_t reportedVersion = -1;
            std::int32_t reportedSize = -1;
            const std::int32_t status =
                getBridgeFunctions(Detail::BridgeVersion, table.get(), tableSize, &reportedVersion, &reportedSize);
            result.bridgeVersion = reportedVersion;
            if (reportedVersion != Detail::BridgeVersion)
            {
                result.diagnostic = "Managed bridge version mismatch: expected " +
                                    std::to_string(Detail::BridgeVersion) + ", received " +
                                    std::to_string(reportedVersion) + '.';
                return {};
            }
            if (reportedSize != tableSize)
            {
                result.diagnostic = "Managed bridge table size mismatch: expected " + std::to_string(tableSize) +
                                    " bytes, received " + std::to_string(reportedSize) + '.';
                return {};
            }
            if (status != 0)
            {
                result.diagnostic = StatusDiagnostic("GetBridgeFunctions", status);
                return {};
            }

            std::array<void*, Detail::BridgeFunctionNames.size()> slots{};
            std::memcpy(slots.data(), table.get(), sizeof(Detail::BridgeFunctions));
            for (std::size_t index = 0; index < slots.size(); ++index)
            {
                if (slots[index] == nullptr)
                {
                    result.diagnostic = std::string("Managed bridge table entry is missing: ") +
                                        Detail::BridgeFunctionNames[index] + '.';
                    return {};
                }
            }
            return table;
        }
    } // namespace

    struct Host::State
    {
        std::shared_ptr<const Detail::HostFxrLibrary> hostFxr;
        // Published only after a complete handshake; component owners share it.
        std::shared_ptr<const Detail::BridgeFunctions> bridge;
    };

    Host::Host() noexcept = default;

    Host::~Host() noexcept = default;

    InitializationResult Host::Initialize(const HostConfiguration& configuration)
    {
        static_assert(std::is_same_v<std::filesystem::path::value_type, char_t>);
        if (state != nullptr)
        {
            // A failed new attempt must not keep the bridge from an earlier configuration.
            state->bridge.reset();
        }
        const auto failed = [](std::string diagnostic) {
            InitializationResult result;
            result.diagnostic = std::move(diagnostic);
            return result;
        };
        if (configuration.runtimeConfigPath.empty())
        {
            return failed("A runtime configuration path is required.");
        }
        std::error_code pathError;
        const auto runtimeConfig = std::filesystem::absolute(configuration.runtimeConfigPath, pathError);
        if (pathError || !std::filesystem::is_regular_file(runtimeConfig, pathError))
        {
            return failed("The runtime configuration must name an accessible regular file.");
        }
        if (configuration.bridgeAssemblyPath.empty())
        {
            return failed("A managed bridge assembly path is required.");
        }
        const auto bridgeAssembly = std::filesystem::absolute(configuration.bridgeAssemblyPath, pathError);
        if (pathError || !std::filesystem::is_regular_file(bridgeAssembly, pathError))
        {
            return failed("The managed bridge assembly must name an accessible regular file.");
        }

        const auto discovery = DiscoverHostFxr(configuration.dotnetRoot);
        if (!discovery.success)
        {
            return failed(discovery.diagnostic);
        }

        const auto loaded = Detail::LoadHostFxr(discovery.hostFxrPath);
        if (loaded.library == nullptr)
        {
            auto result = failed(loaded.diagnostic);
            result.hostFxrLoaded = loaded.moduleLoaded;
            return result;
        }

        if (state == nullptr)
        {
            state = std::make_unique<State>();
        }
        state->hostFxr = loaded.library;

        InitializationResult result;
        result.hostFxrLoaded = true;
        result.requiredExportsResolved = true;
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
        RuntimeDelegates runtime;

        // 0 = Success, 1 = Success_HostAlreadyInitialized. Status 2 is successful
        // preparation with differing runtime properties, which we do not accept yet.
        if ((status == 0 || status == 1) && handle != nullptr)
        {
            result.runtimeConfigInitialized = true;
            void* loadAssembly = nullptr;
            void* getFunctionPointer = nullptr;
            std::int32_t delegateStatus = loaded.library->getRuntimeDelegate(handle, hdt_load_assembly, &loadAssembly);
            if (delegateStatus == 0)
            {
                delegateStatus =
                    loaded.library->getRuntimeDelegate(handle, hdt_get_function_pointer, &getFunctionPointer);
            }
            if (delegateStatus == 0 && loadAssembly != nullptr && getFunctionPointer != nullptr)
            {
                runtime.loadAssembly = reinterpret_cast<load_assembly_fn>(loadAssembly);
                runtime.getFunctionPointer = reinterpret_cast<get_function_pointer_fn>(getFunctionPointer);
                result.runtimeDelegateObtained = true;
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
        }
        if (!result.runtimeDelegateObtained || !result.hostContextClosed)
        {
            return finish(std::move(result));
        }

        auto bridge = LoadBridge(runtime, bridgeAssembly, result);
        if (bridge == nullptr)
        {
            return finish(std::move(result));
        }
        state->bridge = std::move(bridge);
        result.success = true;
        result.diagnostic = "Managed bridge initialized; bridge version " + std::to_string(*result.bridgeVersion) + '.';
        return finish(std::move(result));
    }

    bool Host::IsInitialized() const noexcept
    {
        return state != nullptr && state->bridge != nullptr;
    }

    CallResult<Component> Host::LoadComponent(const std::filesystem::path& assemblyPath) const
    {
        if (!IsInitialized())
        {
            return Detail::Failure<Component>(CallStatus::NotInitialized, "The managed bridge is not initialized.");
        }
        if (assemblyPath.empty())
        {
            return Detail::Failure<Component>(CallStatus::InvalidArgument, "A component assembly path is required.");
        }
        std::error_code error;
        const auto absolute = std::filesystem::absolute(assemblyPath, error);
        if (error)
        {
            return Detail::Failure<Component>(CallStatus::InvalidArgument,
                                              "Cannot resolve the component assembly path: " + error.message());
        }
        std::string path;
        try
        {
            path = Detail::HostStringToUtf8(absolute.native());
        }
        catch (const std::invalid_argument&)
        {
            return Detail::Failure<Component>(CallStatus::InvalidArgument,
                                              "The component path is not valid Unicode text.");
        }
        catch (const std::length_error&)
        {
            return Detail::Failure<Component>(CallStatus::InvalidArgument,
                                              "The component path is too long to convert to UTF-8.");
        }
        if (path.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return Detail::Failure<Component>(CallStatus::InvalidArgument,
                                              "Component path exceeds the bridge's 32-bit byte length limit.");
        }

        // The owner exists before managed code runs, so it releases any token the bridge returns,
        // including one that arrives with a malformed response.
        auto componentState = std::make_unique<Component::State>();
        componentState->bridge = state->bridge;
        Component component(std::move(componentState));
        Detail::ManagedDiagnosticBuffer diagnostic;
        const auto status = state->bridge->load(reinterpret_cast<const std::uint8_t*>(path.data()),
                                                static_cast<std::int32_t>(path.size()), &component.state->token,
                                                diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written,
                                                &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && component.state->token == 0)
        {
            return Detail::Failure<Component>(CallStatus::InvalidResponse,
                                              "Component load returned the invalid token 0.");
        }
        return Detail::CompleteCall<Component>(status, "LoadComponent", diagnostic, std::move(component));
    }
} // namespace Iris
