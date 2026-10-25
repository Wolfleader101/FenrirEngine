#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirWindow/Window.hpp"

namespace Fenrir
{
    /**
     * @brief The window extension's state, which every window native function is given second: the record of the
     * warnings it has logged, made by Window::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirWindowRegistration.cpp) declares the
     * natives, which name it.
     */
    struct WindowScripting
    {
        /**
         * @brief Logs `message` once for each place in the extension that notices a call doing nothing, and each
         * message, in debug builds only.
         */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason.
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[window scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        /**
         * @brief Whether `window` has been made in the app (so there is a Window, too); warns once for the caller's
         * call site if not. `function` is the C# function the native is behind.
         */
        bool Made(const CallContext& context, WindowId window, std::string_view function,
                  std::source_location where = std::source_location::current())
        {
            if (Window::Exists(context.GetApp(), window))
            {
                return true;
            }
            WarnOnce(context,
                     std::string(function) + ": the app has no such window (no Window installed, the window not made "
                                             "yet, or an id that names none), so it does nothing",
                     where);
            return false;
        }

      private:
        std::mutex mutex;
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
