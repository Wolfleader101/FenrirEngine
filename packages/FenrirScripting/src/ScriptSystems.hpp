#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "SystemHost.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief An extension that declares stages, as the systems see it: the stages by full name, and how it places and
     * removes a generation's groups for them (ScriptingExtension::placeStages and removeStages with the App bound).
     */
    struct StageHost
    {
        std::string extension;
        std::vector<std::string> stages;
        std::function<void(std::vector<StageGroup>, StageChanges)> place;
        std::function<void()> remove;
    };

    /**
     * @brief What every StageGroup and StageChanges of one generation shares: the systems that run the groups and the
     * playback of their changes, while `live`. Stop clears `live`, so a group an extension kept does nothing after.
     */
    struct StageSession
    {
        ScriptSystems* systems = nullptr;
        std::function<void()> playBack;
        bool live = true;
    };

    /**
     * @brief The script's systems as the engine runs them: one system host per generation, found through a
     * SystemHostLoader, started once, run with one call per phase and stopped once. The engine's rules live here: a
     * script with no host has no systems; a host that cannot be bound, listed or created fails the load; a phase the
     * mask leaves out costs nothing; a phase asked for off the main thread is skipped with one error; a failing run is
     * logged and the next phase still runs; OnDestroy runs once at Stop and nothing runs after it. What runs inside a
     * phase (the order of systems, the playback after each, a system that throws) is the host's business.
     *
     * The stage rules live here too: a stage system naming a stage no extension declares fails the load, naming the
     * system and the stage; once started, each extension that declares stages is given the generation's groups for
     * them (sorted by stage, then order), even none; at Stop each is told to remove them before OnDestroy, and a group
     * kept past that runs nothing; a group run off the main thread is skipped with one error.
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
         * @brief The extensions that declare stages, and how a stage group's changes play back. Set before the first
         * Start; none means every stage system fails the load.
         */
        void SetStages(std::vector<StageHost> hosts, std::function<void()> playBack);

        /**
         * @brief Finds the generation's host, records the calling thread as the main thread, reads the mask and the
         * stage systems, checks every stage is declared, runs OnCreate and places the groups. Success and nothing held
         * when the script defines no systems. A failure holds nothing and is the load's failure.
         */
        [[nodiscard]] Result Start(LoadedScriptAssembly& generation);

        /**
         * @brief One transition for the phase: nothing when no host is held or the mask bit is clear; off the main
         * thread one error and nothing invoked; a failing run logged.
         */
        void RunPhase(int phase);

        /** @brief One transition for a stage group, by the script's number for it, under RunPhase's rules. */
        void RunStage(std::int32_t group);

        /** @brief Removes the groups and OnDestroy once if a host is held (a failure logged), then nothing held. */
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

        /** @brief How many stage groups have run since construction, across generations. */
        [[nodiscard]] std::size_t StageCalls() const noexcept
        {
            return m_stageCalls;
        }

      private:
        // Each stage host given its groups among `reports`, under a new session.
        void Place(const std::vector<StageSystemReport>& reports);

        // Whether the call may go into the script: off the main thread, one error naming `what`.
        [[nodiscard]] bool OnMainThread(const char* what) const;

        SystemHostLoader* m_loader;
        ILogger* m_logger;
        std::vector<StageHost> m_stageHosts;
        std::function<void()> m_playBack;

        std::unique_ptr<SystemHost> m_host;
        std::shared_ptr<StageSession> m_session; // Set while the held host's groups are placed.
        std::thread::id m_mainThread;
        std::int32_t m_phaseMask = 0; // Bit n is set when the held host has a system in phase n.
        std::size_t m_phaseCalls = 0;
        std::size_t m_stageCalls = 0;
    };
} // namespace Fenrir::Detail
