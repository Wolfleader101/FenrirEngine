#include "ScriptSystems.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace Fenrir
{
    void StageGroup::Run() const
    {
        if (m_session != nullptr && m_session->live && m_session->systems != nullptr)
        {
            m_session->systems->RunStage(m_index);
        }
    }

    void StageChanges::PlayBack() const
    {
        if (m_session != nullptr && m_session->live && m_session->playBack)
        {
            m_session->playBack();
        }
    }
} // namespace Fenrir

namespace Fenrir::Detail
{
    ScriptSystems::ScriptSystems(SystemHostLoader& loader, ILogger* logger) : m_loader(&loader), m_logger(logger)
    {
    }

    void ScriptSystems::SetStages(std::vector<StageHost> hosts, std::function<void()> playBack)
    {
        m_stageHosts = std::move(hosts);
        m_playBack = std::move(playBack);
    }

    ScriptSystems::Result ScriptSystems::Start(LoadedScriptAssembly& generation)
    {
        // A host from an earlier generation is never started over: Stop is the only way it says goodbye.
        m_host.reset();
        m_session.reset();
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
        const auto stageSystems = host->StageSystems();
        if (!stageSystems.Succeeded())
        {
            return Result{stageSystems.status,
                          "The script's stage systems could not be listed: " + stageSystems.diagnostic,
                          stageSystems.diagnosticTruncated};
        }
        // Every stage is declared before anything of the script runs, as a native function mismatch is found.
        for (const auto& report : *stageSystems.value)
        {
            const bool declared = std::any_of(m_stageHosts.begin(), m_stageHosts.end(), [&](const StageHost& stages) {
                return std::find(stages.stages.begin(), stages.stages.end(), report.stage) != stages.stages.end();
            });
            if (!declared)
            {
                return Result{Iris::CallStatus::InvalidArgument,
                              "The script system '" + report.system + "' joins the stage '" + report.stage +
                                  "', which no loaded extension declares.",
                              false};
            }
        }
        const auto created = host->OnCreate();
        if (!created.Succeeded())
        {
            return Result{created.status, "The script's systems could not start: " + created.diagnostic,
                          created.diagnosticTruncated};
        }
        m_phaseMask = *phases.value;
        m_host = std::move(host);
        Place(*stageSystems.value);
        return Result{Iris::CallStatus::Success, {}, false};
    }

    void ScriptSystems::Place(const std::vector<StageSystemReport>& reports)
    {
        m_session = std::make_shared<StageSession>();
        m_session->systems = this;
        m_session->playBack = m_playBack;
        for (const auto& stages : m_stageHosts)
        {
            // One group per number the script gave; its systems all share its stage and order.
            std::vector<const StageSystemReport*> firsts;
            for (const auto& report : reports)
            {
                const bool hosted =
                    std::find(stages.stages.begin(), stages.stages.end(), report.stage) != stages.stages.end();
                const bool seen = std::any_of(firsts.begin(), firsts.end(), [&](const StageSystemReport* first) {
                    return first->group == report.group;
                });
                if (hosted && !seen)
                {
                    firsts.push_back(&report);
                }
            }
            std::sort(firsts.begin(), firsts.end(), [](const StageSystemReport* left, const StageSystemReport* right) {
                return left->stage != right->stage ? left->stage < right->stage : left->order < right->order;
            });
            std::vector<StageGroup> groups;
            groups.reserve(firsts.size());
            for (const auto* first : firsts)
            {
                groups.push_back(StageGroup(m_session, first->stage, first->order, first->group));
            }
            try
            {
                stages.place(std::move(groups), StageChanges(m_session));
            }
            catch (const std::exception& exception)
            {
                if (m_logger != nullptr)
                {
                    m_logger->Error("Scripting: extension '{}' could not place its stage groups: {}", stages.extension,
                                    exception.what());
                }
            }
        }
    }

    bool ScriptSystems::OnMainThread(const char* what) const
    {
        if (std::this_thread::get_id() == m_mainThread)
        {
            return true;
        }
        if (m_logger != nullptr)
        {
            m_logger->Error("Scripting: a script {} was asked to run off the main thread and was skipped.", what);
        }
        return false;
    }

    void ScriptSystems::RunPhase(int phase)
    {
        if (m_host == nullptr || (m_phaseMask & (1 << phase)) == 0)
        {
            return;
        }
        if (!OnMainThread("phase"))
        {
            return;
        }
        ++m_phaseCalls;
        const auto ran = m_host->Run(phase);
        if (!ran.Succeeded() && m_logger != nullptr)
        {
            m_logger->Error("Scripting: running the script's systems failed: {}", ran.diagnostic);
        }
    }

    void ScriptSystems::RunStage(std::int32_t group)
    {
        if (m_host == nullptr || m_session == nullptr)
        {
            return;
        }
        if (!OnMainThread("stage group"))
        {
            return;
        }
        ++m_stageCalls;
        const auto ran = m_host->RunStage(group);
        if (!ran.Succeeded() && m_logger != nullptr)
        {
            m_logger->Error("Scripting: running the script's stage systems failed: {}", ran.diagnostic);
        }
    }

    void ScriptSystems::Stop()
    {
        if (m_session != nullptr)
        {
            // The extensions let go of the groups first, and any they keep run nothing from now on.
            for (const auto& stages : m_stageHosts)
            {
                try
                {
                    stages.remove();
                }
                catch (const std::exception& exception)
                {
                    if (m_logger != nullptr)
                    {
                        m_logger->Error("Scripting: extension '{}' could not remove its stage groups: {}",
                                        stages.extension, exception.what());
                    }
                }
            }
            m_session->live = false;
            m_session.reset();
        }
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
