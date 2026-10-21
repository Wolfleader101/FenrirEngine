#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): the least of ADR-0025's FenrirDebugDraw. A scene resource that
// game code draws into each frame; lines only, depth tested, drawn once. No GPU code: it tessellates into a plain batch
// the renderer uploads.

#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

#include <vector>

namespace Fenrir
{
    class App;

    struct DebugLineVertex
    {
        Math::Vec3 position;
        Colour colour;
    };

    class DebugDraw
    {
      public:
        /** @brief Clears every scene's once-only drawing at the start of each frame (PreUpdate). */
        static void Install(App& app);

        void Line(const Math::Vec3& a, const Math::Vec3& b, const Colour& colour);
        void WireBox(const Math::Vec3& min, const Math::Vec3& max, const Colour& colour);
        void WireSphere(const Math::Vec3& centre, float radius, const Colour& colour, int segments = 32);
        void Axes(const Math::Mat4& frame, float length);
        void Grid(float halfExtent, float spacing, const Colour& colour);

        /** @brief This frame's lines, two vertices each. */
        [[nodiscard]] const std::vector<DebugLineVertex>& Lines() const
        {
            return m_lines;
        }

        void Clear()
        {
            m_lines.clear();
        }

      private:
        std::vector<DebugLineVertex> m_lines;
    };
} // namespace Fenrir
