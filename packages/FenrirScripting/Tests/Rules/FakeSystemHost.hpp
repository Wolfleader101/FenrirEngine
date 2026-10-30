#pragma once

// The rules tests' adapter at ScriptSystems' loader seam: it binds nothing, is told whether a generation has a host and
// which of the host's four methods fail, and records every call in order with the thread that made it, so each rule of
// ScriptSystems is checked with no .NET and no assembly.
#include "SystemHost.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace RulesTest
{
    // A generation the fake is given and never looks at: the host is found by the knobs below, not in it.
    struct UnusedGeneration final : Fenrir::Detail::LoadedScriptAssembly
    {
        Iris::CallResult<void> Release() override
        {
            return {Iris::CallStatus::Success, {}, false};
        }

        Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(std::chrono::milliseconds) override
        {
            return {Iris::CallStatus::Success, Iris::ComponentUnloadState::Collected, {}, false};
        }
    };

    class FakeSystemHostLoader final : public Fenrir::Detail::SystemHostLoader
    {
      public:
        // What Find answers.
        enum class Answer
        {
            NoHost,  // NotFound: the script defines no systems.
            Failure, // The host could not be bound.
            Host     // A host, with the knobs below.
        };

        // What a case tells the fake to do.
        Answer answer = Answer::Host;
        std::int32_t mask = 0;                                       // What the host's PhaseMask answers.
        std::vector<Fenrir::Detail::StageSystemReport> stageSystems; // What its StageSystems answers.
        bool failPhaseMask = false;
        bool failStageSystems = false;
        bool failOnCreate = false;
        bool failRun = false;
        bool failRunStage = false;
        bool failOnDestroy = false;

        // What ScriptSystems asked of it, in order: "Find", "PhaseMask", "StageSystems", "OnCreate", "Run <phase>",
        // "RunStage <group>", "OnDestroy".
        struct Call
        {
            std::string name;
            std::thread::id thread;
        };
        std::vector<Call> calls;

        [[nodiscard]] std::vector<std::string> Names() const
        {
            std::vector<std::string> names;
            for (const auto& call : calls)
            {
                names.push_back(call.name);
            }
            return names;
        }

        Iris::CallResult<std::unique_ptr<Fenrir::Detail::SystemHost>> Find(
            Fenrir::Detail::LoadedScriptAssembly&) override
        {
            Record("Find");
            switch (answer)
            {
            case Answer::NoHost:
                return {Iris::CallStatus::NotFound, std::nullopt, {}, false};
            case Answer::Failure:
                return {Iris::CallStatus::InvalidArgument, std::nullopt, "the fake loader refused the host", false};
            case Answer::Host:
                break;
            }
            return {Iris::CallStatus::Success, std::make_unique<Host>(*this), {}, false};
        }

      private:
        void Record(std::string name)
        {
            calls.push_back({std::move(name), std::this_thread::get_id()});
        }

        template <typename T>
        [[nodiscard]] static Iris::CallResult<T> Refused(const char* method)
        {
            return {Iris::CallStatus::ManagedException, std::nullopt, std::string("the fake host refused ") + method,
                    false};
        }

        [[nodiscard]] static Iris::CallResult<void> RefusedVoid(const char* method)
        {
            return {Iris::CallStatus::ManagedException, std::string("the fake host refused ") + method, false};
        }

        struct Host final : Fenrir::Detail::SystemHost
        {
            explicit Host(FakeSystemHostLoader& owner) : owner(owner)
            {
            }

            Iris::CallResult<std::int32_t> PhaseMask() override
            {
                owner.Record("PhaseMask");
                if (owner.failPhaseMask)
                {
                    return Refused<std::int32_t>("PhaseMask");
                }
                return {Iris::CallStatus::Success, owner.mask, {}, false};
            }

            Iris::CallResult<std::vector<Fenrir::Detail::StageSystemReport>> StageSystems() override
            {
                owner.Record("StageSystems");
                if (owner.failStageSystems)
                {
                    return Refused<std::vector<Fenrir::Detail::StageSystemReport>>("StageSystems");
                }
                return {Iris::CallStatus::Success, owner.stageSystems, {}, false};
            }

            Iris::CallResult<void> RunStage(std::int32_t group) override
            {
                owner.Record("RunStage " + std::to_string(group));
                return owner.failRunStage ? RefusedVoid("RunStage")
                                          : Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }

            Iris::CallResult<void> OnCreate() override
            {
                owner.Record("OnCreate");
                return owner.failOnCreate ? RefusedVoid("OnCreate")
                                          : Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }

            Iris::CallResult<void> Run(std::int32_t phase) override
            {
                owner.Record("Run " + std::to_string(phase));
                return owner.failRun ? RefusedVoid("Run")
                                     : Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }

            Iris::CallResult<void> OnDestroy() override
            {
                owner.Record("OnDestroy");
                return owner.failOnDestroy ? RefusedVoid("OnDestroy")
                                           : Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }

            FakeSystemHostLoader& owner;
        };
    };
} // namespace RulesTest
