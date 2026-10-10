#pragma once

#include "ManagedComponent.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Fenrir::Scripting
{
    struct HostConfiguration
    {
        std::filesystem::path runtimeConfigPath;
        // Assembly identity Fenrir.ManagedHost, exposing the required Bootstrap methods.
        std::filesystem::path bridgeAssemblyPath;
        // Guides initial discovery and framework resolution. Empty uses the defaults; an override
        // must be absolute. nethost may reuse hostfxr once it is loaded in the process.
        std::filesystem::path dotnetRoot;
    };

    struct InitializationResult
    {
        bool success = false;
        // Owning text: remains valid independently of the host and subsequent calls.
        // Includes hostfxr's captured explanation in UTF-8 when one is emitted.
        std::string diagnostic;
        // Progress of this attempt, independent of full initialization success.
        bool hostFxrLoaded = false;
        bool requiredExportsResolved = false;
        bool runtimeConfigInitialized = false;
        bool hostContextClosed = false;
        // This attempt obtained CoreCLR's load_assembly and get_function_pointer delegates.
        // False does not prove that the process runtime is absent (startup can fail partway).
        bool runtimeDelegateObtained = false;
        // The bridge assembly is loaded into the default load context.
        bool bridgeAssemblyLoaded = false;
        // The version the bridge reported during the handshake, including a mismatch.
        std::optional<std::int32_t> bridgeVersion;
    };

    struct HostFxrDiscoveryResult
    {
        bool success = false;
        // Populated only on success. The returned path and diagnostic own their data.
        std::filesystem::path hostFxrPath;
        std::string diagnostic;
    };

    /**
     * @brief Native access to the managed hosting bridge.
     *
     * Construction does not discover or start .NET. Once started, CoreCLR has
     * process lifetime; this wrapper does not own the runtime's lifetime.
     * Destruction releases wrapper-owned state without unloading CoreCLR or
     * its native hosting libraries. Closing a hostfxr context is not CLR shutdown.
     *
     * nethost is a native dependency. Discovery locates hostfxr without loading it.
     * Initialize starts or reuses CoreCLR, obtains the load_assembly and
     * get_function_pointer runtime delegates, closes its temporary hostfxr context, and
     * loads the bridge. Readiness requires a matching, complete bridge function table.
     * This bridge is process-resident; collectible component unloading is separate.
     */
    class DotNetHost
    {
      public:
        DotNetHost() noexcept;
        ~DotNetHost() noexcept;

        DotNetHost(const DotNetHost&) = delete;
        DotNetHost& operator=(const DotNetHost&) = delete;
        DotNetHost(DotNetHost&&) = delete;
        DotNetHost& operator=(DotNetHost&&) = delete;

        /**
         * @brief Locate hostfxr without changing this wrapper's readiness.
         *
         * An empty root uses nethost's environment/registered-installation search.
         * An explicit root must be absolute and contain the target .NET installation.
         * It overrides the initial default search; nethost may reuse an already loaded
         * hostfxr regardless of the root. Native failures include their hexadecimal
         * status code in the diagnostic. Discovery does not validate bridge paths.
         */
        [[nodiscard]] HostFxrDiscoveryResult DiscoverHostFxr(const std::filesystem::path& dotnetRoot = {}) const;

        /**
         * @brief Prepare this wrapper to use the configured managed bridge.
         *
         * Loads the bridge into the default load context, resolves Bootstrap.GetBridgeFunctions
         * and fills the bridge function table. Success requires a matching table version
         * (currently 8) and size, and a non-null entry in every slot. The process can hold only
         * one bridge assembly; a different bridge path in the same process fails to load.
         * Both configuration paths must name existing files;
         * relative paths are resolved against the current working directory.
         * Different runtime properties in an existing runtime are rejected until
         * we can verify compatibility.
         * Calls on the same wrapper must be externally serialized.
         * Each attempt resets readiness; a failed attempt leaves it false. Loaded
         * managed assemblies and CoreCLR remain resident regardless of the result.
         */
        [[nodiscard]] InitializationResult Initialize(const HostConfiguration& configuration);

        /**
         * @brief Whether this wrapper is ready to use its managed bridge.
         *
         * Does not report whether another component has already started CoreCLR.
         */
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Loads a separate assembly into a collectible context. Relative paths are
        // resolved against the current directory; path text is strict UTF-8 at the
        // managed interface. Returned ownership survives host destruction/reinitialization.
        // Failures leave readiness intact. Native allocation failures may throw.
        [[nodiscard]] ComponentLoadResult LoadComponent(const std::filesystem::path& assemblyPath) const;

      private:
        struct State;
        std::unique_ptr<State> state;
    };
} // namespace Fenrir::Scripting
