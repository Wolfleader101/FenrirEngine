#pragma once

#include <cstdint>

namespace Fenrir::Graphics
{
    /** @brief A size in pixels. Either side being zero means there is nothing to draw into (a minimised window). */
    struct PixelSize
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        /** @brief Whether there is nothing to draw into. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return width == 0 || height == 0;
        }

        friend constexpr bool operator==(const PixelSize&, const PixelSize&) = default;
    };

    /**
     * @brief What a window offers a graphics backend to draw into and present (ADR-0022): the OpenGL context today. The
     * renderer core declares it and the backends use only it; the ECS facade adapts the main window's surface to it,
     * and the core's tests use a fake. It names no graphics API type, only neutral ones.
     */
    class GraphicsSurface
    {
      public:
        virtual ~GraphicsSurface() = default;

        /** @brief The drawable size in pixels now, read at the start of each frame. */
        [[nodiscard]] virtual PixelSize GetPixelSize() const = 0;

        /** @brief The address of a graphics API function, for a backend's loader; null if there is none. */
        [[nodiscard]] virtual void* GetProcAddress(const char* name) = 0;

        /** @brief Makes the surface's context current on this thread. False, with nothing changed, if it cannot. */
        virtual bool MakeCurrent() = 0;

        /** @brief Shows what was drawn. Does nothing for a hidden window. */
        virtual void Present() = 0;

        /** @brief Sets the swap interval (0 off, 1 on, -1 adaptive). False if the surface does not support it. */
        virtual bool SetSwapInterval(int interval) = 0;

      protected:
        GraphicsSurface() = default;
        GraphicsSurface(const GraphicsSurface&) = default;
        GraphicsSurface& operator=(const GraphicsSurface&) = default;
        GraphicsSurface(GraphicsSurface&&) = default;
        GraphicsSurface& operator=(GraphicsSurface&&) = default;
    };
} // namespace Fenrir::Graphics
