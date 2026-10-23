#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"

using namespace DemoScaffoldTests;

namespace
{
    constexpr Fenrir::WindowId mainWindow{1};

    FenrirDemo::DemoScene StartAt(Fenrir::Math::Vec3 position)
    {
        FenrirDemo::DemoScene scene = EmptyScene("Open space");
        scene.camera.position = position;
        return scene;
    }

    Fenrir::Math::Vec3 CameraPosition(DemoTestApp& test)
    {
        return test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().pos;
    }

    // The direction the free-fly camera looks: its local -Z in the world.
    Fenrir::Math::Vec3 CameraForward(DemoTestApp& test)
    {
        return test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
    }
} // namespace

TEST_CASE("The free-fly camera starts where the scene says", "[demo][camera]")
{
    DemoTestApp test({StartAt({1.0f, 2.0f, 3.0f})});
    test.Frames();

    const Fenrir::Math::Vec3 position = CameraPosition(test);
    INFO("the camera is at the scene's start position");
    REQUIRE(position.x == 1.0f);
    REQUIRE(position.y == 2.0f);
    REQUIRE(position.z == 3.0f);
}

TEST_CASE("Holding the right mouse button and W flies the camera forward, along -Z", "[demo][camera]")
{
    DemoTestApp test({StartAt({0.0f, 0.0f, 0.0f})});
    test.Frames();
    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();

    feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
    feed.KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    test.Frames(30);

    const Fenrir::Math::Vec3 position = CameraPosition(test);
    INFO("the camera moved forward, along -Z, and nowhere else");
    REQUIRE(position.z < -0.5f);
    REQUIRE(std::abs(position.x) < 1e-4f);
    REQUIRE(std::abs(position.y) < 1e-4f);
}

TEST_CASE("W without the right mouse button is the scene's, and does not move the camera", "[demo][camera]")
{
    DemoTestApp test({StartAt({0.0f, 0.0f, 0.0f})});
    test.Frames();

    test.app.GetResource<Fenrir::InputFeed>().KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    test.Frames(30);

    const Fenrir::Math::Vec3 position = CameraPosition(test);
    INFO("the camera stays where it started");
    REQUIRE(position.z == 0.0f);
}

TEST_CASE("Shift flies the camera faster", "[demo][camera]")
{
    const auto distance = [](bool shift) {
        DemoTestApp test({StartAt({0.0f, 0.0f, 0.0f})});
        test.Frames();
        Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
        feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
        feed.KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
        if (shift)
        {
            feed.KeyDown(mainWindow, Fenrir::Key::LeftShift, Fenrir::Scancode::LeftShift);
        }
        test.Frames(30);
        return -CameraPosition(test).z;
    };

    INFO("the same frames with Shift held cover more distance");
    REQUIRE(distance(true) > 2.0f * distance(false));
}

TEST_CASE("The wheel changes the free-fly camera's speed", "[demo][camera]")
{
    const auto distance = [](float notches) {
        DemoTestApp test({StartAt({0.0f, 0.0f, 0.0f})});
        test.Frames();
        Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
        feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
        test.Frames();
        feed.Scroll(mainWindow, {0.0f, notches});
        test.Frames();
        feed.KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
        test.Frames(30);
        return -CameraPosition(test).z;
    };

    INFO("scrolling away from the user speeds the camera up, and towards slows it down");
    REQUIRE(distance(3.0f) > distance(0.0f));
    REQUIRE(distance(-3.0f) < distance(0.0f));
}

TEST_CASE("Moving the mouse with the right button held turns the camera", "[demo][camera]")
{
    DemoTestApp test({StartAt({0.0f, 0.0f, 0.0f})});
    test.Frames();
    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();

    feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Right);
    test.Frames();
    feed.MouseMove(mainWindow, {700.0f, 360.0f}, {100.0f, 0.0f});
    test.Frames();

    const Fenrir::Math::Vec3 forward = CameraForward(test);
    INFO("moving the mouse right turns the camera to look to the right, towards +X");
    REQUIRE(forward.x > 0.1f);
}

TEST_CASE("A left click sends the scene-click event with the ray from the camera through the point", "[demo][click]")
{
    DemoTestApp test({StartAt({0.0f, 1.0f, 5.0f})});
    test.Frames();
    Fenrir::EventReader<FenrirDemo::DemoSceneClickEvent> clicks;
    (void)clicks.Read(test.app);
    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();

    // The centre of the null backend's 1280 x 720 surface.
    feed.MouseMove(mainWindow, {640.0f, 360.0f}, {0.0f, 0.0f});
    feed.MouseButtonDown(mainWindow, Fenrir::MouseButton::Left);
    test.Frames();

    std::vector<FenrirDemo::DemoSceneClickEvent> sent;
    for (const FenrirDemo::DemoSceneClickEvent& event : clicks.Read(test.app))
    {
        sent.push_back(event);
    }
    INFO("one click, one event");
    REQUIRE(sent.size() == 1);
    INFO("the ray starts at the camera");
    REQUIRE(sent[0].ray.origin.z == 5.0f);
    REQUIRE(sent[0].ray.origin.y == 1.0f);
    INFO("through the centre of the view the ray runs along the camera's -Z");
    REQUIRE(sent[0].ray.dir.z < -0.999f);
}
