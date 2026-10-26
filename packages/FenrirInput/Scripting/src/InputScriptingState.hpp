#pragma once

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The input extension's state, which every input native function is given second: the record of the
     * warnings it has logged, made by Input::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirInputRegistration.cpp) declares the
     * natives, which name it. Its device lookups are built on the native guard, so each warns once under the name of
     * the native function `context` is a call of.
     */
    struct InputScripting
    {
        /** @brief The app's input device `Device`, or null (warning once) when it has no input. */
        template <typename Device>
        const Device* DeviceFor(const CallContext& context)
        {
            const NativeGuard guard(context, warnings);
            return guard.Require(context.GetApp().TryGetResource<Device>(),
                                 "the app has no input (Input::Install), so it reads nothing");
        }

        /** @brief The app's Gamepads when `gamepad` is connected, or null (warning once). */
        const Gamepads* PadFor(const CallContext& context, GamepadId gamepad)
        {
            const Gamepads* gamepads = DeviceFor<Gamepads>(context);
            if (gamepads != nullptr && !gamepads->IsConnected(gamepad))
            {
                NativeGuard(context, warnings).Warn("the gamepad is not connected, so it reads nothing held");
                return nullptr;
            }
            return gamepads;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"input scripting"};
    };
} // namespace Fenrir
