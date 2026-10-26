#pragma once

#include <source_location>

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
     * natives, which name it.
     */
    struct WindowScripting
    {
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
            NativeGuard(context, warnings)
                .Warn("the app has no such window (no Window installed, the window not made yet, or an id that names "
                      "none), so it does nothing",
                      where);
            return false;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"window scripting"};
    };
} // namespace Fenrir
