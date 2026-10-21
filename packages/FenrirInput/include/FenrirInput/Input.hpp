#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): the least of ADR-0017's FenrirInput that drives a fly camera.
// Links no library; FenrirWindow feeds it, as a test could.

#include <array>
#include <cstdint>

namespace Fenrir
{
    class App;

    enum class Key : std::uint8_t
    {
        Unknown,
        W,
        A,
        S,
        D,
        Q,
        E,
        V,
        LeftShift,
        Space,
        Escape,
        F1,
        F2,
        Count
    };

    /** @brief App resource: keys held now, and pressed since the last frame. */
    struct Keyboard
    {
        std::array<bool, static_cast<std::size_t>(Key::Count)> down{};
        std::array<bool, static_cast<std::size_t>(Key::Count)> pressed{};

        [[nodiscard]] bool IsDown(Key key) const
        {
            return down[static_cast<std::size_t>(key)];
        }

        [[nodiscard]] bool JustPressed(Key key) const
        {
            return pressed[static_cast<std::size_t>(key)];
        }
    };

    /** @brief App resource: the mouse, in logical window coordinates. */
    struct Mouse
    {
        float x = 0.0f;
        float y = 0.0f;
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        std::array<bool, 3> buttons{}; // left, right, middle
    };

    namespace Input
    {
        /** @brief Adds the Keyboard and Mouse app resources. */
        void Install(App& app);
    } // namespace Input

    /** @brief What a window library (or a test) calls to feed input. */
    namespace InputFeed
    {
        /** @brief Clears per-frame edges and deltas. FenrirWindow calls it before pumping events. */
        void BeginFrame(App& app);
        void SetKey(App& app, Key key, bool down);
        void MouseMoved(App& app, float x, float y, float dx, float dy);
        void SetMouseButton(App& app, int button, bool down);
    } // namespace InputFeed
} // namespace Fenrir
