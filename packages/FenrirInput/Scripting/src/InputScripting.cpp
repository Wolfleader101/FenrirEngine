#include "FenrirInputScripting/InputScripting.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputEvents.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirInputScripting/InputMappings.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "ScriptInputClaims.hpp"

// How the input events and input claims look to C#: each mapping is checked against its mirror in the Fenrir.Input
// contract (Managed/Fenrir.Input) whenever a script assembly loads, so a drift fails the load and names the field.

template <>
struct Iris::StructMapping<Fenrir::KeyEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.KeyEvent";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Fenrir::KeyEvent, window, "Window"), IRIS_FIELD_AS(Fenrir::KeyEvent, key, "Key"),
        IRIS_FIELD_AS(Fenrir::KeyEvent, scancode, "Scancode"), IRIS_FIELD_AS(Fenrir::KeyEvent, modifiers, "Modifiers"),
        IRIS_FIELD_AS(Fenrir::KeyEvent, pressed, "Pressed"), IRIS_FIELD_AS(Fenrir::KeyEvent, repeat, "Repeat"),
        IRIS_FIELD_AS(Fenrir::KeyEvent, cancelled, "Cancelled"));
};

namespace Fenrir::Detail
{
    // TextEvent's codepoint is a char32_t, which Iris does not map (its managed meaning is ambiguous), so this stands
    // in for it: the same members, the codepoint as the 32-bit number C# reads. MakeEvent checks the size and
    // alignment; the tests read real text events through C#.
    struct TextEventLayout
    {
        WindowId window;
        std::uint32_t codepoint;
    };
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::TextEventLayout>
{
    using Layout = Fenrir::Detail::TextEventLayout;
    static constexpr std::string_view typeName = "Fenrir.Input.TextEvent";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, window, "Window"), IRIS_FIELD_AS(Layout, codepoint, "Codepoint"));
};

template <>
struct Iris::StructMapping<Fenrir::MouseButtonEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.MouseButtonEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MouseButtonEvent, window, "Window"),
                                                      IRIS_FIELD_AS(Fenrir::MouseButtonEvent, button, "Button"),
                                                      IRIS_FIELD_AS(Fenrir::MouseButtonEvent, pressed, "Pressed"),
                                                      IRIS_FIELD_AS(Fenrir::MouseButtonEvent, cancelled, "Cancelled"));
};

template <>
struct Iris::StructMapping<Fenrir::MouseMotionEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.MouseMotionEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MouseMotionEvent, window, "Window"),
                                                      IRIS_FIELD_AS(Fenrir::MouseMotionEvent, position, "Position"),
                                                      IRIS_FIELD_AS(Fenrir::MouseMotionEvent, motion, "Motion"));
};

template <>
struct Iris::StructMapping<Fenrir::MouseScrollEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.MouseScrollEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MouseScrollEvent, window, "Window"),
                                                      IRIS_FIELD_AS(Fenrir::MouseScrollEvent, notches, "Notches"));
};

template <>
struct Iris::StructMapping<Fenrir::WindowFocusEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.WindowFocusEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowFocusEvent, window, "Window"),
                                                      IRIS_FIELD_AS(Fenrir::WindowFocusEvent, focused, "Focused"));
};

template <>
struct Iris::StructMapping<Fenrir::GamepadConnectionEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadConnectionEvent";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::GamepadConnectionEvent, gamepad, "Gamepad"),
                           IRIS_FIELD_AS(Fenrir::GamepadConnectionEvent, connected, "Connected"));
};

template <>
struct Iris::StructMapping<Fenrir::GamepadButtonEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadButtonEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::GamepadButtonEvent, gamepad, "Gamepad"),
                                                      IRIS_FIELD_AS(Fenrir::GamepadButtonEvent, button, "Button"),
                                                      IRIS_FIELD_AS(Fenrir::GamepadButtonEvent, pressed, "Pressed"));
};

template <>
struct Iris::StructMapping<Fenrir::GamepadAxisEvent>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadAxisEvent";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::GamepadAxisEvent, gamepad, "Gamepad"),
                                                      IRIS_FIELD_AS(Fenrir::GamepadAxisEvent, axis, "Axis"),
                                                      IRIS_FIELD_AS(Fenrir::GamepadAxisEvent, value, "Value"));
};

template <>
struct Iris::StructMapping<Fenrir::CaretRect>
{
    static constexpr std::string_view typeName = "Fenrir.Input.CaretRect";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::CaretRect, position, "Position"),
                                                      IRIS_FIELD_AS(Fenrir::CaretRect, size, "Size"));
};

template <>
struct Iris::StructMapping<Fenrir::WindowInputClaims>
{
    static constexpr std::string_view typeName = "Fenrir.Input.WindowInputClaims";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowInputClaims, pointer, "Pointer"),
                           IRIS_FIELD_AS(Fenrir::WindowInputClaims, pointerCapture, "PointerCapture"),
                           IRIS_FIELD_AS(Fenrir::WindowInputClaims, keyboardFocus, "KeyboardFocus"),
                           IRIS_FIELD_AS(Fenrir::WindowInputClaims, textInput, "TextInput"),
                           IRIS_FIELD_AS(Fenrir::WindowInputClaims, caret, "Caret"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ScriptWindowClaims>
{
    using Script = Fenrir::Detail::ScriptWindowClaims;
    static constexpr std::string_view typeName = "Fenrir.Input.WindowClaimsEntry";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Script, window, "Window"), IRIS_FIELD_AS(Script, claims, "Claims"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ScriptInputClaims>
{
    using Script = Fenrir::Detail::ScriptInputClaims;
    static constexpr std::string_view typeName = "Fenrir.Input.InputClaims";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Script, windows, "windows"), IRIS_FIELD_AS(Script, windowCount, "windowCount"),
                           IRIS_FIELD_AS(Script, gamepad, "gamepad"));
};

namespace Fenrir
{
    namespace
    {
        // Copies `claims` into `copy`: the first MaxWindows windows. True if some were left out.
        bool CopyInputClaims(const InputClaims& claims, Detail::ScriptInputClaims& copy)
        {
            const std::vector<WindowId> windows = claims.Windows();
            const std::size_t count = std::min(windows.size(), copy.windows.size());
            copy = Detail::ScriptInputClaims{};
            for (std::size_t index = 0; index < count; ++index)
            {
                copy.windows[index] = Detail::ScriptWindowClaims{windows[index], claims.For(windows[index])};
            }
            copy.windowCount = static_cast<std::uint32_t>(count);
            copy.gamepad = claims.Gamepad();
            return windows.size() > count;
        }

        // A warning logged once for each place in this file that notices a call doing nothing, in debug builds only.
        void WarnOnce(const App& app, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason.
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
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

        // The app's input device `Device`, or null (warning once for the caller's call site) when it has no input.
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

        bool KeyIsDown(const CallContext& context, std::uint16_t key)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.IsDown");
            return keyboard != nullptr && keyboard->IsDown(static_cast<Key>(key));
        }

        bool ScancodeIsDown(const CallContext& context, std::uint16_t scancode)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.IsDown");
            return keyboard != nullptr && keyboard->IsDown(static_cast<Scancode>(scancode));
        }

        bool KeyJustPressed(const CallContext& context, std::uint16_t key)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.JustPressed");
            return keyboard != nullptr && keyboard->JustPressed(static_cast<Key>(key));
        }

        bool ScancodeJustPressed(const CallContext& context, std::uint16_t scancode)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.JustPressed");
            return keyboard != nullptr && keyboard->JustPressed(static_cast<Scancode>(scancode));
        }

        bool KeyJustReleased(const CallContext& context, std::uint16_t key)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.JustReleased");
            return keyboard != nullptr && keyboard->JustReleased(static_cast<Key>(key));
        }

        bool ScancodeJustReleased(const CallContext& context, std::uint16_t scancode)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.JustReleased");
            return keyboard != nullptr && keyboard->JustReleased(static_cast<Scancode>(scancode));
        }

        std::uint16_t KeyModifiersOf(const CallContext& context)
        {
            const Keyboard* keyboard = DeviceFor<Keyboard>(context, "Keyboard.Modifiers");
            return keyboard == nullptr ? 0 : static_cast<std::uint16_t>(keyboard->Modifiers());
        }

        bool MouseIsDown(const CallContext& context, std::uint8_t button)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.IsDown");
            return mouse != nullptr && mouse->IsDown(static_cast<MouseButton>(button));
        }

        bool MouseJustPressed(const CallContext& context, std::uint8_t button)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.JustPressed");
            return mouse != nullptr && mouse->JustPressed(static_cast<MouseButton>(button));
        }

        bool MouseJustReleased(const CallContext& context, std::uint8_t button)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.JustReleased");
            return mouse != nullptr && mouse->JustReleased(static_cast<MouseButton>(button));
        }

        void MousePosition(const CallContext& context, Math::Vec2* position)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.Position");
            if (position != nullptr)
            {
                *position = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Position();
            }
        }

        void MouseMotion(const CallContext& context, Math::Vec2* motion)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.Motion");
            if (motion != nullptr)
            {
                *motion = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Motion();
            }
        }

        void MouseScroll(const CallContext& context, Math::Vec2* scroll)
        {
            const Mouse* mouse = DeviceFor<Mouse>(context, "Mouse.Scroll");
            if (scroll != nullptr)
            {
                *scroll = mouse == nullptr ? Math::Vec2(0.0f, 0.0f) : mouse->Scroll();
            }
        }

        std::int32_t GamepadsConnected(const CallContext& context, std::uint32_t* pads, std::int32_t capacity)
        {
            const Gamepads* gamepads = DeviceFor<Gamepads>(context, "Gamepads.Connected");
            if (gamepads == nullptr)
            {
                return 0;
            }
            const std::vector<GamepadId> connected = gamepads->Connected();
            const std::size_t room = pads == nullptr || capacity < 0 ? 0 : static_cast<std::size_t>(capacity);
            const auto written = static_cast<std::ptrdiff_t>(std::min(room, connected.size()));
            std::transform(connected.begin(), connected.begin() + written, pads,
                           [](GamepadId pad) { return pad.value; });
            return static_cast<std::int32_t>(connected.size());
        }

        bool GamepadIsConnected(const CallContext& context, std::uint32_t gamepad)
        {
            const Gamepads* gamepads = DeviceFor<Gamepads>(context, "Gamepads.IsConnected");
            return gamepads != nullptr && gamepads->IsConnected(GamepadId{gamepad});
        }

        // The app's Gamepads when `gamepad` is connected, or null (warning once for the caller's call site).
        const Gamepads* PadFor(const CallContext& context, std::uint32_t gamepad, std::string_view function,
                               std::source_location where = std::source_location::current())
        {
            const Gamepads* gamepads = DeviceFor<Gamepads>(context, function, where);
            if (gamepads != nullptr && !gamepads->IsConnected(GamepadId{gamepad}))
            {
                WarnOnce(context.GetApp(),
                         std::string(function) + ": the gamepad is not connected, so it reads nothing held", where);
                return nullptr;
            }
            return gamepads;
        }

        bool GamepadIsDown(const CallContext& context, std::uint32_t gamepad, std::uint8_t button)
        {
            const Gamepads* gamepads = PadFor(context, gamepad, "Gamepads.IsDown");
            return gamepads != nullptr && gamepads->IsDown(GamepadId{gamepad}, static_cast<GamepadButton>(button));
        }

        bool GamepadJustPressed(const CallContext& context, std::uint32_t gamepad, std::uint8_t button)
        {
            const Gamepads* gamepads = PadFor(context, gamepad, "Gamepads.JustPressed");
            return gamepads != nullptr && gamepads->JustPressed(GamepadId{gamepad}, static_cast<GamepadButton>(button));
        }

        bool GamepadJustReleased(const CallContext& context, std::uint32_t gamepad, std::uint8_t button)
        {
            const Gamepads* gamepads = PadFor(context, gamepad, "Gamepads.JustReleased");
            return gamepads != nullptr &&
                   gamepads->JustReleased(GamepadId{gamepad}, static_cast<GamepadButton>(button));
        }

        float GamepadAxisOf(const CallContext& context, std::uint32_t gamepad, std::uint8_t axis)
        {
            const Gamepads* gamepads = PadFor(context, gamepad, "Gamepads.Axis");
            return gamepads == nullptr ? 0.0f : gamepads->Axis(GamepadId{gamepad}, static_cast<GamepadAxis>(axis));
        }

        // InputClaims holds its windows in a vector, which C# cannot read in place, so C# reads a plain-data copy of
        // it: an app resource of this library's, refreshed from InputClaims each time C# gets it. Scripts run on the
        // thread that runs the App, in sequential systems, and only FenrirInput's sequential dispatch and router calls
        // change the claims, so the refresh never races a reader.
        ResourceRegistration InputClaimsResource()
        {
            using Detail::ScriptInputClaims;
            ResourceRegistration registration;
            registration.name = "Fenrir.Input.InputClaims";
            registration.typeName = std::string(Iris::StructMapping<ScriptInputClaims>::typeName);
            registration.scope = ResourceScope::App;
            registration.readOnly = true;
            registration.size = sizeof(ScriptInputClaims);
            registration.find = [](App& app) -> void* {
                const InputClaims* claims = app.TryGetResource<InputClaims>();
                if (claims == nullptr)
                {
                    return nullptr;
                }
                ScriptInputClaims* copy = app.TryGetResource<ScriptInputClaims>();
                if (copy == nullptr)
                {
                    copy = &app.AddResource<ScriptInputClaims>();
                }
                if (CopyInputClaims(*claims, *copy))
                {
                    WarnOnce(app, "InputClaims: routers claim input in more windows than C# can read (" +
                                      std::to_string(Detail::ScriptInputClaims::MaxWindows) +
                                      "), so the rest are left out");
                }
                return copy;
            };
            // Scripts cannot insert or remove a read-only resource, so these are never called.
            registration.insert = [](App&) -> void* { return nullptr; };
            registration.remove = [](App&) {};
            registration.validate = [](const Iris::Component& component) {
                return component.ValidateStruct<ScriptInputClaims>();
            };
            return registration;
        }
    } // namespace

    Fenrir::ScriptingExtension Input::ScriptingExtension()
    {
        const std::string prefix = "Fenrir.Input.InputNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirInput";
        extension.sharedAssemblies = {"Fenrir.Input.dll"};
        extension.functions = {
            NativeFunction::Make<&KeyIsDown>(prefix + "KeyIsDown"),
            NativeFunction::Make<&ScancodeIsDown>(prefix + "ScancodeIsDown"),
            NativeFunction::Make<&KeyJustPressed>(prefix + "KeyJustPressed"),
            NativeFunction::Make<&ScancodeJustPressed>(prefix + "ScancodeJustPressed"),
            NativeFunction::Make<&KeyJustReleased>(prefix + "KeyJustReleased"),
            NativeFunction::Make<&ScancodeJustReleased>(prefix + "ScancodeJustReleased"),
            NativeFunction::Make<&KeyModifiersOf>(prefix + "KeyModifiers"),
            NativeFunction::Make<&MouseIsDown>(prefix + "MouseIsDown"),
            NativeFunction::Make<&MouseJustPressed>(prefix + "MouseJustPressed"),
            NativeFunction::Make<&MouseJustReleased>(prefix + "MouseJustReleased"),
            NativeFunction::Make<&MousePosition>(prefix + "MousePosition"),
            NativeFunction::Make<&MouseMotion>(prefix + "MouseMotion"),
            NativeFunction::Make<&MouseScroll>(prefix + "MouseScroll"),
            NativeFunction::Make<&GamepadsConnected>(prefix + "GamepadsConnected"),
            NativeFunction::Make<&GamepadIsConnected>(prefix + "GamepadIsConnected"),
            NativeFunction::Make<&GamepadIsDown>(prefix + "GamepadIsDown"),
            NativeFunction::Make<&GamepadJustPressed>(prefix + "GamepadJustPressed"),
            NativeFunction::Make<&GamepadJustReleased>(prefix + "GamepadJustReleased"),
            NativeFunction::Make<&GamepadAxisOf>(prefix + "GamepadAxis"),
        };
        extension.events = {
            MakeEvent<KeyEvent>("Fenrir.Input.KeyEvent"),
            MakeEvent<TextEvent, Detail::TextEventLayout>("Fenrir.Input.TextEvent"),
            MakeEvent<MouseButtonEvent>("Fenrir.Input.MouseButtonEvent"),
            MakeEvent<MouseMotionEvent>("Fenrir.Input.MouseMotionEvent"),
            MakeEvent<MouseScrollEvent>("Fenrir.Input.MouseScrollEvent"),
            MakeEvent<WindowFocusEvent>("Fenrir.Input.WindowFocusEvent"),
            MakeEvent<GamepadConnectionEvent>("Fenrir.Input.GamepadConnectionEvent"),
            MakeEvent<GamepadButtonEvent>("Fenrir.Input.GamepadButtonEvent"),
            MakeEvent<GamepadAxisEvent>("Fenrir.Input.GamepadAxisEvent"),
        };
        extension.resources = {InputClaimsResource()};
        return extension;
    }
} // namespace Fenrir
