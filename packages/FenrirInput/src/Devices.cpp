#include "FenrirInput/Devices.hpp"

#include <algorithm>
#include <cstddef>

#include "FenrirApp/App.hpp"

namespace Fenrir
{
    namespace Detail
    {
        InputMoment InputMoment::Of(const App& app)
        {
            const std::optional<SchedulePriority> phase = app.CurrentPhase();
            const bool inTick = phase == SchedulePriority::PreTick || phase == SchedulePriority::Tick ||
                                phase == SchedulePriority::PostTick;
            return {inTick, inTick ? app.GetTime().tickCount : app.GetTime().frameCount};
        }

        namespace
        {
            // Stamps start at one, so zero (nothing yet, or a moment before the first frame or tick) never matches.
            bool At(InputMoment moment, std::uint64_t frame, std::uint64_t tick) noexcept
            {
                const std::uint64_t stamp = moment.inTick ? tick : frame;
                return stamp != 0 && stamp == moment.number;
            }
        } // namespace

        bool ControlState::JustPressed(InputMoment moment) const noexcept
        {
            return At(moment, pressFrame, pressTick);
        }

        bool ControlState::JustReleased(InputMoment moment) const noexcept
        {
            return At(moment, releaseFrame, releaseTick);
        }

        Math::Vec2 SummedMotion::Read(InputMoment moment) const noexcept
        {
            if (!At(moment, frame, tick))
            {
                return Math::Vec2(0.0f, 0.0f);
            }
            return moment.inTick ? sinceTick : sinceFrame;
        }
    } // namespace Detail

    namespace
    {
        template <std::size_t N, typename Enum>
        bool IsHeld(const std::array<Detail::ControlState, N>& controls, Enum value)
        {
            const auto index = static_cast<std::size_t>(value);
            return index < N && controls[index].held;
        }

        template <std::size_t N, typename Enum>
        bool Pressed(const App& app, const std::array<Detail::ControlState, N>& controls, Enum value)
        {
            const auto index = static_cast<std::size_t>(value);
            return index < N && controls[index].JustPressed(Detail::InputMoment::Of(app));
        }

        template <std::size_t N, typename Enum>
        bool Released(const App& app, const std::array<Detail::ControlState, N>& controls, Enum value)
        {
            const auto index = static_cast<std::size_t>(value);
            return index < N && controls[index].JustReleased(Detail::InputMoment::Of(app));
        }
    } // namespace

    Keyboard::Keyboard(const App& app) : m_app(&app)
    {
    }

    bool Keyboard::IsDown(Key key) const
    {
        return IsHeld(m_keys, key);
    }

    bool Keyboard::IsDown(Scancode scancode) const
    {
        return IsHeld(m_scancodes, scancode);
    }

    bool Keyboard::JustPressed(Key key) const
    {
        return Pressed(*m_app, m_keys, key);
    }

    bool Keyboard::JustPressed(Scancode scancode) const
    {
        return Pressed(*m_app, m_scancodes, scancode);
    }

    bool Keyboard::JustReleased(Key key) const
    {
        return Released(*m_app, m_keys, key);
    }

    bool Keyboard::JustReleased(Scancode scancode) const
    {
        return Released(*m_app, m_scancodes, scancode);
    }

    Mouse::Mouse(const App& app) : m_app(&app)
    {
    }

    bool Mouse::IsDown(MouseButton button) const
    {
        return IsHeld(m_buttons, button);
    }

    bool Mouse::JustPressed(MouseButton button) const
    {
        return Pressed(*m_app, m_buttons, button);
    }

    bool Mouse::JustReleased(MouseButton button) const
    {
        return Released(*m_app, m_buttons, button);
    }

    Math::Vec2 Mouse::Motion() const
    {
        return m_motion.Read(Detail::InputMoment::Of(*m_app));
    }

    Math::Vec2 Mouse::Scroll() const
    {
        return m_scroll.Read(Detail::InputMoment::Of(*m_app));
    }

    Gamepads::Gamepads(const App& app) : m_app(&app)
    {
    }

    const Gamepads::Pad* Gamepads::Find(GamepadId gamepad) const
    {
        const auto pad = std::find_if(m_pads.begin(), m_pads.end(),
                                      [gamepad](const Pad& candidate) { return candidate.id == gamepad; });
        return pad == m_pads.end() ? nullptr : &*pad;
    }

    Gamepads::Pad* Gamepads::Find(GamepadId gamepad)
    {
        return const_cast<Pad*>(static_cast<const Gamepads&>(*this).Find(gamepad));
    }

    std::vector<GamepadId> Gamepads::Connected() const
    {
        std::vector<GamepadId> ids;
        ids.reserve(m_pads.size());
        for (const Pad& pad : m_pads)
        {
            ids.push_back(pad.id);
        }
        return ids;
    }

    bool Gamepads::IsConnected(GamepadId gamepad) const
    {
        return Find(gamepad) != nullptr;
    }

    bool Gamepads::IsDown(GamepadId gamepad, GamepadButton button) const
    {
        const Pad* pad = Find(gamepad);
        return pad != nullptr && IsHeld(pad->buttons, button);
    }

    bool Gamepads::JustPressed(GamepadId gamepad, GamepadButton button) const
    {
        const Pad* pad = Find(gamepad);
        return pad != nullptr && Pressed(*m_app, pad->buttons, button);
    }

    bool Gamepads::JustReleased(GamepadId gamepad, GamepadButton button) const
    {
        const Pad* pad = Find(gamepad);
        return pad != nullptr && Released(*m_app, pad->buttons, button);
    }

    float Gamepads::Axis(GamepadId gamepad, GamepadAxis axis) const
    {
        const Pad* pad = Find(gamepad);
        const auto index = static_cast<std::size_t>(axis);
        return pad != nullptr && index < GamepadAxisCount ? pad->axes[index] : 0.0f;
    }
} // namespace Fenrir
