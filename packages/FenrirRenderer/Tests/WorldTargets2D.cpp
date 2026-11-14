// The world-space 2D target through the ECS facade (ADR-0060): 2D draw lists submitted to a WorldQuad2D entity through
// the WorldTargets2D app resource, drawn in each view's Transparent pass on the null backend, read back through
// RendererProbe (the spec's seam 4).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DrawLists2D.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::AddQuad;
using RendererTest::Pipeline2D;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    Fenrir::WorldTargets2D& WorldTargetsOf(RendererTestApp& test)
    {
        Fenrir::WorldTargets2D* targets = test.app.TryGetResource<Fenrir::WorldTargets2D>();
        INFO("Renderer::Install adds the WorldTargets2D app resource");
        REQUIRE(targets != nullptr);
        return *targets;
    }

    /** @brief A 2 x 1 world unit quad at `position`, 100 logical units to a world unit, so 200 x 100 units. */
    Fenrir::Entity AddWorldQuad(RendererTestApp& test, Fenrir::Math::Vec3 position = {0.0f, 0.0f, 0.0f})
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.AddComponent<Fenrir::WorldQuad2D>(Fenrir::WorldQuad2D{{2.0f, 1.0f}, 100.0f});
        return entity;
    }

    /** @brief A list of one builtin:2d quad over the whole 200 x 100 quad, with a scissor round its top-left corner. */
    Fenrir::DrawList2D CornerScissoredQuad(RendererTestApp& test)
    {
        Fenrir::DrawList2D list;
        const std::uint32_t pipeline = list.AddPipeline(Pipeline2D(test.Builtin("builtin:2d")));
        AddQuad(list, pipeline, {0.0f, 0.0f}, {200.0f, 100.0f}).scissor = Fenrir::Rect2D{0.0f, 0.0f, 10.0f, 10.0f};
        return list;
    }

    /** @brief The localToClip matrix a draw bound in its set 0 block (Fenrir/2D.glsl: a mat4 at offset 0). */
    Fenrir::Math::Mat4 LocalToClip(const RendererTestApp& test, const RecordedDraw& draw)
    {
        const std::vector<Fenrir::Graphics::BindingGroupEntry> entries =
            RendererProbe::Entries(test.Device(), draw.groups.at(0));
        REQUIRE(entries.size() == 1);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
        Fenrir::Math::Mat4 matrix(1.0f);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                matrix[column][row] = RendererTest::FloatAt(
                    bytes, entries[0].offset + static_cast<std::size_t>(column * 4 + row) * sizeof(float));
            }
        }
        return matrix;
    }

    /** @brief Where `matrix` puts the local point `point` in normalised device coordinates. */
    Fenrir::Math::Vec3 Ndc(const Fenrir::Math::Mat4& matrix, Fenrir::Math::Vec2 point)
    {
        const Fenrir::Math::Vec4 clip = matrix * Fenrir::Math::Vec4(point.x, point.y, 0.0f, 1.0f);
        return Fenrir::Math::Vec3(clip.x, clip.y, clip.z) / clip.w;
    }

    bool Near(float a, float b)
    {
        return std::abs(a - b) <= 1e-4f;
    }
} // namespace

TEST_CASE("A world quad's list draws as one draw in the Transparent pass, with no scissor but the view's",
          "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f}).GetComponent<Fenrir::Camera>().look.bloom.intensity = 0.0f;
    const Fenrir::Entity quad = AddWorldQuad(test);
    WorldTargetsOf(test).Submit(quad, CornerScissoredQuad(test));

    test.Frame();

    const std::vector<RecordedDraw> draws = test.Draws("Transparent");
    INFO("one Transparent draw, of builtin:2d");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "Builtin2D");
    INFO("its scissor is the view's viewport, not the command's corner: clips are the shader's");
    REQUIRE(draws[0].scissor.has_value());
    REQUIRE(*draws[0].scissor == draws[0].viewport);
    INFO("it tests the scene's depth without writing it, and draws both faces");
    const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draws[0].pipeline);
    REQUIRE(state.depth.test);
    REQUIRE_FALSE(state.depth.write);
    REQUIRE(state.cull == Fenrir::Graphics::CullMode::None);
    INFO("nothing draws on the window's overlay");
    INFO("nothing draws on the window's overlay");
    REQUIRE(test.Draws("Overlay 2D").empty());
    INFO("nothing logged an error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A world quad's set 0 maps its list's units through the quad's world matrix and the view",
          "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f});
    Fenrir::Entity quad = AddWorldQuad(test);
    WorldTargetsOf(test).Submit(quad, CornerScissoredQuad(test));
    test.Frame();
    const Fenrir::Math::Mat4 facing = LocalToClip(test, test.Draws("Transparent").at(0));

    INFO(
        "the quad's centre, (100, 50) in its list, is the entity's origin, ahead of the camera at the screen's centre");
    const Fenrir::Math::Vec3 centre = Ndc(facing, {100.0f, 50.0f});
    REQUIRE(Near(centre.x, 0.0f));
    REQUIRE(Near(centre.y, 0.0f));
    INFO("the list's top-left is the quad's top-left: left of and above the centre, X right and Y down");
    const Fenrir::Math::Vec3 topLeft = Ndc(facing, {0.0f, 0.0f});
    const Fenrir::Math::Vec3 bottomRight = Ndc(facing, {200.0f, 100.0f});
    REQUIRE(topLeft.x < 0.0f);
    REQUIRE(topLeft.y > 0.0f);
    REQUIRE(Near(bottomRight.x, -topLeft.x));
    REQUIRE(Near(bottomRight.y, -topLeft.y));
    INFO("a 2 x 1 quad is twice as wide as it is high on screen, under a square view's aspect");
    const Fenrir::Math::Vec3 topRight = Ndc(facing, {200.0f, 0.0f});
    REQUIRE(Near(topRight.x, -topLeft.x));

    // Turned a quarter about Y, the quad is seen edge on: its left and right edges meet the centre's column.
    quad.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(std::numbers::pi_v<float> * 0.5f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    WorldTargetsOf(test).Submit(quad, CornerScissoredQuad(test));
    test.Frame();
    const Fenrir::Math::Mat4 turned = LocalToClip(test, test.Draws("Transparent").at(0));
    INFO("turned edge on, the quad's left edge lies on the screen's centre column");
    REQUIRE(Near(Ndc(turned, {0.0f, 50.0f}).x, 0.0f));
    REQUIRE(Near(Ndc(turned, {200.0f, 50.0f}).x, 0.0f));
}

TEST_CASE("A world quad sorts with the transparent items by its origin's depth, back to front", "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f});
    const auto blended = test.Material("builtin:unlit");
    test.Store().SetBlendMode(blended, Fenrir::BlendMode::AlphaBlend);
    Fenrir::Entity near = test.AddMeshRenderer(test.Mesh(RendererTest::Quad()), blended);
    near.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, 1.0f};
    Fenrir::Entity far = test.AddMeshRenderer(test.Mesh(RendererTest::Quad()), blended);
    far.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -2.0f};
    const Fenrir::Entity quad = AddWorldQuad(test, {0.0f, 0.0f, 0.0f});
    WorldTargetsOf(test).Submit(quad, CornerScissoredQuad(test));

    test.Frame();

    const std::vector<RecordedDraw> draws = test.Draws("Transparent");
    INFO("the far item, then the world quad between them, then the near item");
    REQUIRE(draws.size() == 3);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) != "Builtin2D");
    REQUIRE(RendererProbe::Label(test.Device(), draws[1].pipeline) == "Builtin2D");
    REQUIRE(RendererProbe::Label(test.Device(), draws[2].pipeline) != "Builtin2D");
    REQUIRE(test.DrawPosition(draws[0]).z == -2.0f);
    REQUIRE(test.DrawPosition(draws[2]).z == 1.0f);
}

TEST_CASE("A world quad behind the camera is culled with the transparent items", "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f});
    const Fenrir::Entity behind = AddWorldQuad(test, {0.0f, 0.0f, 8.0f});
    WorldTargetsOf(test).Submit(behind, CornerScissoredQuad(test));

    test.Frame();

    INFO("no Transparent pass: nothing in front of the camera is transparent");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Transparent").empty());
    INFO("nothing logged an error");
    REQUIRE(test.messages->errors.empty());

    // Brought round in front, it draws.
    const Fenrir::Entity ahead = AddWorldQuad(test, {0.0f, 0.0f, 0.0f});
    WorldTargetsOf(test).Submit(behind, CornerScissoredQuad(test));
    WorldTargetsOf(test).Submit(ahead, CornerScissoredQuad(test));
    test.Frame();
    INFO("the quad ahead draws, the one behind does not");
    REQUIRE(test.Draws("Transparent").size() == 1);
}

TEST_CASE("A list submitted to an entity with no WorldQuad2D is dropped, logged once", "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f});
    const Fenrir::Entity plain = test.ActiveScene().GetEntityList().CreateEntity();
    WorldTargetsOf(test).Submit(plain, CornerScissoredQuad(test));
    test.Frame();
    WorldTargetsOf(test).Submit(plain, CornerScissoredQuad(test));
    test.Frame();

    INFO("nothing draws, and the warning is logged once");
    REQUIRE(test.Draws("Transparent").empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "WorldQuad2D") == 1);
}

TEST_CASE("A world quad's offscreen group draws into a transient image before the Transparent pass composites it",
          "[renderer][world]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 5.0f});
    const Fenrir::Entity quad = AddWorldQuad(test);
    Fenrir::DrawList2D list;
    const std::uint32_t pipeline = list.AddPipeline(Pipeline2D(test.Builtin("builtin:2d")));
    list.commands.emplace_back(Fenrir::BeginGroup2D{Fenrir::Rect2D{10.0f, 10.0f, 50.0f, 40.0f}});
    AddQuad(list, pipeline, {10.0f, 10.0f}, {60.0f, 50.0f});
    Fenrir::EndGroup2D end;
    end.opacity = 0.5f;
    list.commands.emplace_back(end);
    WorldTargetsOf(test).Submit(quad, std::move(list));

    test.Frame();

    const std::vector<RendererTest::PassRecord> groups = RendererTest::Labelled(test.Passes(), "Group 2D");
    INFO("one group pass, at one pixel per logical unit: 50 x 40");
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].viewport == Fenrir::Graphics::PixelRect{0, 0, 50, 40});
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    const auto group = std::find(passes.begin(), passes.end(), "Group 2D");
    const auto transparent = std::find(passes.begin(), passes.end(), "Transparent");
    INFO("the group's pass runs before the Transparent pass that composites it");
    REQUIRE(transparent != passes.end());
    REQUIRE(group < transparent);
    INFO("the Transparent pass draws the group's image: one composite quad");
    REQUIRE(test.Draws("Transparent").size() == 1);
    INFO("nothing logged an error");
    REQUIRE(test.messages->errors.empty());
}
