#pragma once

// The test seam for native functions: a call context for a named native over a HeadlessApp and a change buffer, with
// no .NET and no fixture assembly, so a native's rules (no world, stale handle, dead entity, each warning once) are
// tested by calling it from C++ and reading the app and its logger. CallContext names this type as a friend, as
// Scripting names ScriptingProbe; none of it is part of the interface the engine sees.
//
// The change buffer and the registry it plays changes back through are FenrirScripting's private modules, so they live
// behind Impl in NativeCallHarness.cpp, the one file in the FenrirScriptingTestSupport library that includes them; a
// test executable links the library and sees only this header.

#include <cstddef>
#include <memory>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "HeadlessApp.hpp"

namespace Fenrir::Testing
{
    /** @brief What playing the recorded changes back did: how many were applied and how many skipped. */
    struct ApplyOutcome
    {
        std::size_t applied = 0;
        std::size_t skipped = 0;
    };

    /**
     * @brief A HeadlessApp and a change buffer, handing out the call context a native function would receive from the
     * registration's thunk for a named native, and playing the changes it records back as the system runner does. The
     * extension state a native takes is the test's to make (`PhysicsScripting physics;`).
     */
    class NativeCallHarness
    {
      public:
        NativeCallHarness();
        ~NativeCallHarness();
        NativeCallHarness(const NativeCallHarness&) = delete;
        NativeCallHarness& operator=(const NativeCallHarness&) = delete;

        /** @brief The app the contexts serve: its active scene is what the natives act on. */
        [[nodiscard]] App& GetApp() noexcept;

        /** @brief What the app logged: where a native's warnings land. */
        [[nodiscard]] const RecordingLogger& Logger() const noexcept;

        /**
         * @brief The call context for a call of the native function `native`, named by its type-and-method part
         * (`PhysicsNative.Raycast`), as the thunk builds it. `native` must outlive the context: a literal does.
         */
        [[nodiscard]] CallContext Context(std::string_view native) noexcept;

        /**
         * @brief Plays back the structural changes natives have recorded through their contexts, in order, as the
         * system runner does after the calling system. Returns how many were applied and how many skipped.
         */
        ApplyOutcome ApplyChanges();

        /** @brief How many changes are recorded and not yet played back. */
        [[nodiscard]] std::size_t PendingChanges() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Testing
