// Shapes and text drawn from C# tessellate exactly as the same C++ calls do (spec seam 7: debug draw from C#), with
// every shape option and with none, so the facade passes every value and the C# options' defaults are C++'s.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <string>

#include "DebugDrawScriptingApp.hpp"

using namespace Fenrir;
using namespace DebugDrawScriptingTests;
using Math::Vec3;

namespace
{
    bool Same(float a, float b)
    {
        return std::equal_to<float>{}(a, b);
    }

    bool Same(const Vec3& a, const Vec3& b)
    {
        return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z);
    }

    bool Same(const Colour& a, const Colour& b)
    {
        return Same(a.r, b.r) && Same(a.g, b.g) && Same(a.b, b.b) && Same(a.a, b.a);
    }

    bool Same(const std::vector<DebugVertex>& a, const std::vector<DebugVertex>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!Same(a[i].position, b[i].position) || !Same(a[i].colour, b[i].colour))
            {
                return false;
            }
        }
        return true;
    }

    // Whether two tessellations are the same, value for value: what crossed from C# is unchanged.
    bool Same(const DebugBatches& a, const DebugBatches& b)
    {
        if (a.lines.size() != b.lines.size() || a.triangles.size() != b.triangles.size() ||
            a.texts.size() != b.texts.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.lines.size(); ++i)
        {
            if (a.lines[i].depth != b.lines[i].depth || !Same(a.lines[i].thickness, b.lines[i].thickness) ||
                !Same(a.lines[i].vertices, b.lines[i].vertices))
            {
                return false;
            }
        }
        for (std::size_t i = 0; i < a.triangles.size(); ++i)
        {
            if (a.triangles[i].depth != b.triangles[i].depth || !Same(a.triangles[i].vertices, b.triangles[i].vertices))
            {
                return false;
            }
        }
        for (std::size_t i = 0; i < a.texts.size(); ++i)
        {
            const DebugTextRun& x = a.texts[i];
            const DebugTextRun& y = b.texts[i];
            if (x.space != y.space || x.text != y.text || !Same(x.position, y.position) || !Same(x.size, y.size) ||
                !Same(x.colour, y.colour))
            {
                return false;
            }
        }
        return true;
    }

    const Colour paint{0.25f, 0.5f, 0.75f, 0.5f};
    // The fixture's quarter turn about +Y and oblique tilt; glm's quaternion takes w first.
    const Math::Quat turn(0.9238795f, 0.0f, 0.38268343f, 0.0f);
    const Math::Quat tilt(0.9128709f, 0.18257418f, 0.36514837f, 0.0f);
    const Math::Mat4 placed(Math::Vec4(0, 0, -1, 0), Math::Vec4(0, 1, 0, 0), Math::Vec4(1, 0, 0, 0),
                            Math::Vec4(1, 2, 3, 1));
    const Math::Mat4 perspective(Math::Vec4(2, 0, 0, 0), Math::Vec4(0, 2, 0, 0), Math::Vec4(0, 0, 0, -1),
                                 Math::Vec4(0, 0, 0.5f, 0));

    // The C++ call the fixture's ShapeCalls.Draw(index) makes, with the same values.
    void CppShape(DebugDraw& draw, int index, bool withOptions)
    {
        const ShapeOptions options =
            withOptions ? ShapeOptions{.group = "Shapes", .thickness = 3.0f, .depth = DepthMode::OnTop, .segments = 8}
                        : ShapeOptions{};
        const std::array<Vec3, 4> star{Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 1)};
        const std::array<Vec3, 7> triangles{Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(1, 0, 0),
                                            Vec3(1, 1, 0), Vec3(0, 1, 0), Vec3(5, 5, 5)};
        const Math::AABB box{Vec3(-1, -2, -3), Vec3(1, 2, 3)};
        switch (index)
        {
        case 0:
            draw.Line(Vec3(1, 2, 3), Vec3(4, 5, 6), paint, options);
            break;
        case 1:
            draw.Ray(Vec3(0, 1, 0), Vec3(2, 0, 1), paint, options);
            break;
        case 2:
            draw.Arrow(Vec3(0, 0, 0), Vec3(0, 2, 0), 0.5f, paint, options);
            break;
        case 3:
            draw.Polyline(star, true, paint, options);
            break;
        case 4:
            draw.Point(Vec3(1, 1, 1), 6.0f, paint, options);
            break;
        case 5:
            draw.Box(box, paint, options);
            break;
        case 6:
            draw.Box(Vec3(1, 2, 3), Vec3(0.5f, 1, 1.5f), turn, paint, options);
            break;
        case 7:
            draw.Sphere(Vec3(1, 2, 3), 1.5f, paint, options);
            break;
        case 8:
            draw.Capsule(Vec3(0, 0, 0), Vec3(0, 2, 0), 0.5f, paint, options);
            break;
        case 9:
            draw.Cylinder(Vec3(0, 0, 0), Vec3(1, 2, 0), 0.5f, paint, options);
            break;
        case 10:
            draw.Cone(Vec3(0, 2, 0), Vec3(0, 0, 0), 0.75f, paint, options);
            break;
        case 11:
            draw.Circle(Vec3(1, 0, 0), Vec3(0, 1, 0), 2.0f, paint, options);
            break;
        case 12:
            draw.Arc(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0), 2.0f, paint, options);
            break;
        case 13:
            draw.Plane(Vec3(0, 1, 0), Vec3(0, 0.6f, 0.8f), 1.5f, paint, options);
            break;
        case 14:
            draw.Grid(Vec3(0, 0, 0), tilt, 3, 0.5f, paint, options);
            break;
        case 15:
            draw.Axes(placed, 2.0f, options);
            break;
        case 16:
            draw.Frustum(perspective, paint, options, 0.25f);
            break;
        case 17:
            draw.SolidBox(box, paint, options);
            break;
        case 18:
            draw.SolidBox(Vec3(1, 2, 3), Vec3(0.5f, 1, 1.5f), turn, paint, options);
            break;
        case 19:
            draw.SolidSphere(Vec3(1, 2, 3), 1.5f, paint, options);
            break;
        case 20:
            draw.SolidCapsule(Vec3(0, 0, 0), Vec3(0, 2, 0), 0.5f, paint, options);
            break;
        case 21:
            draw.Triangles(triangles, paint, options);
            break;
        default:
            FAIL("The fixture has a shape the test does not: " << index);
        }
    }
} // namespace

TEST_CASE("Every shape drawn from C# tessellates as the same C++ call does", "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    const int shapes = test.Call<int>(ShapeCallsType, "Shapes");
    {
        INFO("The fixture should draw all 22 wire and solid shape calls");
        REQUIRE(shapes == 22);
    }
    for (int index = 0; index < shapes; ++index)
    {
        for (const bool withOptions : {false, true})
        {
            test.Draw().Clear();
            test.Call<void>(ShapeCallsType, "Draw", index, withOptions ? 1 : 0);
            const DebugBatches fromCSharp = test.Draw().Tessellate();
            test.Draw().Clear();
            CppShape(test.Draw(), index, withOptions);
            const DebugBatches fromCpp = test.Draw().Tessellate();

            INFO("Shape " << index << (withOptions ? " with every option set" : " with no options")
                          << ": C# should draw something, the same batches as C++ does");
            REQUIRE_FALSE(fromCpp.Empty());
            REQUIRE(Same(fromCSharp, fromCpp));
        }
    }
}

TEST_CASE("Screen text, messages and world text drawn from C# tessellate as the same C++ calls do",
          "[debugdraw][scripting]")
{
    DebugDrawScriptingApp test;
    test.Call<void>(TextCallsType, "Draw");
    const DebugBatches fromCSharp = test.Draw().Tessellate();
    test.Draw().Clear();

    DebugDraw& draw = test.Draw();
    draw.ScreenText(Math::Vec2(16, 120), "Screen text: café", Colour::White, {.size = 20.0f});
    draw.Message("first", Colour::Green);
    draw.Message(7, "keyed one", Colour::Yellow);
    draw.Message("second", Colour::Cyan, {.group = "Messages"});
    draw.Message(7, "keyed two", Colour::Red);
    draw.WorldText(Vec3(1, 2, 3), "label Ærø €", Colour::Magenta, {.size = 14.0f});
    const DebugBatches fromCpp = draw.Tessellate();

    {
        INFO("C++ should give a screen run, three messages (the keyed one replaced) and a world run");
        REQUIRE(fromCpp.texts.size() == 5);
    }
    INFO("C# should give the same runs, its text as the same UTF-8");
    REQUIRE(Same(fromCSharp, fromCpp));
}
