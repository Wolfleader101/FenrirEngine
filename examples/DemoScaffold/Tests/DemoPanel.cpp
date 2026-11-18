// The demo panel's rows and the app sections the scaffold gives it (examples/DemoScaffold/README.md, "The demo UI kit";
// ADR-0071): readouts pushed and pulled, the scene switcher and the frame stats, on the null
// backend with the kit's root drawing into a capturing target.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirUI/UIRoot.hpp"

using namespace DemoScaffoldTests;

namespace
{
    /** @brief A readout as the panel shows it: its label and its value. */
    struct ShownReadout
    {
        std::string label;
        std::string value;

        friend bool operator==(const ShownReadout&, const ShownReadout&) = default;
    };

    // The panel's readouts, in tree order: each row of class demo-readout, its label and its value.
    std::vector<ShownReadout> Readouts(DemoTestApp& test)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        std::vector<ShownReadout> readouts;
        for (const Fenrir::ElementId row : root.FindByClass("demo-readout"))
        {
            ShownReadout shown;
            for (const Fenrir::ElementId child : root.GetChildren(row))
            {
                if (root.HasClass(child, "demo-readout-label"))
                {
                    shown.label = root.GetText(child);
                }
                else if (root.HasClass(child, "demo-readout-value"))
                {
                    shown.value = root.GetText(child);
                }
            }
            readouts.push_back(std::move(shown));
        }
        return readouts;
    }

    // The value of the readout labelled `label`, or "(none)" when the panel has none.
    std::string ReadoutValue(DemoTestApp& test, std::string_view label)
    {
        for (const ShownReadout& readout : Readouts(test))
        {
            if (readout.label == label)
            {
                return readout.value;
            }
        }
        return "(none)";
    }

    // The scene switcher's buttons that have the class `active`, by their text.
    std::vector<std::string> ActiveSceneButtons(DemoTestApp& test)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        std::vector<std::string> active;
        for (const Fenrir::ElementId button : root.FindByClass("demo-scene-button"))
        {
            if (root.HasClass(button, "active"))
            {
                active.emplace_back(root.GetText(button));
            }
        }
        return active;
    }

    // A triangle in the XY plane at the origin, facing the free-fly camera's start.
    Fenrir::MeshData Triangle()
    {
        Fenrir::MeshData mesh;
        mesh.positions = {{-0.5f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
        mesh.indices = {0, 1, 2};
        return mesh;
    }
} // namespace

TEST_CASE("A readout set by a scene's update shows that value the same frame", "[demo][kit][readouts]")
{
    auto speed = std::make_shared<FenrirDemo::DemoReadout>();
    auto frames = std::make_shared<int>(0);
    FenrirDemo::DemoScene scene = EmptyScene("Spinning cube");
    scene.spawn = [speed](Fenrir::App& app, Fenrir::Scene&) {
        *speed = app.GetResource<FenrirDemo::DemoUI>().SceneSection("Cube").Readout("Speed");
    };
    scene.update = [speed, frames](Fenrir::App&, Fenrir::Scene&) { speed->Set(std::to_string(++*frames) + " rad/s"); };
    DemoTestApp test({scene});
    test.Frames(3);

    INFO("the readout shows what this frame's update set, under its label");
    REQUIRE(Readouts(test) == std::vector<ShownReadout>{{"Speed", std::to_string(*frames) + " rad/s"}});
}

TEST_CASE("A pulled readout shows its function's value, each frame", "[demo][kit][readouts]")
{
    auto lights = std::make_shared<int>(4);
    FenrirDemo::DemoScene scene = EmptyScene("Lit room");
    scene.spawn = [lights](Fenrir::App& app, Fenrir::Scene&) {
        app.GetResource<FenrirDemo::DemoUI>().SceneSection("Lights").Readout(
            "Lights", [lights] { return std::to_string(*lights); });
    };
    DemoTestApp test({scene});
    test.Frames(2);
    INFO("the readout shows the function's value");
    REQUIRE(ReadoutValue(test, "Lights") == "4");

    *lights = 7;
    test.Frames();
    INFO("and the function's new value the next frame");
    REQUIRE(ReadoutValue(test, "Lights") == "7");
}

TEST_CASE("A scene's readouts are gone once it is left, and fresh when it is entered again", "[demo][kit][readouts]")
{
    auto entries = std::make_shared<int>(0);
    FenrirDemo::DemoScene counted = EmptyScene("Counted");
    counted.spawn = [entries](Fenrir::App& app, Fenrir::Scene&) {
        app.GetResource<FenrirDemo::DemoUI>().SceneSection("Entries").Readout("Entry").Set(std::to_string(++*entries));
    };
    DemoTestApp test({counted, EmptyScene("Empty")});
    test.Frames(2);
    INFO("the first entry's readout");
    REQUIRE(Readouts(test) == std::vector<ShownReadout>{{"Entry", "1"}});

    test.Tap(Fenrir::Key::PageDown);
    INFO("after PageDown the scene's readout is gone");
    REQUIRE(Readouts(test).empty());

    test.Tap(Fenrir::Key::PageUp);
    INFO("entered again, the scene has one readout, the new entry's");
    REQUIRE(Readouts(test) == std::vector<ShownReadout>{{"Entry", "2"}});
}

TEST_CASE("A separator shows its title between the rows it parts", "[demo][kit][readouts]")
{
    FenrirDemo::DemoScene scene = EmptyScene("Rows");
    scene.spawn = [](Fenrir::App& app, Fenrir::Scene&) {
        FenrirDemo::DemoSection section = app.GetResource<FenrirDemo::DemoUI>().SceneSection("Rows");
        section.Readout("Before").Set("1");
        section.Separator("Later");
        section.Readout("After").Set("2");
    };
    DemoTestApp test({scene});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();

    INFO("the separator shows its title");
    REQUIRE(test.Texts("demo-separator") == std::vector<std::string>{"Later"});
    const Fenrir::ElementId separator = root.FindByClass("demo-separator").front();
    const std::vector<Fenrir::ElementId> rows = root.FindByClass("demo-readout");
    INFO("two readouts, one each side of it");
    REQUIRE(rows.size() == 2);
    INFO("below the first readout and above the second");
    REQUIRE(root.GetBoxes(separator).border.y > root.GetBoxes(rows[0]).border.y);
    REQUIRE(root.GetBoxes(separator).border.y < root.GetBoxes(rows[1]).border.y);
}

TEST_CASE("A readout's handle hides and shows it", "[demo][kit][readouts]")
{
    auto hidden = std::make_shared<FenrirDemo::DemoReadout>();
    FenrirDemo::DemoScene scene = EmptyScene("Rows");
    scene.spawn = [hidden](Fenrir::App& app, Fenrir::Scene&) {
        FenrirDemo::DemoSection section = app.GetResource<FenrirDemo::DemoUI>().SceneSection("Rows");
        section.Readout("Shown").Set("1");
        *hidden = section.Readout("Hidden");
        hidden->Set("2");
        hidden->SetShown(false);
    };
    DemoTestApp test({scene});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();

    const std::vector<Fenrir::ElementId> rows = root.FindByClass("demo-readout");
    INFO("both readouts are in the section");
    REQUIRE(rows.size() == 2);
    INFO("the shown readout takes room");
    REQUIRE(root.GetBoxes(rows[0]).border.height > 0.0f);
    INFO("the hidden one takes none");
    REQUIRE(root.GetBoxes(rows[1]).border.height == 0.0f);

    hidden->SetShown(true);
    test.Frames();
    INFO("shown again, it takes room");
    REQUIRE(root.GetBoxes(rows[1]).border.height > 0.0f);
}

TEST_CASE("A readout's handle greys it out and back", "[demo][kit][readouts]")
{
    auto readout = std::make_shared<FenrirDemo::DemoReadout>();
    FenrirDemo::DemoScene scene = EmptyScene("Rows");
    scene.spawn = [readout](Fenrir::App& app, Fenrir::Scene&) {
        *readout = app.GetResource<FenrirDemo::DemoUI>().SceneSection("Rows").Readout("Speed");
        readout->SetDisabled(true);
    };
    DemoTestApp test({scene});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementId row = root.FindByClass("demo-readout").front();

    INFO("disabled, the row has the kit's disabled class, which demo.css dims");
    REQUIRE(root.HasClass(row, "demo-disabled"));
    readout->SetDisabled(false);
    INFO("enabled again, it has not");
    REQUIRE_FALSE(root.HasClass(row, "demo-disabled"));
}

TEST_CASE("The scene switcher lists every scene, marks the active one, and a click enters another",
          "[demo][kit][switcher]")
{
    DemoTestApp test({EmptyScene("First"), EmptyScene("Second"), EmptyScene("Third")});
    test.Frames(2);

    INFO("the switcher is the panel's first section");
    REQUIRE(test.Texts("demo-section-title").front() == "Scenes");
    INFO("one button per scene, in the app's order");
    REQUIRE(test.Texts("demo-scene-button") == std::vector<std::string>{"First", "Second", "Third"});
    INFO("the active scene's button is marked");
    REQUIRE(ActiveSceneButtons(test) == std::vector<std::string>{"First"});

    const Fenrir::ElementId third = test.Kit().Root().FindByClass("demo-scene-button")[2];
    test.Click(test.Centre(third));
    INFO("a click on the third enters it");
    REQUIRE(test.ActiveScene() == "Third");
    INFO("logged as PageDown's entry is");
    REQUIRE(test.logged->InfoContains("Scene 3/3: Third"));
    INFO("and its button is now the marked one");
    REQUIRE(ActiveSceneButtons(test) == std::vector<std::string>{"Third"});
}

TEST_CASE("A click on the active scene's button leaves the scene be", "[demo][kit][switcher]")
{
    auto entries = std::make_shared<int>(0);
    FenrirDemo::DemoScene counted = EmptyScene("Counted");
    counted.spawn = [entries](Fenrir::App&, Fenrir::Scene&) { ++*entries; };
    DemoTestApp test({counted, EmptyScene("Other")});
    test.Frames(2);

    test.Click(test.Centre(test.Kit().Root().FindByClass("demo-scene-button")[0]));
    INFO("the scene was entered once, at the start, and not again");
    REQUIRE(*entries == 1);
}

TEST_CASE("PageDown moves the scene switcher's mark", "[demo][kit][switcher]")
{
    DemoTestApp test({EmptyScene("First"), EmptyScene("Second")});
    test.Frames(2);

    test.Tap(Fenrir::Key::PageDown);
    INFO("the entered scene's button is the marked one");
    REQUIRE(ActiveSceneButtons(test) == std::vector<std::string>{"Second"});
}

TEST_CASE("Frame stats show the frame time, fps and the renderer's draws", "[demo][kit][framestats]")
{
    FenrirDemo::DemoScene scene = EmptyScene("Triangles");
    scene.spawn = [](Fenrir::App& app, Fenrir::Scene& scene) {
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        const auto mesh = store.Add<Fenrir::Mesh>(Triangle(), scene);
        const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", scene).handle, scene);
        for (int i = 0; i < 3; ++i)
        {
            Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::Transform>().pos = {static_cast<float>(i) - 1.0f, 0.0f, 0.0f};
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material});
        }
    };
    DemoTestApp test({scene}, NullRun(), {}, [](FenrirDemo::DemoConfig& config) { config.frameStats = true; });
    // More than half a second of sixtieth-of-a-second frames, so the smoothed values are a whole window's.
    test.Frames(40);

    INFO("the frame stats are the app's section after the scene switcher");
    REQUIRE(test.Texts("demo-section-title") == std::vector<std::string>{"Scenes", "Frame"});
    INFO("every frame is a sixtieth of a second, so the frame time is 16.7 ms");
    REQUIRE(ReadoutValue(test, "Frame time") == "16.7 ms");
    INFO("and the fps 60");
    REQUIRE(ReadoutValue(test, "FPS") == "60");
    const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(test.app);
    INFO("the scene's triangles are drawn");
    REQUIRE(stats.instances == 3);
    INFO("the draws are the renderer's");
    REQUIRE(ReadoutValue(test, "Draws") == std::to_string(stats.draws));
    INFO("the instances are the three triangles");
    REQUIRE(ReadoutValue(test, "Instances") == "3");
    INFO("the shadow draws are the renderer's");
    REQUIRE(ReadoutValue(test, "Shadow draws") == std::to_string(stats.shadowDraws));
}

TEST_CASE("Without frameStats the panel has no Frame section", "[demo][kit][framestats]")
{
    DemoTestApp test({EmptyScene("Plain")});
    test.Frames(2);
    INFO("only the scene switcher");
    REQUIRE(test.Texts("demo-section-title") == std::vector<std::string>{"Scenes"});
}
