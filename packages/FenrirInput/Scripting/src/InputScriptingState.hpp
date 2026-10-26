#pragma once

#include <source_location>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

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
                warnings.Warn(context,
                              std::string(function) + ": the app has no input (Input::Install), so it reads nothing",
                              where);
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
                warnings.Warn(
                    context, std::string(function) + ": the gamepad is not connected, so it reads nothing held", where);
                return nullptr;
            }
            return gamepads;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"input scripting"};
    };
} // namespace Fenrir
