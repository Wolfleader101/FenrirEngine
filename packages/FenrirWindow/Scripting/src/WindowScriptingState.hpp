#pragma once

#include <source_location>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirWindow/Window.hpp"

namespace Fenrir
{
    /**
     * @brief The window extension's state, which every window native function is given second: the record of the
     * warnings it has logged, made by Window::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirWindowRegistration.cpp) declares the
     * natives, which name it. The texts the natives warn with are named here, one per rule, so the package's rules
     * read in one place and its rules tests name them.
     */
    struct WindowScripting
    {
        /** @brief Warned when `window` names no window the app has made: the native does nothing. */
        static constexpr std::string_view NoWindow = "the app has no such window (no Window installed, the window not "
                                                     "made yet, or an id that names none), so it "
                                                     "does nothing";

        /** @brief Warned by SetCursorMode when the mode is none of CursorMode's enumerators: it does nothing. */
        static constexpr std::string_view NoCursorMode = "no such cursor mode, so it does nothing";

        /** @brief Warned by SetCursorShape when the shape is none of CursorShape's enumerators: it does nothing. */
        static constexpr std::string_view NoCursorShape = "no such cursor shape, so it does nothing";

        /**
         * @brief Whether `window` has been made in the app (so there is a Window, too); warns once for the caller's
         * call site, under the native function's name, if not.
         */
        bool Made(const CallContext& context, WindowId window,
                  std::source_location where = std::source_location::current())
        {
            if (Window::Exists(context.GetApp(), window))
            {
                return true;
            }
            NativeGuard(context, warnings).Warn(std::string(NoWindow), where);
            return false;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"window scripting"};
    };
} // namespace Fenrir
