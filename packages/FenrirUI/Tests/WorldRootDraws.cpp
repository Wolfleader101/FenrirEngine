// World roots on the renderer's null backend (the spec's seam 4): a root whose target is a UIWorldTarget draws its
// list on its entity's WorldQuad2D through the renderer's WorldTargets2D, as one draw in the camera's Transparent pass,
// read back through RendererProbe as the renderer's own world target tests are.

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIWorldTarget.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A 2 by 1 world unit quad at `position`, facing the camera at z = 5, with a root holding one panel. */
    Fenrir::UIRoot& WorldRoot(RendererTestApp& test, Fenrir::Math::Vec3 position)
    {
        Fenrir::Entity quad = test.ActiveScene().GetEntityList().CreateEntity();
        quad.GetComponent<Fenrir::Transform>().pos = position;
        quad.AddComponent<Fenrir::WorldQuad2D>(Fenrir::WorldQuad2D{{2.0f, 1.0f}, 100.0f});
        Fenrir::UIRootConfig config;
        config.target = std::make_shared<Fenrir::UIWorldTarget>(test.app, quad);
        Fenrir::UIRoot& root = test.app.GetResource<Fenrir::UIRoots>().CreateRoot(config);
        const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
        Fenrir::StyleDeclarations* style = root.EditInlineStyle(panel);
        INFO("the panel has no inline style to edit");
        REQUIRE(style != nullptr);
        style->SetWidth(Fenrir::Length::Percent(100.0f))
            .SetHeight(Fenrir::Length::Percent(100.0f))
            .SetBackgroundColor(Fenrir::Colour::FromSrgb(0x336699FF))
            .SetBorderRadius(Fenrir::CornerRadii::All(10.0f))
            .SetOverflow(Fenrir::Overflow::Hidden);
        return root;
    }

    /** @brief The localToClip matrix a draw bound in its set 0 block (Fenrir/2D.glsl: a mat4 at offset 0). */
    Fenrir::Math::Mat4 LocalToClip(const RendererTestApp& test, const RendererTest::RecordedDraw& draw)
    {
        const std::vector<Fenrir::Graphics::BindingGroupEntry> entries =
            RendererProbe::Entries(test.Device(), draw.groups.at(0));
        INFO("set 0 binds one block, the target's");
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
} // namespace

TEST_CASE("A world root's list draws as one Transparent draw of builtin:painter, unscissored, through its quad",
          "[ui][world]")
{
    RendererTestApp test;
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    test.AddCamera({0.0f, 0.0f, 5.0f});
    (void)WorldRoot(test, {0.5f, 0.0f, 0.0f});

    test.Frame();
    test.Frame();

    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Transparent");
    INFO("the root's rounded, clipped panel is one draw of builtin:painter's linear variant");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinPainter");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline).empty());
    INFO("its scissor is the whole view: the clips are the painter's shader's");
    REQUIRE(draws[0].scissor.has_value());
    REQUIRE(*draws[0].scissor == draws[0].viewport);
    INFO("set 0 carries the quad's matrix: the root's centre, (100, 50), is the quad's origin, right of the centre");
    const Fenrir::Math::Vec4 centre = LocalToClip(test, draws[0]) * Fenrir::Math::Vec4(100.0f, 50.0f, 0.0f, 1.0f);
    REQUIRE(centre.x / centre.w > 0.0f);
    REQUIRE(std::abs(centre.y / centre.w) < 1e-4f);
    INFO("nothing draws on the window's overlay");
    REQUIRE(test.Draws("Overlay 2D").empty());
    INFO("nothing logged an error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A world root behind the camera is culled with the transparent items", "[ui][world]")
{
    RendererTestApp test;
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    test.AddCamera({0.0f, 0.0f, 5.0f});
    (void)WorldRoot(test, {0.0f, 0.0f, 9.0f});

    test.Frame();
    test.Frame();

    INFO("nothing transparent is in front of the camera: no Transparent pass");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Transparent").empty());
    INFO("nothing logged an error");
    REQUIRE(test.messages->errors.empty());
}
