#include "FenrirDebugDraw/DebugDraw.hpp"

#include "FenrirApp/App.hpp"

#include <cmath>

namespace Fenrir
{
    void DebugDraw::Install(App& app)
    {
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[](App& a) {
                                     a.ForEachScene([](Scene& scene) {
                                         if (DebugDraw* draw = scene.GetResource<DebugDraw>())
                                         {
                                             draw->Clear();
                                         }
                                     });
                                 }});
    }

    void DebugDraw::Line(const Math::Vec3& a, const Math::Vec3& b, const Colour& colour)
    {
        m_lines.push_back({a, colour});
        m_lines.push_back({b, colour});
    }

    void DebugDraw::WireBox(const Math::Vec3& lo, const Math::Vec3& hi, const Colour& colour)
    {
        const Math::Vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                 {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                  {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges)
        {
            Line(c[e[0]], c[e[1]], colour);
        }
    }

    void DebugDraw::WireSphere(const Math::Vec3& centre, float radius, const Colour& colour, int segments)
    {
        const float step = 6.2831853f / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i)
        {
            const float a = step * static_cast<float>(i);
            const float b = a + step;
            const float ca = std::cos(a) * radius, sa = std::sin(a) * radius;
            const float cb = std::cos(b) * radius, sb = std::sin(b) * radius;
            Line(centre + Math::Vec3(ca, sa, 0), centre + Math::Vec3(cb, sb, 0), colour);
            Line(centre + Math::Vec3(ca, 0, sa), centre + Math::Vec3(cb, 0, sb), colour);
            Line(centre + Math::Vec3(0, ca, sa), centre + Math::Vec3(0, cb, sb), colour);
        }
    }

    void DebugDraw::Axes(const Math::Mat4& frame, float length)
    {
        const Math::Vec3 o(frame[3]);
        Line(o, o + Math::Vec3(frame[0]) * length, {1, 0, 0, 1});
        Line(o, o + Math::Vec3(frame[1]) * length, {0, 1, 0, 1});
        Line(o, o + Math::Vec3(frame[2]) * length, {0, 0, 1, 1});
    }

    void DebugDraw::Grid(float h, float spacing, const Colour& colour)
    {
        for (float x = -h; x <= h + 1e-3f; x += spacing)
        {
            Line({x, 0.001f, -h}, {x, 0.001f, h}, colour);
            Line({-h, 0.001f, x}, {h, 0.001f, x}, colour);
        }
    }
} // namespace Fenrir
