#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "DemoApp.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UIWorldTarget.hpp"

// The room's world roots (DemoApp.hpp): the light terminal and the name plate, each a root on a quad entity of the
// room's, and the SDF family their text and the compass's letters are set in.
namespace
{
    using UIDemoTest::DemoApp;

    // The entity whose quad `root` draws on.
    Fenrir::Entity QuadOf(const Fenrir::UIRoot& root)
    {
        const auto target = std::dynamic_pointer_cast<Fenrir::UIWorldTarget>(root.GetConfig().target);
        INFO("the root's target is not a UIWorldTarget");
        REQUIRE(target != nullptr);
        return target->GetEntity();
    }

    // A ray from two metres in front of the terminal's quad straight at the point `ui` of its root, in UI units.
    Fenrir::Math::Ray RayAt(const Fenrir::UIRoot& root, Fenrir::Math::Vec2 ui)
    {
        const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(QuadOf(root));
        const Fenrir::Math::Vec3 local(ui.x / UIDemo::WorldUnitsPerMetre - UIDemo::TerminalWidth * 0.5f,
                                       UIDemo::TerminalHeight * 0.5f - ui.y / UIDemo::WorldUnitsPerMetre, 0.0f);
        const Fenrir::Math::Vec3 point(world * Fenrir::Math::Vec4(local, 1.0f));
        const Fenrir::Math::Vec3 facing =
            Fenrir::Math::Normalized(Fenrir::Math::Vec3(world * Fenrir::Math::Vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        Fenrir::Math::Ray ray;
        ray.origin = point + facing * 2.0f;
        ray.dir = -facing;
        return ray;
    }

    // Presses and releases on `id` in the terminal through the ray, as the room's system gives it.
    void Click(UIDemo::WorldUI& world, std::string_view id)
    {
        const Fenrir::UIRoot& root = *world.TerminalRoot();
        const Fenrir::Math::Ray ray = RayAt(root, DemoApp::Centre(root, root.FindById(id)));
        INFO("the ray at #" << id << " is not over the terminal");
        REQUIRE(world.Update(ray, false));
        REQUIRE(world.Update(ray, true));
        REQUIRE(world.Update(ray, false));
    }

    bool Same(const Fenrir::Colour& a, const Fenrir::Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    // The colour of every lamp in the room, which must all agree.
    Fenrir::Colour LampColour(DemoApp& demo)
    {
        Fenrir::EntityList& entities = demo.app.GetActiveScene().GetEntityList();
        const std::array<Fenrir::ComponentType, 1> lights{Fenrir::ComponentType::Native<Fenrir::PointLight>()};
        std::optional<Fenrir::Colour> colour;
        entities.ForEachWith(lights, [&](std::uint32_t id) {
            const Fenrir::Colour lamp = entities.GetEntity(id).GetComponent<Fenrir::PointLight>().colour;
            REQUIRE((!colour || Same(*colour, lamp)));
            colour = lamp;
        });
        REQUIRE(colour.has_value());
        return *colour;
    }
} // namespace

TEST_CASE("The room puts the terminal and the name plate on quads, 500 UI units a metre, in the SDF family")
{
    DemoApp demo;
    demo.Frames(3);

    INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
    INFO("nothing logged an error");
    INFO("the demo logged no error");
    REQUIRE(demo.errors->empty());
    UIDemo::WorldUI& world = demo.app.GetResource<UIDemo::WorldUI>();
    REQUIRE(world.TerminalRoot() != nullptr);
    REQUIRE(world.PlateRoot() != nullptr);
    INFO("the terminal's 1.6 by 1 metres and the plate's 1 by 0.32 are their roots' UI units, at a scale of one");
    REQUIRE(world.TerminalRoot()->GetTargetSize() == Fenrir::UITargetSize{{800.0f, 500.0f}, 1.0f});
    REQUIRE(world.PlateRoot()->GetTargetSize() == Fenrir::UITargetSize{{500.0f, 160.0f}, 1.0f});
    REQUIRE(world.TerminalRoot()->GetScale() == 1.0f);
    INFO("both blend linear, as the lit room does");
    REQUIRE(world.TerminalRoot()->GetConfig().blendSpace == Fenrir::BlendSpace2D::Linear);
    INFO("the terminal holds its buttons and slider, the plate its name");
    REQUIRE(world.TerminalRoot()->FindById("cool").IsValid());
    REQUIRE(world.TerminalRoot()->FindById("hue").IsValid());
    REQUIRE(world.PlateRoot()->FindById("plate-name").IsValid());

    const auto faces = demo.app.GetResource<Fenrir::UIRoots>().GetFontFaces();
    const bool sdf = std::any_of(faces.begin(), faces.end(), [](const Fenrir::UIFontFace& face) {
        return face.family == UIDemo::SdfFamily && face.rendering == Fenrir::FontRendering::Sdf;
    });
    INFO("the ui-sdf family is registered in SDF mode");
    REQUIRE(sdf);
}

TEST_CASE("A ray at the terminal's buttons sets the lamps' colour")
{
    DemoApp demo;
    demo.Frames(3);
    UIDemo::WorldUI& world = demo.app.GetResource<UIDemo::WorldUI>();
    INFO("the lamps start warm");
    REQUIRE(Same(LampColour(demo), Fenrir::Colour::FromSrgb(UIDemo::WorldUI::WarmLight)));

    Click(world, "cool");
    REQUIRE(Same(world.GetLightColour(), Fenrir::Colour::FromSrgb(UIDemo::WorldUI::CoolLight)));
    demo.Frame();
    INFO("Cool turns every lamp cool, and the terminal's reading says so");
    REQUIRE(Same(LampColour(demo), Fenrir::Colour::FromSrgb(UIDemo::WorldUI::CoolLight)));
    REQUIRE(world.TerminalRoot()->GetText(world.TerminalRoot()->FindById("reading")) == "Cool white");

    Click(world, "warm");
    demo.Frame();
    REQUIRE(Same(LampColour(demo), Fenrir::Colour::FromSrgb(UIDemo::WorldUI::WarmLight)));
    INFO("nothing logged an error");
    INFO("the demo logged no error");
    REQUIRE(demo.errors->empty());
}

TEST_CASE("The terminal's hue slider, dragged by the ray, turns the lamps' hue")
{
    DemoApp demo;
    demo.Frames(3);
    UIDemo::WorldUI& world = demo.app.GetResource<UIDemo::WorldUI>();
    const Fenrir::UIRoot& root = *world.TerminalRoot();
    const Fenrir::ElementBoxes slider = root.GetBoxes(root.FindById("hue"));
    const float y = slider.border.y + slider.border.height * 0.5f;

    // Pressed near its left end, dragged to near its right end, let go.
    INFO("the ray along the slider is not over the terminal");
    REQUIRE(world.Update(RayAt(root, {slider.border.x + 4.0f, y}), true));
    REQUIRE(world.Update(RayAt(root, {slider.border.x + slider.border.width * 0.75f, y}), true));
    REQUIRE(world.Update(RayAt(root, {slider.border.x + slider.border.width * 0.75f, y}), false));
    demo.Frame();

    INFO("the lamps take a hue three quarters round, which is not the warm white they started in");
    REQUIRE_FALSE(Same(LampColour(demo), Fenrir::Colour::FromSrgb(UIDemo::WorldUI::WarmLight)));
    REQUIRE(Same(LampColour(demo), world.GetLightColour()));
    REQUIRE(root.GetText(root.FindById("reading")).starts_with("Hue "));
    INFO("nothing logged an error");
    INFO("the demo logged no error");
    REQUIRE(demo.errors->empty());
}
