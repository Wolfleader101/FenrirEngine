#include "FenrirInputScripting/InputScripting.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "InputClaimsResource.hpp"
#include "InputScriptingState.hpp"

// Everything C# sees of input is generated (api/FenrirInput.binding.toml): the events' and shared structs' mappings,
// checked against the Fenrir.Input contract whenever a script assembly loads, and from the native functions below
// (described into api/FenrirInput.natives.json) their C# declarations, InputNative, and this registration of them all.
// The one hand-written registration is InputClaims's (InputClaimsResource.cpp), which is refreshed whenever C# gets it.
namespace Fenrir::Generated
{
    void RegisterInput(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    // The native functions behind Fenrir.Input's Keyboard, Mouse and Gamepads. tools/describe-api reads them from this
    // file, in this order, and Iris.Bindings generates their C# declarations (Fenrir.Input.InputNative) and their
    // registration from that. Each is its one call behind the native guard (ADR-0045): with no input in the app (or a
    // pad that is not connected) it does nothing and returns false or zero, warning once per native function in a
    // debug build under the native's name, which the call context carries; its `out` parameter is zeroed by the
    // registration before it runs, so it writes nothing on that path. Not in an anonymous namespace: the generated
    // registration names them from another file.
    namespace InputNatives
    {
        /**
         * @brief Whether the key named `key` by the layout is held.
         * @facade Keyboard.IsDown
         */
        bool KeyIsDown(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->IsDown(key);
        }

        /**
         * @brief Whether the key at `scancode` is held.
         * @facade Keyboard.IsDown
         */
        bool ScancodeIsDown(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->IsDown(scancode);
        }

        /**
         * @brief Whether `key` went down since the last tick or frame. Repeats are not presses.
         * @facade Keyboard.JustPressed
         */
        bool KeyJustPressed(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->JustPressed(key);
        }

        /**
         * @brief Whether the key at `scancode` went down since the last tick or frame.
         * @facade Keyboard.JustPressed
         */
        bool ScancodeJustPressed(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->JustPressed(scancode);
        }

        /**
         * @brief Whether `key` went up since the last tick or frame.
         * @facade Keyboard.JustReleased
         */
        bool KeyJustReleased(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->JustReleased(key);
        }

        /**
         * @brief Whether the key at `scancode` went up since the last tick or frame.
         * @facade Keyboard.JustReleased
         */
        bool ScancodeJustReleased(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard != nullptr && keyboard->JustReleased(scancode);
        }

        /** @brief The modifiers of the latest key that went down or up. */
        KeyModifiers KeyModifiersOf(const CallContext& context, InputScripting& input)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context);
            return keyboard == nullptr ? KeyModifiers::None : keyboard->Modifiers();
        }

        /**
         * @brief Whether `button` is held.
         * @facade Mouse.IsDown
         */
        bool MouseIsDown(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            return mouse != nullptr && mouse->IsDown(button);
        }

        /**
         * @brief Whether `button` went down since the last tick or frame.
         * @facade Mouse.JustPressed
         */
        bool MouseJustPressed(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            return mouse != nullptr && mouse->JustPressed(button);
        }

        /**
         * @brief Whether `button` went up since the last tick or frame.
         * @facade Mouse.JustReleased
         */
        bool MouseJustReleased(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            return mouse != nullptr && mouse->JustReleased(button);
        }

        /**
         * @brief Where the pointer last was.
         * @param[out] position The position; zero with no input.
         */
        void MousePosition(const CallContext& context, InputScripting& input, Math::Vec2* position)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            if (mouse != nullptr)
            {
                *position = mouse->Position();
            }
        }

        /**
         * @brief How far the pointer moved since the last tick or frame.
         * @param[out] motion The motion; zero with no input.
         */
        void MouseMotion(const CallContext& context, InputScripting& input, Math::Vec2* motion)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            if (mouse != nullptr)
            {
                *motion = mouse->Motion();
            }
        }

        /**
         * @brief How far the wheel turned since the last tick or frame, in notches.
         * @param[out] scroll The scroll; zero with no input.
         */
        void MouseScroll(const CallContext& context, InputScripting& input, Math::Vec2* scroll)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context);
            if (mouse != nullptr)
            {
                *scroll = mouse->Scroll();
            }
        }

        /**
         * @brief Writes the pads connected now, oldest connection first, to `pads`, and returns how many are connected.
         * When more are connected than fit, the oldest are written.
         * @param pads Where to write at most `capacity` pads.
         * @facade Gamepads.Connected
         */
        std::int32_t GamepadsConnected(const CallContext& context, InputScripting& input, GamepadId* pads,
                                       std::int32_t capacity)
        {
            const Gamepads* gamepads = input.DeviceFor<Gamepads>(context);
            if (gamepads == nullptr)
            {
                return 0;
            }
            const std::vector<GamepadId> connected = gamepads->Connected();
            const std::span<GamepadId> room = SpanOf(pads, capacity);
            std::copy_n(connected.begin(), std::min(room.size(), connected.size()), room.begin());
            return static_cast<std::int32_t>(connected.size());
        }

        /**
         * @brief Whether `gamepad` is connected.
         * @facade Gamepads.IsConnected
         */
        bool GamepadIsConnected(const CallContext& context, InputScripting& input, GamepadId gamepad)
        {
            const Gamepads* gamepads = input.DeviceFor<Gamepads>(context);
            return gamepads != nullptr && gamepads->IsConnected(gamepad);
        }

        /**
         * @brief Whether `button` is held on `gamepad`.
         * @facade Gamepads.IsDown
         */
        bool GamepadIsDown(const CallContext& context, InputScripting& input, GamepadId gamepad, GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad);
            return gamepads != nullptr && gamepads->IsDown(gamepad, button);
        }

        /**
         * @brief Whether `button` went down on `gamepad` since the last tick or frame.
         * @facade Gamepads.JustPressed
         */
        bool GamepadJustPressed(const CallContext& context, InputScripting& input, GamepadId gamepad,
                                GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad);
            return gamepads != nullptr && gamepads->JustPressed(gamepad, button);
        }

        /**
         * @brief Whether `button` went up on `gamepad` since the last tick or frame.
         * @facade Gamepads.JustReleased
         */
        bool GamepadJustReleased(const CallContext& context, InputScripting& input, GamepadId gamepad,
                                 GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad);
            return gamepads != nullptr && gamepads->JustReleased(gamepad, button);
        }

        /**
         * @brief The value of `axis` on `gamepad`: -1 to 1 for a stick, 0 to 1 for a trigger.
         * @facade Gamepads.Axis
         */
        float GamepadAxisOf(const CallContext& context, InputScripting& input, GamepadId gamepad, GamepadAxis axis)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad);
            return gamepads == nullptr ? 0.0f : gamepads->Axis(gamepad, axis);
        }
    } // namespace InputNatives

    Fenrir::ScriptingExtension Input::ScriptingExtension()
    {
        const auto input = std::make_shared<InputScripting>();
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirInput";
        extension.SetState(input);
        extension.sharedAssemblies = {"Fenrir.Input.dll"};
        Generated::RegisterInput(extension);
        extension.resources.push_back(InputClaimsResource(input));
        return extension;
    }
} // namespace Fenrir
