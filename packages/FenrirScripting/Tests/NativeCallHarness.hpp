#pragma once

// The test seam for native functions: a call context for a named native over a HeadlessApp and a change buffer, with
// no .NET and no fixture assembly, so a native's rules (no world, stale handle, dead entity, each warning once) are
// tested by calling it from C++ and reading the app and its logger. CallContext names this type as a friend, as
// Scripting names ScriptingProbe; none of it is part of the interface the engine sees.
//
// It includes FenrirScripting's private src/ChangeBuffer.hpp, so a test executable using it adds the package's `src`
// directory to its includes, as FenrirScriptingRulesTests does
// (target_include_directories(<tests> PRIVATE "${fenrir_scripting_tests_dir}/../src")), beside
// `fenrir_scripting_tests_dir` for this header and HeadlessApp.hpp.

#include <string_view>

#include "ChangeBuffer.hpp"
#include "ComponentRegistry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "HeadlessApp.hpp"

namespace Fenrir::Testing
{
    /**
     * @brief A HeadlessApp and a change buffer, handing out the call context a native function would receive from the
     * registration's thunk for a named native, and playing the changes it records back as the system runner does. The
     * extension state a native takes is the test's to make (`PhysicsScripting physics;`).
     */
    class NativeCallHarness
    {
      public:
        /** @brief The app the contexts serve: its active scene is what the natives act on. */
        [[nodiscard]] App& GetApp() noexcept
        {
            return m_headless.Get();
        }

        /** @brief What the app logged: where a native's warnings land. */
        [[nodiscard]] const RecordingLogger& Logger() const noexcept
        {
            return m_headless.Logger();
        }

        /**
         * @brief The call context for a call of the native function `native`, named by its type-and-method part
         * (`PhysicsNative.Raycast`), as the thunk builds it. `native` must outlive the context: a literal does.
         */
        [[nodiscard]] CallContext Context(std::string_view native) noexcept
        {
            return CallContext(m_headless.Get(), m_changes, native);
        }

        /**
         * @brief Plays back the structural changes natives have recorded through their contexts, in order, as the
         * system runner does after the calling system. Returns how many were applied and how many skipped.
         */
        Detail::ApplyResult ApplyChanges()
        {
            return m_changes.Apply(m_headless.Get(), m_registry);
        }

        /** @brief How many changes are recorded and not yet played back. */
        [[nodiscard]] std::size_t PendingChanges() const noexcept
        {
            return m_changes.Size();
        }

      private:
        HeadlessApp m_headless;
        // Empty: a native records its changes as functions of the App, which need no registration to play back.
        Detail::ComponentRegistry m_registry;
        Detail::ChangeBuffer m_changes;
    };
} // namespace Fenrir::Testing
