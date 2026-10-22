#pragma once

#include <memory>

#include "FenrirDebugDraw/DebugDraw.hpp"

namespace Fenrir
{
    class App;

    /**
     * @brief The owner of what DebugDrawing::Install set up: each scene's DebugDraw and the expiry system. Move-only.
     *
     * Destroying it removes every scene's DebugDraw; the system it installed then does nothing. Destroy it before the
     * App, and keep it alive while the App runs.
     */
    class DebugDrawing
    {
      public:
        /**
         * @brief Turns debug drawing on for `app`. Every scene gets a DebugDraw: the scenes there are now at once, and
         * a scene made later at the start of the next frame. A sequential `PreUpdate` system, the expiry system, runs
         * once a frame: timed items lose the frame's time, and expired items, and those whose `relativeTo` entity is
         * gone, are dropped. Items drawn from parallel systems need no system of their own: they are merged when their
         * batch ends, in registration order, through the scheduler's per-system send buffers. It cannot fail.
         */
        [[nodiscard]] static DebugDrawing Install(App& app);

        DebugDrawing(DebugDrawing&& other) noexcept;
        DebugDrawing& operator=(DebugDrawing&& other) noexcept;
        DebugDrawing(const DebugDrawing&) = delete;
        DebugDrawing& operator=(const DebugDrawing&) = delete;
        ~DebugDrawing();

        /** @brief Whether this owner holds an install: false once moved from. Install itself always succeeds. */
        [[nodiscard]] bool IsValid() const noexcept;

      private:
        explicit DebugDrawing(std::shared_ptr<Detail::DebugDrawState> state);

        void Shutdown() noexcept;

        // Shared with the installed system, which holds it weakly: once the owner is gone it does nothing.
        std::shared_ptr<Detail::DebugDrawState> m_state;
    };
} // namespace Fenrir
