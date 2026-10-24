#include "FenrirInput/InputFeed.hpp"

namespace Fenrir
{
    void InputFeed::KeyDown(WindowId window, Key key, Scancode scancode, KeyModifiers modifiers, bool repeat)
    {
        m_queue.emplace_back(KeyEvent{window, key, scancode, modifiers, true, repeat});
    }

    void InputFeed::KeyUp(WindowId window, Key key, Scancode scancode, KeyModifiers modifiers)
    {
        m_queue.emplace_back(KeyEvent{window, key, scancode, modifiers, false, false});
    }

    void InputFeed::Text(WindowId window, std::u32string_view codepoints)
    {
        for (const char32_t codepoint : codepoints)
        {
            m_queue.emplace_back(TextEvent{window, codepoint});
        }
    }

    void InputFeed::MouseButtonDown(WindowId window, MouseButton button)
    {
        m_queue.emplace_back(MouseButtonEvent{window, button, true});
    }

    void InputFeed::MouseButtonUp(WindowId window, MouseButton button)
    {
        m_queue.emplace_back(MouseButtonEvent{window, button, false});
    }

    void InputFeed::MouseMove(WindowId window, Math::Vec2 position, Math::Vec2 motion)
    {
        m_queue.emplace_back(MouseMotionEvent{window, position, motion});
    }

    void InputFeed::Scroll(WindowId window, Math::Vec2 notches)
    {
        m_queue.emplace_back(MouseScrollEvent{window, notches});
    }

    void InputFeed::FocusGained(WindowId window)
    {
        m_queue.emplace_back(WindowFocusEvent{window, true});
    }

    void InputFeed::FocusLost(WindowId window)
    {
        m_queue.emplace_back(WindowFocusEvent{window, false});
    }

    void InputFeed::GamepadConnected(GamepadId gamepad)
    {
        m_queue.emplace_back(GamepadConnectionEvent{gamepad, true});
    }

    void InputFeed::GamepadDisconnected(GamepadId gamepad)
    {
        m_queue.emplace_back(GamepadConnectionEvent{gamepad, false});
    }

    void InputFeed::GamepadButtonDown(GamepadId gamepad, GamepadButton button)
    {
        m_queue.emplace_back(GamepadButtonEvent{gamepad, button, true});
    }

    void InputFeed::GamepadButtonUp(GamepadId gamepad, GamepadButton button)
    {
        m_queue.emplace_back(GamepadButtonEvent{gamepad, button, false});
    }

    void InputFeed::GamepadAxisMoved(GamepadId gamepad, GamepadAxis axis, float value)
    {
        m_queue.emplace_back(GamepadAxisEvent{gamepad, axis, value});
    }
} // namespace Fenrir
