#pragma once

// The native call harness: the test adapter at the registration seam. Given scripting extensions, it registers their
// native functions as Scripting::Install does (NativeFunction::Register with the extension's state) and is the active
// Scripting while it lives (its HeadlessApp, an empty registry and its change buffer as the engine context), so a
// test calls a native by its registered name through the registration's own thunk, with no .NET and no fixture
// assembly, and reads what it returned, wrote and warned. What the registration settles (the context, the state, the
// zeroed outputs, the "not running" value) is under test, never re-spelled here. NativeFunction, ScriptingExtension
// and CallContext name this type as a friend, as Scripting names ScriptingProbe; none of it is part of the interface
// the engine sees.
//
// The change buffer, the registry it plays changes back through and the engine context are FenrirScripting's private
// modules, so they live behind Impl in NativeCallHarness.cpp, the one file in the FenrirScriptingTestSupport library
// that includes them; a test executable links the library and sees only this header.

#include <cstddef>
#include <memory>
#include <string_view>
#include <typeinfo>
#include <vector>

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
     * @brief A HeadlessApp and a change buffer run as the active Scripting, with the native functions of the
     * extensions it is given registered as Scripting::Install registers them: `Call<R>("PhysicsNative.AddImpulse",
     * &body, &up)` calls a native by its registered name through the registration's thunk, which builds the call
     * context from this harness and passes the extension's state, and `WarnedOnce` reads what it warned. `Context`
     * hands out the call context the thunk would build, for a test of what a native is written with (the guard, the
     * marshalling helpers, the zeroing template) that calls no registered native; `ApplyChanges` plays the changes
     * natives record back as the system runner does.
     *
     * One harness is active at a time, as one Scripting is: a second constructed while one lives, or while a
     * Scripting runs, throws.
     */
    class NativeCallHarness
    {
      public:
        /** @brief A harness with no native registered: `Context`, `ApplyChanges` and the app and logger. */
        NativeCallHarness();

        /**
         * @brief Registers each extension's native functions with the extension's state, as Scripting::Install does,
         * and becomes the active Scripting. Throws std::runtime_error naming the native function whose registration
         * fails (it takes a state the extension does not carry, or Iris refuses it), or when another harness or a
         * Scripting is active.
         */
        explicit NativeCallHarness(std::vector<ScriptingExtension> extensions);
        ~NativeCallHarness();
        NativeCallHarness(const NativeCallHarness&) = delete;
        NativeCallHarness& operator=(const NativeCallHarness&) = delete;

        /** @brief How many times a place warns in this build: once in a debug build, never in a release build. */
#ifdef NDEBUG
        static constexpr std::size_t once = 0;
#else
        static constexpr std::size_t once = 1;
#endif

        /** @brief The app the contexts serve: its active scene is what the natives act on. */
        [[nodiscard]] App& GetApp() noexcept;

        /** @brief What the app logged: where a native's warnings land. */
        [[nodiscard]] const RecordingLogger& Logger() const noexcept;

        /**
         * @brief Calls the registered native function `native`, named by its type-and-method part
         * (`PhysicsNative.AddImpulse`, NativeFunction::NativeNameFor), through its registration's thunk, as C# would:
         * `R` is the result type the C# method declares and `Args...` its parameter types, deduced from the arguments
         * (a literal `0` is an int and `"text"` a pointer: write `std::uint64_t{0}`, `"text"sv`, or name the types,
         * `Call<void, std::string_view>(...)`). Throws std::runtime_error naming the native when none of that name is
         * registered, or when `R(Args...)` is not the thunk's signature, naming both.
         */
        template <typename R, typename... Args>
        R Call(std::string_view native, Args... arguments)
        {
            return reinterpret_cast<R (*)(Args...)>(Thunk(native, typeid(R(Args...))))(arguments...);
        }

        /**
         * @brief How many warnings the app logged whose text ends with WarnOnceRecord::Line(native, message): what the
         * native `native` (its type-and-method part) warned `message` through its extension's record, whatever the
         * extension's prefix.
         */
        [[nodiscard]] std::size_t Warnings(std::string_view native, std::string_view message) const;

        /** @brief Whether `native` warned `message` exactly `once`: once in a debug build, never in a release build. */
        [[nodiscard]] bool WarnedOnce(std::string_view native, std::string_view message) const
        {
            return Warnings(native, message) == once;
        }

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
        // The registered native `native`'s thunk, whose signature must be `signature`: throws, naming the native,
        // when none is registered under that name or the signature is another.
        [[nodiscard]] void* Thunk(std::string_view native, const std::type_info& signature) const;

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Testing
