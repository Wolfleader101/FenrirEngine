#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief The input extension's state, which every input native function is given second: the record of the
     * warnings it has logged, made by Input::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirInputRegistration.cpp) declares the
     * natives, which name it.
     */
    struct InputScripting
    {
        /**
         * @brief Logs `message` once for each place in the extension that notices a call doing nothing, and each
         * message, in debug builds only.
         */
        void WarnOnce(const App& app, const std::string& message,
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
            app.Logger()->Warn("[input scripting] {} (warned once)", message);
#else
            (void)app;
            (void)message;
            (void)where;
#endif
        }

        /**
         * @brief The app's input device `Device`, or null (warning once for the caller's call site) when it has no
         * input. `function` is the C# function the native is behind.
         */
        template <typename Device>
        const Device* DeviceFor(const CallContext& context, std::string_view function,
                                std::source_location where = std::source_location::current())
        {
            const Device* device = context.GetApp().TryGetResource<Device>();
            if (device == nullptr)
            {
                WarnOnce(context.GetApp(),
                         std::string(function) + ": the app has no input (Input::Install), so it reads nothing", where);
            }
            return device;
        }

        /** @brief The app's Gamepads when `gamepad` is connected, or null (warning once for the caller's call site). */
        const Gamepads* PadFor(const CallContext& context, GamepadId gamepad, std::string_view function,
                               std::source_location where = std::source_location::current())
        {
            const Gamepads* gamepads = DeviceFor<Gamepads>(context, function, where);
            if (gamepads != nullptr && !gamepads->IsConnected(gamepad))
            {
                WarnOnce(context.GetApp(),
                         std::string(function) + ": the gamepad is not connected, so it reads nothing held", where);
                return nullptr;
            }
            return gamepads;
        }

      private:
        std::mutex mutex;
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
