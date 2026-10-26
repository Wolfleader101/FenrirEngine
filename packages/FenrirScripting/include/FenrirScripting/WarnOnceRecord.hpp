#pragma once

// What an extension's native functions have warned about, so that each place warns once (ADR-0043): one record per
// extension, kept in its extension state and reached through it, never a process global. Every extension's state holds
// one as a public `warnings`, and its natives call Warn on it with their call context.

#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief The warnings an extension's native functions have logged, so that each is logged once: once per Scripting
     * given the extension for each place in the extension's native code that notices a call doing nothing (its
     * std::source_location) and each message, however many C# call sites or scripts reach it. Logs in debug builds
     * only. Natives may run on several threads at once, so it locks.
     */
    class WarnOnceRecord
    {
      public:
        /** @brief A record whose warnings are prefixed `[<extension>]`: "assets scripting", say. */
        explicit WarnOnceRecord(std::string_view extension) noexcept : m_extension(extension)
        {
        }

        /**
         * @brief Logs "[<extension>] <message> (warned once)" to `app`'s logger, unless this record has logged
         * `message` for `where` before.
         */
        void Warn(const App& app, const std::string& message,
                  std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason.
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            app.Logger()->Warn("[{}] {} (warned once)", m_extension, message);
#else
            (void)app;
            (void)message;
            (void)where;
#endif
        }

        /**
         * @brief Warn for the native call `context` is: logs "[<extension>] <native>: <message> (warned once)" to its
         * app's logger, with the native function's name from the context (CallContext::Native), so a native function
         * never spells its own.
         */
        void Warn(const CallContext& context, const std::string& message,
                  std::source_location where = std::source_location::current())
        {
            Warn(context.GetApp(), std::string(context.Native()) + ": " + message, where);
        }

      private:
        std::string_view m_extension;
        std::mutex m_mutex;
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> m_warned;
    };
} // namespace Fenrir
