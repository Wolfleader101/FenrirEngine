#include "FenrirInput/Input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "InputDispatch.hpp"

namespace Fenrir
{
    namespace Detail
    {
        InputInstallation::InputInstallation(App& owningApp) : app(&owningApp)
        {
            app->AddResource<Keyboard>(*app);
            app->AddResource<Mouse>(*app);
            app->AddResource<Gamepads>(*app);
            app->AddResource<InputFeed>();
            app->AddResource<InputClaims>();
        }

        InputInstallation::~InputInstallation()
        {
            app->RemoveResource<InputClaims>();
            app->RemoveResource<InputFeed>();
            app->RemoveResource<Gamepads>();
            app->RemoveResource<Mouse>();
            app->RemoveResource<Keyboard>();
            app->RemoveResource<InstalledInput>();
        }

        namespace
        {
            // When dispatched input happened: this frame, and the next fixed step to begin (Time::tickCount keeps the
            // last step's number until another begins).
            struct Stamp
            {
                std::uint64_t frame;
                std::uint64_t tick;
            };

            void Press(ControlState& control, Stamp stamp)
            {
                if (control.held)
                {
                    return;
                }
                control.held = true;
                control.pressFrame = stamp.frame;
                control.pressTick = stamp.tick;
            }

            void Release(ControlState& control, Stamp stamp)
            {
                if (!control.held)
                {
                    return;
                }
                control.held = false;
                control.releaseFrame = stamp.frame;
                control.releaseTick = stamp.tick;
            }

            void Apply(ControlState& control, bool pressed, Stamp stamp)
            {
                if (pressed)
                {
                    Press(control, stamp);
                }
                else
                {
                    Release(control, stamp);
                }
            }

            void Add(SummedMotion& summed, Math::Vec2 amount, Stamp stamp)
            {
                if (summed.frame != stamp.frame)
                {
                    summed.frame = stamp.frame;
                    summed.sinceFrame = Math::Vec2(0.0f, 0.0f);
                }
                if (summed.tick != stamp.tick)
                {
                    summed.tick = stamp.tick;
                    summed.sinceTick = Math::Vec2(0.0f, 0.0f);
                }
                summed.sinceFrame += amount;
                summed.sinceTick += amount;
            }

            template <typename T, std::size_t N, typename Enum>
            T* At(std::array<T, N>& controls, Enum value)
            {
                const auto index = static_cast<std::size_t>(value);
                return index < N ? &controls[index] : nullptr;
            }

            // A key going down or up, by key code or by scancode. A repeat is not a new press: it holds the key with
            // no edge.
            template <std::size_t N, typename Enum>
            void ApplyKey(std::array<ControlState, N>& keys, Enum value, const KeyEvent& event, Stamp stamp)
            {
                ControlState* key = At(keys, value);
                if (key == nullptr)
                {
                    return;
                }
                if (event.pressed && event.repeat)
                {
                    key->held = true;
                    return;
                }
                Apply(*key, event.pressed, stamp);
            }
        } // namespace

        void InputDispatch::Pass(App& app, const FedInput& input)
        {
            Keyboard& keyboard = app.GetResource<Keyboard>();
            Mouse& mouse = app.GetResource<Mouse>();
            Gamepads& gamepads = app.GetResource<Gamepads>();
            const Stamp stamp{app.GetTime().frameCount, app.GetTime().tickCount + 1};

            // Pointer input moves the mouse only from the focused window, or from any while none is focused.
            const auto fromFocused = [&keyboard](WindowId window) {
                return !keyboard.m_focusedWindow.has_value() || *keyboard.m_focusedWindow == window;
            };

            std::visit(
                [&](const auto& event) {
                    using Event = std::decay_t<decltype(event)>;
                    if constexpr (std::is_same_v<Event, KeyEvent>)
                    {
                        // A cancelled release is not a key the user touched, so the modifiers stay as they were.
                        if (!event.cancelled)
                        {
                            keyboard.m_modifiers = event.modifiers;
                        }
                        // Unknown keys are sent as events but have no state of their own.
                        if (event.key != Key::Unknown)
                        {
                            ApplyKey(keyboard.m_keys, event.key, event, stamp);
                        }
                        if (event.scancode != Scancode::Unknown)
                        {
                            ApplyKey(keyboard.m_scancodes, event.scancode, event, stamp);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseButtonEvent>)
                    {
                        if (ControlState* button = At(mouse.m_buttons, event.button))
                        {
                            Apply(*button, event.pressed, stamp);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseMotionEvent>)
                    {
                        if (fromFocused(event.window))
                        {
                            mouse.m_position = event.position;
                            Add(mouse.m_motion, event.motion, stamp);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseScrollEvent>)
                    {
                        if (fromFocused(event.window))
                        {
                            Add(mouse.m_scroll, event.notches, stamp);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, WindowFocusEvent>)
                    {
                        if (event.focused)
                        {
                            keyboard.m_focusedWindow = event.window;
                        }
                        else if (keyboard.m_focusedWindow == event.window)
                        {
                            keyboard.m_focusedWindow.reset();
                        }
                    }
                    else if constexpr (std::is_same_v<Event, GamepadConnectionEvent>)
                    {
                        const bool known = gamepads.Find(event.gamepad) != nullptr;
                        if (event.connected && !known)
                        {
                            gamepads.m_pads.push_back(Gamepads::Pad{event.gamepad, {}, {}});
                        }
                        else if (!event.connected && known)
                        {
                            std::erase_if(gamepads.m_pads,
                                          [&event](const Gamepads::Pad& pad) { return pad.id == event.gamepad; });
                        }
                    }
                    else if constexpr (std::is_same_v<Event, GamepadButtonEvent>)
                    {
                        if (Gamepads::Pad* pad = gamepads.Find(event.gamepad))
                        {
                            if (ControlState* button = At(pad->buttons, event.button))
                            {
                                Apply(*button, event.pressed, stamp);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<Event, GamepadAxisEvent>)
                    {
                        if (Gamepads::Pad* pad = gamepads.Find(event.gamepad))
                        {
                            if (float* axis = At(pad->axes, event.axis))
                            {
                                *axis = event.value;
                            }
                        }
                    }
                    // Text changes no polled state; it is only an event.

                    app.SendEvent(event);
                },
                input);
        }
    } // namespace Detail

    Input Input::Install(App& app)
    {
        if (const Detail::InstalledInput* installed = app.TryGetResource<Detail::InstalledInput>())
        {
            if (std::shared_ptr<Detail::InputInstallation> existing = installed->installation.lock())
            {
                return Input(std::move(existing));
            }
        }

        auto installation = std::make_shared<Detail::InputInstallation>(app);
        app.AddResource<Detail::InstalledInput>(Detail::InstalledInput{installation});
        app.AddSequentialSystem(SchedulePriority::PreUpdate,
                                [weak = std::weak_ptr<Detail::InputInstallation>(installation)](App& running) {
                                    if (!weak.expired())
                                    {
                                        Dispatch(running);
                                    }
                                });
        return Input(std::move(installation));
    }

    void Input::Dispatch(App& app)
    {
        // The installation removes this resource when its last owner goes, so an app whose input ended does nothing.
        if (app.TryGetResource<Detail::InstalledInput>() != nullptr)
        {
            Detail::InputDispatch::Run(app);
        }
    }

    InputRouterId Input::AddInputRouter(App& app, int priority, std::shared_ptr<InputRouter> router)
    {
        return Detail::InputDispatch::AddRouter(app, priority, std::move(router));
    }

    void Input::RemoveInputRouter(App& app, InputRouterId router)
    {
        Detail::InputDispatch::RemoveRouter(app, router);
    }

    void Input::SetInputClaims(App& app, InputRouterId router, WindowId window, const WindowInputClaims& claims)
    {
        Detail::InputDispatch::SetClaims(app, router, window, claims);
    }

    void Input::SetGamepadClaim(App& app, InputRouterId router, bool claimed)
    {
        Detail::InputDispatch::SetGamepadClaim(app, router, claimed);
    }

    Input::Input(std::shared_ptr<Detail::InputInstallation> installation) : m_installation(std::move(installation))
    {
    }

    Input::Input(Input&& other) noexcept = default;
    Input& Input::operator=(Input&& other) noexcept = default;
    Input::~Input() = default;
} // namespace Fenrir
