#pragma once

#include <memory>

#include "FenrirDebugDraw/DebugDraw.hpp"

namespace Fenrir
{
    class App;
    // Only named here: FenrirDebugDrawScripting defines DebugDrawing::ScriptingExtension().
    struct ScriptingExtension;

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

        /**
         * @brief FenrirDebugDraw's scripting extension (ADR-0029): the Fenrir.DebugDraw contract, whose static
         * `DebugDraw` facade makes every DebugDraw call from C# into the active scene's DebugDraw. Add it to
         * ScriptingConfig::extensions.
         *
         * It needs no owner: its functions read the app the running scripts serve, so scripts always load. With no
         * DebugDraw in the active scene (debug drawing not installed), or a `relativeTo` entity that is not alive, they
         * draw nothing and IsGroupShown is false. In debug builds each such case is warned about once per scripting
         * install for each place in FenrirDebugDrawScripting's native code that notices it, not once per C# call site.
         *
         * Defined by the FenrirDebugDrawScripting library, not FenrirDebugDraw (which never links FenrirScripting), so
         * it exists only in a build with scripting on. Include "FenrirDebugDrawScripting/DebugDrawScripting.hpp" and
         * link FenrirDebugDrawScripting to call it.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension();

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
