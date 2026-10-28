#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

#include <Iris/Iris.hpp>

namespace Fenrir::Detail
{
    /**
     * @brief What a loaded generation is to the GenerationLifecycle: something to release when it is replaced or
     * refused, and to wait for until the runtime has collected it. What the generation contains (the components and
     * systems it defines) is the host's business, through the lifecycle's hooks, which receive it as this type.
     */
    class LoadedScriptAssembly
    {
      public:
        LoadedScriptAssembly() = default;
        virtual ~LoadedScriptAssembly() = default;
        LoadedScriptAssembly(const LoadedScriptAssembly&) = delete;
        LoadedScriptAssembly& operator=(const LoadedScriptAssembly&) = delete;

        /** @brief Releases the script assembly, requesting its unloading. Collection happens later. */
        [[nodiscard]] virtual Iris::CallResult<void> Release() = 0;

        /**
         * @brief Waits up to `timeout` for the released script assembly to be collected: Collected, or Pending if the
         * time ran out, or NotRequested if it was never released.
         */
        [[nodiscard]] virtual Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(
            std::chrono::milliseconds timeout) = 0;
    };

    /**
     * @brief Where the GenerationLifecycle loads a generation's copy of the script assembly from, and what comes back.
     * Two adapters stand behind it: one over an Iris host, in Scripting.cpp, whose loaded script assembly is an
     * Iris component; and a fake in the rules tests, which decides when a load fails and when a generation counts as
     * collected, so every reload rule is checked with no .NET.
     */
    class ScriptAssemblyLoader
    {
      public:
        ScriptAssemblyLoader() = default;
        virtual ~ScriptAssemblyLoader() = default;
        ScriptAssemblyLoader(const ScriptAssemblyLoader&) = delete;
        ScriptAssemblyLoader& operator=(const ScriptAssemblyLoader&) = delete;

        /**
         * @brief Loads the assembly at `path` as a new generation.
         *
         * @return the loaded script assembly, or why it could not be loaded
         */
        [[nodiscard]] virtual Iris::CallResult<std::unique_ptr<LoadedScriptAssembly>> Load(
            const std::filesystem::path& path) = 0;
    };
} // namespace Fenrir::Detail
