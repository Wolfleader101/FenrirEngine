#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"

// The app resources Input::Install adds: the polled state of the keyboard, the mouse and the gamepads (ADR-0017).
// Any system may read them, in parallel or not; only FenrirInput's dispatch, in PreUpdate, writes them.
//
// Input edges follow the reading phase. Read from PreTick, Tick or PostTick, "just pressed" and "just released" are
// the edges since the last tick; read from any other phase (or outside the frame), the edges since the last frame.
// So a tap is never lost in a frame that runs no tick, nor seen twice in a frame that runs two, and a press and release
// that both came between two ticks read in the next tick as just pressed and just released, with held false. Input is
// processed in PreUpdate, after the phase's parallel systems, so read it from BeforeTicks on.

namespace Fenrir
{
    class App;

    namespace Detail
    {
        class InputDispatch;

        /**
         * @brief Which frame or fixed step a reading phase answers for: the tick's number from PreTick, Tick and
         * PostTick, else the frame's. Zero means none has begun, and no input is stamped with zero.
         */
        struct InputMoment
        {
            bool inTick = false;
            std::uint64_t number = 0;

            /** @brief The moment the caller's phase reads at. */
            [[nodiscard]] static InputMoment Of(const App& app);
        };

        /**
         * @brief One control's state (a key, a mouse button, a gamepad button): whether it is held, and the frame and
         * the fixed step of its latest press and release. Input dispatched in frame F, before step T, is stamped with
         * frame F and step T.
         */
        struct ControlState
        {
            bool held = false;
            std::uint64_t pressFrame = 0;
            std::uint64_t releaseFrame = 0;
            std::uint64_t pressTick = 0;
            std::uint64_t releaseTick = 0;

            /** @brief Whether the latest press was at `moment`'s frame or fixed step. */
            [[nodiscard]] bool JustPressed(InputMoment moment) const noexcept;
            /** @brief Whether the latest release was at `moment`'s frame or fixed step. */
            [[nodiscard]] bool JustReleased(InputMoment moment) const noexcept;
        };

        /** @brief A movement summed since the last frame and since the last fixed step (mouse motion and scroll). */
        struct SummedMotion
        {
            Math::Vec2 sinceFrame{0.0f, 0.0f};
            Math::Vec2 sinceTick{0.0f, 0.0f};
            std::uint64_t frame = 0;
            std::uint64_t tick = 0;

            /** @brief The sum since the frame or fixed step `moment` reads at began, or zero if nothing moved since. */
            [[nodiscard]] Math::Vec2 Read(InputMoment moment) const noexcept;
        };
    } // namespace Detail

    /**
     * @brief The keyboard: keys held, just pressed and just released, by key code and by scancode, which are tracked
     * separately; the modifiers; and which window has the input focus. An app resource.
     */
    class Keyboard
    {
      public:
        /** @brief Reads its input edges by the phase of `app`; made by Input::Install. */
        explicit Keyboard(const App& app);

        /** @brief Whether the key named `key` by the layout is held. */
        [[nodiscard]] bool IsDown(Key key) const;
        /** @brief Whether the key at `scancode` is held. */
        [[nodiscard]] bool IsDown(Scancode scancode) const;

        /** @brief Whether `key` went down since the last tick or frame (see the input edges above). Repeats are not
         * presses. */
        [[nodiscard]] bool JustPressed(Key key) const;
        /** @copydoc JustPressed(Key) const */
        [[nodiscard]] bool JustPressed(Scancode scancode) const;

        /** @brief Whether `key` went up since the last tick or frame (see the input edges above). */
        [[nodiscard]] bool JustReleased(Key key) const;
        /** @copydoc JustReleased(Key) const */
        [[nodiscard]] bool JustReleased(Scancode scancode) const;

        /** @brief The modifiers of the latest key that went down or up. */
        [[nodiscard]] KeyModifiers Modifiers() const noexcept
        {
            return m_modifiers;
        }

        /** @brief The window that has the input focus, or none before a window gains it or after it loses it. */
        [[nodiscard]] std::optional<WindowId> FocusedWindow() const noexcept
        {
            return m_focusedWindow;
        }

      private:
        friend class Detail::InputDispatch;

        const App* m_app;
        std::array<Detail::ControlState, KeyCount> m_keys{};
        std::array<Detail::ControlState, ScancodeCount> m_scancodes{};
        KeyModifiers m_modifiers = KeyModifiers::None;
        std::optional<WindowId> m_focusedWindow;
    };

    /**
     * @brief The mouse: buttons held, just pressed and just released; the pointer's position; and its motion and the
     * wheel's scroll. An app resource.
     *
     * Position and motion are in the focused window's logical coordinates (floats, from its top-left corner), motion
     * raw while the cursor is captured; scroll is in notches. Only input from the focused window (Keyboard's
     * FocusedWindow) moves them, or from any window while none has the focus. Motion and scroll follow the reading
     * phase as input edges do: summed since the last tick from PreTick, Tick and PostTick, else since the last frame.
     */
    class Mouse
    {
      public:
        /** @brief Reads its input edges by the phase of `app`; made by Input::Install. */
        explicit Mouse(const App& app);

        /** @brief Whether `button` is held. */
        [[nodiscard]] bool IsDown(MouseButton button) const;
        /** @brief Whether `button` went down since the last tick or frame. */
        [[nodiscard]] bool JustPressed(MouseButton button) const;
        /** @brief Whether `button` went up since the last tick or frame. */
        [[nodiscard]] bool JustReleased(MouseButton button) const;

        /** @brief Where the pointer last was, in logical coordinates from the window's top-left corner. */
        [[nodiscard]] Math::Vec2 Position() const noexcept
        {
            return m_position;
        }

        /** @brief How far the pointer moved since the last tick or frame, in the same units as Position. */
        [[nodiscard]] Math::Vec2 Motion() const;

        /** @brief How far the wheel turned since the last tick or frame, in notches: y positive away from the user. */
        [[nodiscard]] Math::Vec2 Scroll() const;

      private:
        friend class Detail::InputDispatch;

        const App* m_app;
        std::array<Detail::ControlState, MouseButtonCount> m_buttons{};
        Math::Vec2 m_position{0.0f, 0.0f};
        Detail::SummedMotion m_motion;
        Detail::SummedMotion m_scroll;
    };

    /**
     * @brief The gamepads connected now, in the standard layout: per pad, buttons held, just pressed and just released,
     * and each axis's value. A pad comes and goes with its connect and disconnect (hot plug); asked about a pad that is
     * not connected, every answer is false or zero. An app resource.
     */
    class Gamepads
    {
      public:
        /** @brief Reads its input edges by the phase of `app`; made by Input::Install. */
        explicit Gamepads(const App& app);

        /** @brief The pads connected now, oldest connection first. */
        [[nodiscard]] std::vector<GamepadId> Connected() const;

        /** @brief Whether `gamepad` is connected. */
        [[nodiscard]] bool IsConnected(GamepadId gamepad) const;

        /** @brief Whether `button` is held on `gamepad`. */
        [[nodiscard]] bool IsDown(GamepadId gamepad, GamepadButton button) const;
        /** @brief Whether `button` went down on `gamepad` since the last tick or frame. */
        [[nodiscard]] bool JustPressed(GamepadId gamepad, GamepadButton button) const;
        /** @brief Whether `button` went up on `gamepad` since the last tick or frame. */
        [[nodiscard]] bool JustReleased(GamepadId gamepad, GamepadButton button) const;

        /** @brief The value of `axis` on `gamepad`: -1 to 1 for a stick, 0 to 1 for a trigger. */
        [[nodiscard]] float Axis(GamepadId gamepad, GamepadAxis axis) const;

      private:
        friend class Detail::InputDispatch;

        struct Pad
        {
            GamepadId id;
            std::array<Detail::ControlState, GamepadButtonCount> buttons{};
            std::array<float, GamepadAxisCount> axes{};
        };

        [[nodiscard]] const Pad* Find(GamepadId gamepad) const;
        [[nodiscard]] Pad* Find(GamepadId gamepad);

        const App* m_app;
        std::vector<Pad> m_pads;
    };
} // namespace Fenrir
