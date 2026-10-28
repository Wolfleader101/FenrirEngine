#include "ScriptSystems.hpp"

#include <utility>

namespace Fenrir::Detail
{
    ScriptSystems::ScriptSystems(SystemHostLoader& loader, ILogger* logger) : m_loader(&loader), m_logger(logger)
    {
    }

    ScriptSystems::Result ScriptSystems::Start(LoadedScriptAssembly& generation)
    {
        // A host from an earlier generation is never started over: Stop is the only way it says goodbye.
        m_host.reset();
        m_phaseMask = 0;

        auto found = m_loader->Find(generation);
        if (found.status == Iris::CallStatus::NotFound)
        {
            return Result{Iris::CallStatus::Success, {}, false};
        }
        if (!found.Succeeded())
        {
            return Result{found.status, std::move(found.diagnostic), found.diagnosticTruncated};
        }
        std::unique_ptr<SystemHost> host = std::move(*found.value);
        // Iris calls, script calls and the native functions they call all assume the thread that started the host.
        m_mainThread = std::this_thread::get_id();

        const auto phases = host->PhaseMask();
        if (!phases.Succeeded())
        {
            return Result{phases.status, "The script's systems could not be listed: " + phases.diagnostic,
                          phases.diagnosticTruncated};
        }
        const auto created = host->OnCreate();
        if (!created.Succeeded())
        {
            return Result{created.status, "The script's systems could not start: " + created.diagnostic,
                          created.diagnosticTruncated};
        }
        m_phaseMask = *phases.value;
        m_host = std::move(host);
        return Result{Iris::CallStatus::Success, {}, false};
    }

    void ScriptSystems::RunPhase(int phase)
    {
        if (m_host == nullptr || (m_phaseMask & (1 << phase)) == 0)
        {
            return;
        }
        if (std::this_thread::get_id() != m_mainThread)
        {
            if (m_logger != nullptr)
            {
                m_logger->Error("Scripting: a script phase was asked to run off the main thread and was skipped.");
            }
            return;
        }
        ++m_phaseCalls;
        const auto ran = m_host->Run(phase);
        if (!ran.Succeeded() && m_logger != nullptr)
        {
            m_logger->Error("Scripting: running the script's systems failed: {}", ran.diagnostic);
        }
    }

    void ScriptSystems::Stop()
    {
        if (m_host != nullptr)
        {
            const auto destroyed = m_host->OnDestroy();
            if (!destroyed.Succeeded() && m_logger != nullptr)
            {
                m_logger->Error("Scripting: the script's systems could not shut down cleanly: {}",
                                destroyed.diagnostic);
            }
        }
        m_host.reset();
        m_phaseMask = 0;
    }
} // namespace Fenrir::Detail
