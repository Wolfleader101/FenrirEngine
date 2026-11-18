// Demo slots (ADR-0072; examples/DemoScaffold/README.md): a scene with a Razor panel gets an element of
// a known id in the demo panel, which C# finds by that id and mounts its Razor component into. It goes when the scene
// is left, and with no script to mount into it, it shows a note that the panel needs scripting.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirUI/UIRoot.hpp"

using namespace DemoScaffoldTests;

namespace
{
    // A scene named `name` whose demo slot is `slot`.
    FenrirDemo::DemoScene RazorScene(std::string name, std::string slot)
    {
        FenrirDemo::DemoScene scene = EmptyScene(std::move(name));
        scene.razorPanel = std::move(slot);
        return scene;
    }

    // Whether the kit's root has a live element of id `id`.
    bool HasSlot(DemoTestApp& test, std::string_view id)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        const Fenrir::ElementId slot = root.FindById(id);
        return slot.IsValid() && root.IsAlive(slot);
    }
} // namespace

TEST_CASE("A scene with a Razor panel has its slot by id while active, and not after it is left", "[slot]")
{
    DemoTestApp test({RazorScene("Panelled", "panelled-razor"), EmptyScene("Plain")});
    test.Frames(2);
    INFO("the first scene is active");
    REQUIRE(test.ActiveScene() == "Panelled");
    INFO("its slot is in the kit by its id");
    REQUIRE(HasSlot(test, "panelled-razor"));

    test.Tap(Fenrir::Key::PageDown);
    INFO("PageDown enters the second scene");
    REQUIRE(test.ActiveScene() == "Plain");
    INFO("the first scene's slot went with it");
    REQUIRE_FALSE(HasSlot(test, "panelled-razor"));

    test.Tap(Fenrir::Key::PageUp);
    INFO("PageUp enters the first scene again");
    REQUIRE(test.ActiveScene() == "Panelled");
    INFO("its slot is back");
    REQUIRE(HasSlot(test, "panelled-razor"));
}

TEST_CASE("With no script to mount into it, a scene's slot shows that its panel needs scripting", "[slot]")
{
    DemoTestApp test({RazorScene("Panelled", "panelled-razor")});
    test.Frames(2);
    const Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementId slot = root.FindById("panelled-razor");
    INFO("the scene's slot is there");
    REQUIRE(slot.IsValid());
    const std::vector<Fenrir::ElementId> notes = root.FindByClass("demo-slot-note");
    INFO("the kit has one slot note");
    REQUIRE(notes.size() == 1);
    INFO("the note is in the slot");
    REQUIRE(root.GetParent(notes.front()) == slot);
    INFO("the note says the panel needs scripting and where its keys are");
    REQUIRE(root.GetText(notes.front()) == "This panel is a Razor component and needs scripting "
                                           "(FENRIR_ENABLE_SCRIPTING); its keys are in the help.");
}

TEST_CASE("A row made in a slot shows its hidden hint label in the hint line while hovered", "[slot][hint]")
{
    // The elements a shared Razor component makes for a switch row (Fenrir.Demo.Razor's Toggle): the row, its label and
    // control, and its hint as a label of class demo-row-hint, which demo.css hides.
    DemoTestApp test({RazorScene("Panelled", "panelled-razor")});
    test.Frames();
    Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementId slot = root.FindById("panelled-razor");
    INFO("the scene's slot is there");
    REQUIRE(slot.IsValid());
    const Fenrir::ElementId row = root.Create(slot, "panel");
    root.AddClass(row, "demo-row");
    root.AddClass(row, "demo-toggle");
    const Fenrir::ElementId label = root.Create(row, "label");
    root.AddClass(label, "demo-row-label");
    root.SetText(label, "Reversed");
    const Fenrir::ElementId box = root.Create(row, "checkbox");
    root.AddClass(box, "demo-checkbox");
    const Fenrir::ElementId hint = root.Create(row, "label");
    root.AddClass(hint, "demo-row-hint");
    root.SetText(hint, "Turns the boxes the other way; R does too");
    test.Frames();

    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
    feed.MouseMove(Fenrir::WindowId{1}, test.Centre(box), {0.0f, 0.0f});
    test.Frames();
    const Fenrir::ElementId line = root.FindById("demo-hint");
    INFO("the document has the hint line");
    REQUIRE(line.IsValid());
    INFO("hovering the row's checkbox shows the row's hint");
    REQUIRE(root.GetText(line) == "Turns the boxes the other way; R does too");
    INFO("the hint label itself takes no room");
    REQUIRE(root.GetBoxes(hint).border.height == 0.0f);
}
