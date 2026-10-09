#pragma once

#include <filesystem>
#include <string>

namespace Fenrir::Scripting
{
    struct HostConfiguration
    {
        std::filesystem::path runtimeConfigPath;
        std::filesystem::path bridgeAssemblyPath;
    };

    struct InitializationResult
    {
        bool success = false;
        // Owning text: remains valid independently of the host and subsequent calls.
        std::string diagnostic;
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
     * nethost is a native dependency. Discovery locates hostfxr without loading it
     * or starting CoreCLR. Runtime initialization and managed invocation are not
     * implemented yet.
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
         * It overrides the default search. Native failures include their hexadecimal
         * status code in the diagnostic. Discovery does not validate bridge paths.
         */
        [[nodiscard]] HostFxrDiscoveryResult DiscoverHostFxr(const std::filesystem::path& dotnetRoot = {}) const;

        /**
         * @brief Prepare this wrapper to use the configured managed bridge.
         *
         * Success will mean both the runtime and bridge are ready for this wrapper.
         * Currently returns a not-implemented diagnostic without inspecting paths
         * or changing initialization status. Configuration is not retained yet.
         */
        [[nodiscard]] InitializationResult Initialize(const HostConfiguration& configuration);

        /**
         * @brief Whether this wrapper is ready to use its managed bridge.
         *
         * Does not report whether another component has already started CoreCLR.
         */
        [[nodiscard]] bool IsInitialized() const noexcept;
    };
} // namespace Fenrir::Scripting
