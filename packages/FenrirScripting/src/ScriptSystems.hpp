#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include <Iris/Iris.hpp>

#include "FenrirLogger/ILogger.hpp"
#include "SystemHost.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief The script's systems as the engine runs them: one system host per generation, found through a
     * SystemHostLoader, started once, run with one call per phase and stopped once. The engine's rules live here: a
     * script with no host has no systems; a host that cannot be bound, listed or created fails the load; a phase the
     * mask leaves out costs nothing; a phase asked for off the main thread is skipped with one error; a failing run is
     * logged and the next phase still runs; OnDestroy runs once at Stop and nothing runs after it. What runs inside a
     * phase (the order of systems, the playback after each, a system that throws) is the host's business.
     */
    class ScriptSystems
    {
      public:
        using Result = Iris::CallResult<void>;

        /** @brief `loader` and `logger` are borrowed and must outlive it. */
        ScriptSystems(SystemHostLoader& loader, ILogger* logger);
        ScriptSystems(const ScriptSystems&) = delete;
        ScriptSystems& operator=(const ScriptSystems&) = delete;

        /**
         * @brief Finds the generation's host, records the calling thread as the main thread, reads the mask and runs
         * OnCreate. Success and nothing held when the script defines no systems. A failure holds nothing and is the
         * load's failure.
         */
        [[nodiscard]] Result Start(LoadedScriptAssembly& generation);

        /**
         * @brief One transition for the phase: nothing when no host is held or the mask bit is clear; off the main
         * thread one error and nothing invoked; a failing run logged.
         */
        void RunPhase(int phase);

        /** @brief OnDestroy once if a host is held (a failure logged), then nothing held. */
        void Stop();

        /** @brief Whether a host is held: the loaded generation defines systems and they have started. */
        [[nodiscard]] bool HasSystems() const noexcept
        {
            return m_host != nullptr;
        }

        /** @brief How many phases have run since construction, across generations. */
        [[nodiscard]] std::size_t PhaseCalls() const noexcept
        {
            return m_phaseCalls;
        }

      private:
        SystemHostLoader* m_loader;
        ILogger* m_logger;

        std::unique_ptr<SystemHost> m_host;
        std::thread::id m_mainThread;
        std::int32_t m_phaseMask = 0; // Bit n is set when the held host has a system in phase n.
        std::size_t m_phaseCalls = 0;
    };
} // namespace Fenrir::Detail
