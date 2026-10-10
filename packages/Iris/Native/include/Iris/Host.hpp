#pragma once

#include "CallResult.hpp"
#include "Component.hpp"
#include "Log.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Iris
{
    struct HostConfiguration
    {
        std::filesystem::path runtimeConfigPath;
        // The Iris.Bridge bridge assembly.
        std::filesystem::path bridgeAssemblyPath;
        // Empty uses nethost's default search; otherwise an absolute .NET installation root, used
        // for discovery and framework resolution. nethost may reuse an already loaded hostfxr.
        std::filesystem::path dotnetRoot;
        // Assemblies every component shares as one copy in the default load context, such as an API
        // that components compile against. A component's reference to one always binds to the shared
        // copy, even when it deploys its own. Sharing is process-wide and permanent, including for
        // assemblies shared before a later one fails; sharing a different file under an already
        // shared assembly name fails initialization.
        std::vector<std::filesystem::path> sharedAssemblies;
    };

    struct InitializationResult
    {
        bool success = false;
        // Owning UTF-8, including hostfxr's own messages when it emits any.
        std::string diagnostic;

        // How far this attempt got. A false flag after a later step started does not prove CoreCLR
        // is absent from the process.
        bool hostFxrLoaded = false;
        bool requiredExportsResolved = false;
        bool runtimeConfigInitialized = false;
        bool hostContextClosed = false;
        bool runtimeDelegateObtained = false;
        bool bridgeAssemblyLoaded = false;
        // The version the bridge reported during the handshake, including a mismatched one.
        std::optional<std::int32_t> bridgeVersion;
    };

    struct HostFxrDiscoveryResult
    {
        bool success = false;
        // Present only on success.
        std::filesystem::path hostFxrPath;
        std::string diagnostic;
    };

    /**
     * @brief Starts CoreCLR and connects to the resident managed bridge.
     *
     * Construction does not start .NET. CoreCLR, hostfxr and the bridge have process lifetime:
     * destroying the host or a failed initialization unloads none of them. Components loaded
     * through the host have their own owners and may outlive it.
     *
     * Calls on one host must be externally serialized.
     */
    class Host
    {
      public:
        Host() noexcept;
        ~Host() noexcept;

        Host(const Host&) = delete;
        Host& operator=(const Host&) = delete;
        Host(Host&&) = delete;
        Host& operator=(Host&&) = delete;

        // Locates hostfxr through nethost without loading it. An explicit root must be absolute.
        [[nodiscard]] static HostFxrDiscoveryResult DiscoverHostFxr(const std::filesystem::path& dotnetRoot = {});

        /**
         * @brief Starts or reuses CoreCLR and loads the bridge.
         *
         * Both paths must name existing files; relative paths resolve against the current
         * directory. Joining a running runtime with different properties is rejected. The bridge is
         * loaded into the default load context, so a process holds one bridge assembly. Readiness
         * requires a bridge function table of the expected version and size with every slot
         * present. Each attempt resets readiness first.
         */
        [[nodiscard]] InitializationResult Initialize(const HostConfiguration& configuration);

        [[nodiscard]] bool IsInitialized() const noexcept;

        /**
         * @brief Registers the process-wide sink for bridge messages, or clears it when `sink` is empty.
         *
         * There is one bridge and one sink per process, so this replaces whatever any host registered. The
         * sink survives the host that set it and stays registered until cleared or replaced; its captured
         * state must outlive that. Needs an initialized host; messages sent before the sink is set,
         * including during Initialize, are not delivered. See LogSink for what the sink may do.
         */
        [[nodiscard]] CallResult<void> SetLogSink(LogSink sink);

        // Loads an assembly and its private dependencies into a new collectible load context.
        // Relative paths resolve against the current directory. A failure leaves the host ready.
        [[nodiscard]] CallResult<Component> LoadComponent(const std::filesystem::path& assemblyPath) const;

      private:
        struct State;
        std::unique_ptr<State> state;
    };
} // namespace Iris
