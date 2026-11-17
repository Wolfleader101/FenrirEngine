// Demo tutorials (spec: .scratch/demo-ui/spec.md, "Demo tutorials"): a scene's steps in a card at the top centre, shown
// on the scene's first entry in a run and never in a smoke run or with FENRIR_DEMO_KEYS, stepped by hand or by a step's
// `done`, closed by Close or Escape and shown again by the scene's Tutorial button, on the null backend with the kit's
// root drawing into a capturing target.

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIRoot.hpp"

using namespace DemoScaffoldTests;

namespace
{
    // Frames enough for the card's outro to end (demo.css's transitions are a fraction of a second).
    constexpr int outroFrames = 60;

    // A tutorial of three steps, "One", "Two" and "Three", none ending itself.
    FenrirDemo::DemoTutorial ThreeSteps()
    {
        return {.title = "Getting about",
                .steps = {{.title = "One", .text = "PageDown steps through the scenes."},
                          {.title = "Two", .text = "F1 hides the kit.", .keys = {{"F1", "hide or show the kit"}}},
                          {.title = "Three", .text = "That is all."}}};
    }

    // A scene named `name` with `tutorial`.
    FenrirDemo::DemoScene TutorialScene(std::string name, FenrirDemo::DemoTutorial tutorial = ThreeSteps())
    {
        FenrirDemo::DemoScene scene = EmptyScene(std::move(name));
        scene.tutorial = std::move(tutorial);
        return scene;
    }

    // The kit's elements of class `name` that are not leaving: an element removed waits for its outro.
    std::vector<Fenrir::ElementId> Live(DemoTestApp& test, std::string_view name)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        std::vector<Fenrir::ElementId> live;
        for (const Fenrir::ElementId element : root.FindByClass(name))
        {
            if (!root.HasPseudoClass(element, Fenrir::PseudoClass::Outro))
            {
                live.push_back(element);
            }
        }
        return live;
    }

    // The tutorial card's step title, or "(no card)" when no card shows.
    std::string ShownStep(DemoTestApp& test)
    {
        if (Live(test, "demo-tutorial").empty())
        {
            return "(no card)";
        }
        const std::vector<std::string> titles = test.Texts("demo-tutorial-step-title");
        return titles.empty() ? "(no step title)" : titles.back();
    }

    // The card's one element of class `name`.
    Fenrir::ElementId CardPart(DemoTestApp& test, std::string_view name)
    {
        const std::vector<Fenrir::ElementId> found = Live(test, name);
        INFO("the card has one element of class " << name);
        REQUIRE(found.size() == 1);
        return found.front();
    }

    // Clicks the card's element of class `name`.
    void ClickCard(DemoTestApp& test, std::string_view name)
    {
        test.Click(test.Centre(CardPart(test, name)));
    }

    // The kit's button captioned `caption`, or nothing.
    Fenrir::ElementId ButtonCaptioned(DemoTestApp& test, std::string_view caption)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        for (const Fenrir::ElementId button : root.FindByClass("demo-button"))
        {
            if (root.GetText(button) == caption)
            {
                return button;
            }
        }
        return {};
    }
} // namespace

TEST_CASE("A scene's tutorial shows its first step on the scene's first entry", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);

    INFO("the card shows the tutorial's first step");
    REQUIRE(ShownStep(test) == "One");
    INFO("the kit says a tutorial shows");
    REQUIRE(test.Kit().IsTutorialShown());
    INFO("under the tutorial's title");
    REQUIRE(test.Texts("demo-tutorial-title") == std::vector<std::string>{"Getting about"});
    INFO("with the step's text");
    REQUIRE(test.Texts("demo-tutorial-text") == std::vector<std::string>{"PageDown steps through the scenes."});
}

TEST_CASE("A scene's tutorial does not show on its second entry", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube"), EmptyScene("Other")});
    test.Frames(2);
    INFO("the first entry shows the tutorial");
    REQUIRE(ShownStep(test) == "One");

    test.Tap(Fenrir::Key::PageDown);
    test.Frames(outroFrames);
    INFO("the tutorial's card goes with its scene");
    REQUIRE(ShownStep(test) == "(no card)");
    INFO("and the tutorial with it");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());

    test.Tap(Fenrir::Key::PageUp);
    test.Frames(2);
    INFO("PageUp enters the scene again");
    REQUIRE(test.ActiveScene() == "Lit cube");
    INFO("entered again, the scene shows no tutorial");
    REQUIRE(ShownStep(test) == "(no card)");
}

TEST_CASE("A tutorial never shows in a smoke run", "[demo][kit][tutorial][smoke]")
{
    auto shown = std::make_shared<int>(0);
    FenrirDemo::DemoScene scene = TutorialScene("Lit cube");
    scene.update = [shown](Fenrir::App& app, Fenrir::Scene&) {
        if (app.GetResource<FenrirDemo::DemoUI>().IsTutorialShown())
        {
            ++*shown;
        }
    };
    FenrirDemo::DemoRunOptions run = NullRun();
    run.smokeFrames = 10;
    DemoTestApp test({scene}, run);

    const int exitCode = test.demo.Run();

    INFO("the smoke run passes");
    REQUIRE(exitCode == 0);
    INFO("in none of its frames does the tutorial show");
    REQUIRE(*shown == 0);
}

TEST_CASE("A tutorial never shows with FENRIR_DEMO_KEYS", "[demo][kit][tutorial]")
{
    FenrirDemo::DemoRunOptions run = NullRun();
    run.pressKeys = true;
    DemoTestApp test({TutorialScene("Lit cube")}, run);
    test.Frames(2);

    INFO("a run pressing the scene's keys shows no tutorial");
    REQUIRE(ShownStep(test) == "(no card)");
}

TEST_CASE("Next steps a tutorial on, and is Done on its last step", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);

    ClickCard(test, "demo-tutorial-next");
    INFO("Next shows the second step");
    REQUIRE(ShownStep(test) == "Two");
    INFO("with its key rows");
    REQUIRE(test.Texts("demo-tutorial-key") == std::vector<std::string>{"F1"});

    ClickCard(test, "demo-tutorial-next");
    INFO("Next again shows the last step");
    REQUIRE(ShownStep(test) == "Three");
    INFO("whose Next button is Done");
    REQUIRE(test.Kit().Root().GetText(CardPart(test, "demo-tutorial-next")) == "Done");

    ClickCard(test, "demo-tutorial-next");
    INFO("Done closes the tutorial");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());
    test.Frames(outroFrames);
    INFO("and its card goes once its outro ends");
    REQUIRE(test.Kit().Root().FindByClass("demo-tutorial").empty());
}

TEST_CASE("Back steps a tutorial back, and is disabled on its first step", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    const auto backDisabled = [&test] {
        const auto* back =
            dynamic_cast<const Fenrir::Control*>(test.Kit().Root().GetElement(CardPart(test, "demo-tutorial-back")));
        return back != nullptr && back->IsDisabled();
    };
    INFO("on the first step there is nothing to go back to");
    REQUIRE(backDisabled());

    ClickCard(test, "demo-tutorial-next");
    INFO("Next shows the second step");
    REQUIRE(ShownStep(test) == "Two");
    INFO("past the first step Back is enabled");
    REQUIRE_FALSE(backDisabled());
    ClickCard(test, "demo-tutorial-back");
    INFO("Back shows the step before");
    REQUIRE(ShownStep(test) == "One");
}

TEST_CASE("Close closes a tutorial on any step", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    ClickCard(test, "demo-tutorial-next");

    ClickCard(test, "demo-tutorial-close");
    INFO("Close closes it on its second step");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());
    INFO("and its card is going");
    REQUIRE(ShownStep(test) == "(no card)");
}

TEST_CASE("Escape closes a shown tutorial", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);

    test.Tap(Fenrir::Key::Escape);
    INFO("Escape closes the tutorial");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());
}

TEST_CASE("Escape closes a tutorial after a click on its card", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    ClickCard(test, "demo-tutorial-next");

    test.Tap(Fenrir::Key::Escape);
    INFO("Escape closes the tutorial whether or not the card had the keyboard");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());
}

TEST_CASE("The scene's keys reach the game after a click on the tutorial's card", "[demo][kit][tutorial]")
{
    auto pressed = std::make_shared<int>(0);
    FenrirDemo::DemoScene scene = TutorialScene("Lit cube");
    scene.update = [pressed](Fenrir::App& app, Fenrir::Scene&) {
        if (app.GetResource<Fenrir::Keyboard>().JustPressed(Fenrir::Key::L))
        {
            ++*pressed;
        }
    };
    DemoTestApp test({scene});
    test.Frames(2);
    // Close's button stays through the card's outro, so it would hold the focus, and the keyboard, while it fades.
    ClickCard(test, "demo-tutorial-close");

    test.Tap(Fenrir::Key::L);
    INFO("the scene's keys work at once after the card: it keeps no keyboard");
    REQUIRE(*pressed == 1);
}

TEST_CASE("A step whose done turns true moves the tutorial on, and closes it after the last", "[demo][kit][tutorial]")
{
    auto ready = std::make_shared<bool>(false);
    auto asked = std::make_shared<int>(0);
    const auto done = [ready, asked](Fenrir::App&) {
        ++*asked;
        return *ready;
    };
    FenrirDemo::DemoTutorial tutorial{.title = "By itself",
                                      .steps = {{.title = "Press L", .text = "L lights it.", .done = done},
                                                {.title = "Press K", .text = "K colours it.", .done = done}}};
    DemoTestApp test({TutorialScene("Lit cube", tutorial)});
    test.Frames(3);
    INFO("done is asked each frame while its step shows");
    REQUIRE(*asked >= 2);
    INFO("false, the step stays");
    REQUIRE(ShownStep(test) == "Press L");

    *ready = true;
    test.Frames();
    INFO("true, the next step shows");
    REQUIRE(ShownStep(test) == "Press K");
    test.Frames();
    INFO("true on the last step, the tutorial closes");
    REQUIRE_FALSE(test.Kit().IsTutorialShown());
}

TEST_CASE("A closed tutorial's done is not asked", "[demo][kit][tutorial]")
{
    auto asked = std::make_shared<int>(0);
    FenrirDemo::DemoTutorial tutorial{.title = "By itself",
                                      .steps = {{.title = "Wait", .text = "Never ends.", .done = [asked](Fenrir::App&) {
                                                     ++*asked;
                                                     return false;
                                                 }}}};
    DemoTestApp test({TutorialScene("Lit cube", tutorial)});
    test.Frames(2);
    ClickCard(test, "demo-tutorial-close");
    const int before = *asked;

    test.Frames(5);
    INFO("once closed, the step is no longer asked whether it is done");
    REQUIRE(*asked == before);
}

TEST_CASE("The scene's Tutorial button shows its tutorial again from its first step", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    ClickCard(test, "demo-tutorial-next");
    ClickCard(test, "demo-tutorial-close");
    test.Frames(outroFrames);
    INFO("the tutorial is closed before its button is pressed");
    REQUIRE(ShownStep(test) == "(no card)");

    const Fenrir::ElementId button = ButtonCaptioned(test, "Tutorial");
    INFO("a scene with a tutorial has a Tutorial button in its section");
    REQUIRE(button.IsValid());
    INFO("in the section titled after the scene");
    REQUIRE(test.Texts("demo-section-title").back() == "Lit cube");
    test.Click(test.Centre(button));
    INFO("the button shows the tutorial again, from its first step");
    REQUIRE(ShownStep(test) == "One");
}

TEST_CASE("The Tutorial button restarts a tutorial that shows", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    ClickCard(test, "demo-tutorial-next");

    test.Click(test.Centre(ButtonCaptioned(test, "Tutorial")));
    INFO("the tutorial is back on its first step");
    REQUIRE(ShownStep(test) == "One");
    INFO("in the one card");
    REQUIRE(Live(test, "demo-tutorial").size() == 1);
}

TEST_CASE("A scene with no tutorial has no Tutorial button", "[demo][kit][tutorial]")
{
    DemoTestApp test({EmptyScene("Plain")});
    test.Frames(2);

    INFO("no tutorial, no button");
    REQUIRE_FALSE(ButtonCaptioned(test, "Tutorial").IsValid());
}

TEST_CASE("The tutorial's card is at the top centre", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    const Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementBoxes boxes = root.GetBoxes(CardPart(test, "demo-tutorial"));
    const float rootWidth = root.GetBoxes(root.GetRootElement()).border.width;

    INFO("the card is centred across the window");
    REQUIRE(std::abs(boxes.border.x + boxes.border.width * 0.5f - rootWidth * 0.5f) < 1.0f);
    INFO("at its top");
    REQUIRE(boxes.border.y < 200.0f);
}

TEST_CASE("A press on the tutorial's card sends no scene click, and one beside it does", "[demo][kit][tutorial]")
{
    DemoTestApp test({TutorialScene("Lit cube")});
    test.Frames(2);
    const Fenrir::Math::Vec2 onCard = test.Centre(CardPart(test, "demo-tutorial-text"));
    const Fenrir::ElementBoxes boxes = test.Kit().Root().GetBoxes(CardPart(test, "demo-tutorial"));
    // Left of the card, level with its text: the card's row across the window takes no press of its own.
    const Fenrir::Math::Vec2 besideCard{(boxes.border.x - 40.0f) * test.Kit().Root().GetScale(), onCard.y};

    INFO("a press on the card is the kit's");
    REQUIRE(test.SceneClicksAt(onCard) == 0);
    INFO("a press beside it is the scene's");
    REQUIRE(test.SceneClicksAt(besideCard) == 1);
}
