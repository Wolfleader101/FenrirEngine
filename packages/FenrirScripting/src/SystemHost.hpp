#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Iris/Iris.hpp>

#include "ScriptAssemblyLoader.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief One stage system the script defines (`[StageSystem(stage, Order = order)]`), as its host reports it: the
     * system's full name, for diagnostics, and the number of its (stage, order) group, which the host runs it by.
     */
    struct StageSystemReport
    {
        std::string system;
        std::string stage;
        std::int32_t order = 0;
        std::int32_t group = 0;
    };

    /**
     * @brief The script's one systems object, found in a generation and bound: what the engine calls once per phase.
     * It makes the script's systems and runs them; which phases run, on which thread, and what a failure means are
     * ScriptSystems' rules, kept on this side of the seam.
     */
    class SystemHost
    {
      public:
        SystemHost() = default;
        virtual ~SystemHost() = default;
        SystemHost(const SystemHost&) = delete;
        SystemHost& operator=(const SystemHost&) = delete;

        /** @brief The phases the script has a system in: bit n is set when phase n has one. */
        [[nodiscard]] virtual Iris::CallResult<std::int32_t> PhaseMask() = 0;

        /** @brief The script's stage systems, each with its stage, order and group; none if it has no stage system. */
        [[nodiscard]] virtual Iris::CallResult<std::vector<StageSystemReport>> StageSystems() = 0;

        /** @brief Runs every system's OnCreate, once the generation is loaded. */
        [[nodiscard]] virtual Iris::CallResult<void> OnCreate() = 0;

        /** @brief Runs the systems of one phase, in order, with their structural changes played back after each. */
        [[nodiscard]] virtual Iris::CallResult<void> Run(std::int32_t phase) = 0;

        /**
         * @brief Runs the systems of one stage group, in name order, with no playback: what they record waits for
         * their host. A system that throws has what it recorded dropped.
         */
        [[nodiscard]] virtual Iris::CallResult<void> RunStage(std::int32_t group) = 0;

        /** @brief Runs every system's OnDestroy, before the generation is released. */
        [[nodiscard]] virtual Iris::CallResult<void> OnDestroy() = 0;
    };

    /**
     * @brief Where a generation's system host comes from. Two adapters stand behind it: one over the Iris component a
     * generation is, in Scripting.cpp, which finds the generated type and binds its methods by name; and a fake in the
     * rules tests, which decides whether there is a host and what fails, so every rule of ScriptSystems is checked
     * with no .NET.
     */
    class SystemHostLoader
    {
      public:
        SystemHostLoader() = default;
        virtual ~SystemHostLoader() = default;
        SystemHostLoader(const SystemHostLoader&) = delete;
        SystemHostLoader& operator=(const SystemHostLoader&) = delete;

        /**
         * @brief Finds and binds the generation's system host.
         *
         * @return the host; NotFound when the script defines no systems; otherwise why it could not be bound
         */
        [[nodiscard]] virtual Iris::CallResult<std::unique_ptr<SystemHost>> Find(LoadedScriptAssembly& generation) = 0;
    };
} // namespace Fenrir::Detail
