#include "FenrirInputScripting/InputScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
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
    // The native functions behind Fenrir.Input's Keyboard, Mouse and Gamepads, each named as C# calls it.
    // tools/describe-api reads them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Input.InputNative) and their registration from that. Each does nothing and returns false or zero with no
    // input in the app (or a pad that is not connected), warning once per call site in a debug build. Not in an
    // anonymous namespace: the generated registration names them from another file.
    namespace InputNatives
    {
        /** @brief Whether the key named `key` by the layout is held. */
        bool KeyIsDown(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.IsDown");
            return keyboard != nullptr && keyboard->IsDown(key);
        }

        /** @brief Whether the key at `scancode` is held. */
        bool ScancodeIsDown(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.IsDown");
            return keyboard != nullptr && keyboard->IsDown(scancode);
        }

        /** @brief Whether `key` went down since the last tick or frame. Repeats are not presses. */
        bool KeyJustPressed(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.JustPressed");
            return keyboard != nullptr && keyboard->JustPressed(key);
        }

        /** @brief Whether the key at `scancode` went down since the last tick or frame. */
        bool ScancodeJustPressed(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.JustPressed");
            return keyboard != nullptr && keyboard->JustPressed(scancode);
        }

        /** @brief Whether `key` went up since the last tick or frame. */
        bool KeyJustReleased(const CallContext& context, InputScripting& input, Key key)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.JustReleased");
            return keyboard != nullptr && keyboard->JustReleased(key);
        }

        /** @brief Whether the key at `scancode` went up since the last tick or frame. */
        bool ScancodeJustReleased(const CallContext& context, InputScripting& input, Scancode scancode)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.JustReleased");
            return keyboard != nullptr && keyboard->JustReleased(scancode);
        }

        /** @brief The modifiers of the latest key that went down or up. */
        KeyModifiers KeyModifiersOf(const CallContext& context, InputScripting& input)
        {
            const Keyboard* keyboard = input.DeviceFor<Keyboard>(context, "Keyboard.Modifiers");
            return keyboard == nullptr ? KeyModifiers::None : keyboard->Modifiers();
        }

        /** @brief Whether `button` is held. */
        bool MouseIsDown(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.IsDown");
            return mouse != nullptr && mouse->IsDown(button);
        }

        /** @brief Whether `button` went down since the last tick or frame. */
        bool MouseJustPressed(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.JustPressed");
            return mouse != nullptr && mouse->JustPressed(button);
        }

        /** @brief Whether `button` went up since the last tick or frame. */
        bool MouseJustReleased(const CallContext& context, InputScripting& input, MouseButton button)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.JustReleased");
            return mouse != nullptr && mouse->JustReleased(button);
        }

        /**
         * @brief Where the pointer last was.
         * @param[out] position The position; zero with no input.
         */
        void MousePosition(const CallContext& context, InputScripting& input, Math::Vec2* position)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.Position");
            if (position != nullptr)
            {
                *position = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Position();
            }
        }

        /**
         * @brief How far the pointer moved since the last tick or frame.
         * @param[out] motion The motion; zero with no input.
         */
        void MouseMotion(const CallContext& context, InputScripting& input, Math::Vec2* motion)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.Motion");
            if (motion != nullptr)
            {
                *motion = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Motion();
            }
        }

        /**
         * @brief How far the wheel turned since the last tick or frame, in notches.
         * @param[out] scroll The scroll; zero with no input.
         */
        void MouseScroll(const CallContext& context, InputScripting& input, Math::Vec2* scroll)
        {
            const Mouse* mouse = input.DeviceFor<Mouse>(context, "Mouse.Scroll");
            if (scroll != nullptr)
            {
                *scroll = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Scroll();
            }
        }

        /**
         * @brief Writes the pads connected now, oldest connection first, to `pads`, and returns how many are connected.
         * @param pads Where to write at most `capacity` pads.
         */
        std::int32_t GamepadsConnected(const CallContext& context, InputScripting& input, GamepadId* pads,
                                       std::int32_t capacity)
        {
            const Gamepads* gamepads = input.DeviceFor<Gamepads>(context, "Gamepads.Connected");
            if (gamepads == nullptr)
            {
                return 0;
            }
            const std::vector<GamepadId> connected = gamepads->Connected();
            const std::size_t room = pads == nullptr || capacity < 0 ? 0 : static_cast<std::size_t>(capacity);
            std::copy_n(connected.begin(), std::min(room, connected.size()), pads);
            return static_cast<std::int32_t>(connected.size());
        }

        /** @brief Whether `gamepad` is connected. */
        bool GamepadIsConnected(const CallContext& context, InputScripting& input, GamepadId gamepad)
        {
            const Gamepads* gamepads = input.DeviceFor<Gamepads>(context, "Gamepads.IsConnected");
            return gamepads != nullptr && gamepads->IsConnected(gamepad);
        }

        /** @brief Whether `button` is held on `gamepad`. */
        bool GamepadIsDown(const CallContext& context, InputScripting& input, GamepadId gamepad, GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad, "Gamepads.IsDown");
            return gamepads != nullptr && gamepads->IsDown(gamepad, button);
        }

        /** @brief Whether `button` went down on `gamepad` since the last tick or frame. */
        bool GamepadJustPressed(const CallContext& context, InputScripting& input, GamepadId gamepad,
                                GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad, "Gamepads.JustPressed");
            return gamepads != nullptr && gamepads->JustPressed(gamepad, button);
        }

        /** @brief Whether `button` went up on `gamepad` since the last tick or frame. */
        bool GamepadJustReleased(const CallContext& context, InputScripting& input, GamepadId gamepad,
                                 GamepadButton button)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad, "Gamepads.JustReleased");
            return gamepads != nullptr && gamepads->JustReleased(gamepad, button);
        }

        /** @brief The value of `axis` on `gamepad`: -1 to 1 for a stick, 0 to 1 for a trigger. */
        float GamepadAxisOf(const CallContext& context, InputScripting& input, GamepadId gamepad, GamepadAxis axis)
        {
            const Gamepads* gamepads = input.PadFor(context, gamepad, "Gamepads.Axis");
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
