#pragma once

#include <cstdint>

namespace Fenrir
{
    class App;
}

namespace Fenrir::Detail
{
    /** @brief What DebugDrawing::Install set up, shared by the owner, its system and every scene's DebugDraw. */
    struct DebugDrawState
    {
        explicit DebugDrawState(App& installedOn) : app(&installedOn)
        {
        }

        App* app;
    };

    /** @brief How long a debug draw item is kept. */
    struct Lifetime
    {
        enum class Kind : std::uint8_t
        {
            // Duration 0 drawn outside the tick phases: while Time::frameCount is still `stamp`, so until the end of
            // that frame's Render.
            ThisFrame,
            // Duration 0 drawn from PreTick, Tick or PostTick: while Time::tickCount is still `stamp`, so until the
            // next tick begins.
            UntilNextTick,
            // A positive duration: `seconds` more of frame time, counted down from the frame after `stamp`.
            Seconds,
            // DebugDraw::Forever: until cleared.
            Forever,
        };

        Kind kind = Kind::ThisFrame;
        std::uint64_t stamp = 0;
        double seconds = 0.0;
    };

    /** @brief The lifetime of an item of `duration` drawn now, from whichever phase is running (ADR-0025). */
    [[nodiscard]] Lifetime LifetimeFor(float duration, const App& app);

    /** @brief Whether an item of this lifetime is still kept. */
    [[nodiscard]] bool LifetimeIsLive(const Lifetime& lifetime, const App& app);
} // namespace Fenrir::Detail
