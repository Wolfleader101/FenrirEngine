#pragma once

// The rules tests' adapter at the GenerationLifecycle's loader seam: it loads nothing, decides which loads fail and
// when a released generation counts as collected, and records what the lifecycle asked of it, so each reload rule is
// checked with no .NET and no assembly.
#include "ScriptAssemblyLoader.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace RulesTest
{
    class FakeScriptAssemblyLoader final : public Fenrir::Detail::ScriptAssemblyLoader
    {
      public:
        // What a case tells the fake to do.
        std::set<int> failLoads; // The loads (counted from 1) that fail.
        int pendingWaits = 0;    // Each released generation is Pending for this many waits, then Collected.

        // What the lifecycle asked of it.
        std::vector<std::filesystem::path> loaded; // Every path given to Load, in order, failed ones included.
        std::vector<int> released; // The loads (counted from 1) whose generation was released, in order.
        int waits = 0;

        Iris::CallResult<std::unique_ptr<Fenrir::Detail::LoadedScriptAssembly>> Load(
            const std::filesystem::path& path) override
        {
            loaded.push_back(path);
            const int number = static_cast<int>(loaded.size());
            if (failLoads.contains(number))
            {
                return {Iris::CallStatus::InvalidArgument, std::nullopt,
                        "the fake loader refused load " + std::to_string(number), false};
            }
            return {Iris::CallStatus::Success, std::make_unique<Generation>(*this, number), {}, false};
        }

      private:
        struct Generation final : Fenrir::Detail::LoadedScriptAssembly
        {
            Generation(FakeScriptAssemblyLoader& owner, int number) : owner(owner), number(number)
            {
            }

            Iris::CallResult<void> Release() override
            {
                isReleased = true;
                owner.released.push_back(number);
                return {Iris::CallStatus::Success, {}, false};
            }

            Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(std::chrono::milliseconds) override
            {
                ++owner.waits;
                if (!isReleased)
                {
                    return {Iris::CallStatus::Success, Iris::ComponentUnloadState::NotRequested, {}, false};
                }
                const auto state = ++waited > owner.pendingWaits ? Iris::ComponentUnloadState::Collected
                                                                 : Iris::ComponentUnloadState::Pending;
                return {Iris::CallStatus::Success, state, {}, false};
            }

            FakeScriptAssemblyLoader& owner;
            int number;
            int waited = 0;
            bool isReleased = false;
        };
    };
} // namespace RulesTest
