// The demo panel's switch rows and its hint line (spec: .scratch/demo-ui/spec.md, "The C++ API"): toggles, ranges,
// choices, colours, buttons and text entries bound to a scene's state through DemoValue, and the keyboard and gamepad
// around them, on the null backend with the kit's root drawing into a capturing target.

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Style.hpp"
#include "FenrirUI/UIRoot.hpp"

using namespace DemoScaffoldTests;
using Catch::Approx;

namespace
{
    constexpr Fenrir::WindowId mainWindow{1};
    constexpr Fenrir::GamepadId testPad{3};
    // Left of the window's centre, clear of the panel on the right and the help at the bottom left.
    const Fenrir::Math::Vec2 besidePanels{500.0f, 200.0f};

    // A scene whose spawn gives `rows` its scene section, "Switches".
    FenrirDemo::DemoScene SceneWithRows(std::function<void(const FenrirDemo::DemoSection&)> rows)
    {
        FenrirDemo::DemoScene scene = EmptyScene("Switches");
        scene.spawn = [rows = std::move(rows)](Fenrir::App& app, Fenrir::Scene&) {
            rows(app.GetResource<FenrirDemo::DemoUI>().SceneSection("Switches"));
        };
        return scene;
    }

    // The kit's elements of class `name`, in tree order.
    std::vector<Fenrir::ElementId> All(DemoTestApp& test, std::string_view name)
    {
        return test.Kit().Root().FindByClass(name);
    }

    // The kit's one element of class `name`.
    Fenrir::ElementId One(DemoTestApp& test, std::string_view name)
    {
        const std::vector<Fenrir::ElementId> found = All(test, name);
        INFO("the kit has one element of class " << name);
        REQUIRE(found.size() == 1);
        return found.front();
    }

    // `element` as the control it is.
    template <typename Control>
    Control& As(DemoTestApp& test, Fenrir::ElementId element)
    {
        auto* control = dynamic_cast<Control*>(test.Kit().Root().GetElement(element));
        INFO("the element is the control expected");
        REQUIRE(control != nullptr);
        return *control;
    }

    // Where a press `fraction` of the way across `slider`'s padding box lands, in the window's logical units: the
    // slider takes its value from there.
    Fenrir::Math::Vec2 Along(DemoTestApp& test, Fenrir::ElementId slider, float fraction)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        const Fenrir::Rect2D box = root.GetBoxes(slider).padding;
        return Fenrir::Math::Vec2(box.x + box.width * fraction, box.y + box.height * 0.5f) * root.GetScale();
    }

    Fenrir::InputFeed& Feed(DemoTestApp& test)
    {
        return test.app.GetResource<Fenrir::InputFeed>();
    }

    Fenrir::WindowInputClaims Claims(DemoTestApp& test)
    {
        return test.app.GetResource<Fenrir::InputClaims>().For(mainWindow);
    }

    bool SceneHolds(DemoTestApp& test, Fenrir::Key key)
    {
        return test.app.GetResource<Fenrir::Keyboard>().IsDown(key);
    }

    // Presses the left button at `position` and holds it.
    void PressAt(DemoTestApp& test, Fenrir::Math::Vec2 position)
    {
        Feed(test).MouseMove(mainWindow, position, {0.0f, 0.0f});
        test.Frames();
        Feed(test).MouseButtonDown(mainWindow, Fenrir::MouseButton::Left);
        test.Frames();
    }

    void Release(DemoTestApp& test)
    {
        Feed(test).MouseButtonUp(mainWindow, Fenrir::MouseButton::Left);
        test.Frames();
    }

    // Focuses the kit's element `element`, as Tab would.
    void FocusOn(DemoTestApp& test, Fenrir::ElementId element)
    {
        INFO("the element takes the focus");
        REQUIRE(test.Kit().Root().Focus(element));
        test.Frames();
    }

    // The hint line's text.
    std::string Hint(DemoTestApp& test)
    {
        const Fenrir::UIRoot& root = test.Kit().Root();
        const Fenrir::ElementId hint = root.FindById("demo-hint");
        INFO("the document has the hint line");
        REQUIRE(hint.IsValid());
        return std::string(root.GetText(hint));
    }

    // A scene with one toggle, "Shadows", over `shadows`.
    struct ToggleScene
    {
        std::shared_ptr<bool> shadows = std::make_shared<bool>(false);
        std::shared_ptr<FenrirDemo::DemoRow> row = std::make_shared<FenrirDemo::DemoRow>();
        DemoTestApp test{{SceneWithRows([shadows = shadows, row = row](const FenrirDemo::DemoSection& section) {
            *row = section.Toggle("Shadows", shadows.get(), "Casts the sun's shadows");
        })}};

        ToggleScene()
        {
            test.Frames(2);
        }

        Fenrir::ElementId Box()
        {
            return One(test, "demo-checkbox");
        }
    };

    // A scene with one range, "Exposure", from 0 to 4 with one decimal, over `exposure`; its update records what it
    // saw in `seen`.
    struct RangeScene
    {
        std::shared_ptr<float> exposure = std::make_shared<float>(0.0f);
        std::shared_ptr<float> seen = std::make_shared<float>(-1.0f);
        DemoTestApp test{{Scene()}};

        RangeScene()
        {
            test.Frames(2);
        }

        FenrirDemo::DemoScene Scene() const
        {
            FenrirDemo::DemoScene scene = SceneWithRows([exposure = exposure](const FenrirDemo::DemoSection& section) {
                section.Range("Exposure", exposure.get(), 0.0f, 4.0f, {.decimals = 1},
                              "Brightens the image before tonemapping");
            });
            scene.update = [exposure = exposure, seen = seen](Fenrir::App&, Fenrir::Scene&) { *seen = *exposure; };
            return scene;
        }

        Fenrir::ElementId Slider()
        {
            return One(test, "demo-slider");
        }
    };

    // A scene with one text entry, "Label", over `label`, which starts "Hi".
    struct TextEntryScene
    {
        std::shared_ptr<std::string> label = std::make_shared<std::string>("Hi");
        DemoTestApp test{{SceneWithRows([label = label](const FenrirDemo::DemoSection& section) {
            section.TextEntry("Label", label.get(), "The text over the cube");
        })}};

        TextEntryScene()
        {
            test.Frames(2);
        }

        Fenrir::ElementId Entry()
        {
            return One(test, "demo-entry");
        }

        // Clicks the entry, which takes the focus and the keyboard.
        void Edit()
        {
            test.Click(test.Centre(Entry()));
            test.Frames();
            INFO("the clicked entry has the keyboard");
            REQUIRE(Claims(test).keyboardFocus);
        }
    };

    // A scene with a choice of three, "View", and one of five, "Tonemapper".
    struct ChoiceScene
    {
        std::shared_ptr<int> view = std::make_shared<int>(0);
        std::shared_ptr<int> tonemapper = std::make_shared<int>(0);
        DemoTestApp test{{SceneWithRows([view = view, tonemapper = tonemapper](const FenrirDemo::DemoSection& section) {
            section.Choice("View", view.get(), {"Lit", "Albedo", "Normals"});
            section.Choice("Tonemapper", tonemapper.get(), {"None", "Reinhard", "ACES", "AgX", "Khronos PBR"});
        })}};

        ChoiceScene()
        {
            test.Frames(2);
        }
    };
} // namespace

TEST_CASE("A toggle row is labelled", "[demo][kit][switches]")
{
    ToggleScene scene;
    INFO("the row shows its label");
    REQUIRE(scene.test.Texts("demo-row-label") == std::vector<std::string>{"Shadows"});
}

TEST_CASE("A toggle's click flips its bool", "[demo][kit][switches]")
{
    ToggleScene scene;
    scene.test.Click(scene.test.Centre(scene.Box()));
    INFO("the click checked the box");
    REQUIRE(As<Fenrir::Checkbox>(scene.test, scene.Box()).IsChecked());
    INFO("and set the bool");
    REQUIRE(*scene.shadows);
}

TEST_CASE("Code flipping a toggle's bool moves its checkbox the next frame", "[demo][kit][switches]")
{
    ToggleScene scene;
    *scene.shadows = true;
    scene.test.Frames();
    INFO("the box is checked the frame after");
    REQUIRE(As<Fenrir::Checkbox>(scene.test, scene.Box()).IsChecked());
}

TEST_CASE("A disabled switch row's control takes no input", "[demo][kit][switches]")
{
    ToggleScene scene;
    scene.row->SetDisabled(true);
    scene.test.Frames();
    INFO("the row's checkbox is disabled");
    REQUIRE(As<Fenrir::Checkbox>(scene.test, scene.Box()).IsDisabled());
    scene.test.Click(scene.test.Centre(scene.Box()));
    INFO("so a click does not toggle the bool");
    REQUIRE_FALSE(*scene.shadows);
}

TEST_CASE("A range's press writes its value before the scene's update that frame", "[demo][kit][switches][range]")
{
    RangeScene scene;
    PressAt(scene.test, Along(scene.test, scene.Slider(), 0.25f));
    INFO("the press set the value a quarter of the way");
    REQUIRE(*scene.exposure == Approx(1.0f).margin(0.02));
    INFO("and the scene's update that frame saw it");
    REQUIRE(*scene.seen == *scene.exposure);
    Release(scene.test);
}

TEST_CASE("A range drag changes its value, shown beside it, and doesn't turn the free-fly camera",
          "[demo][kit][switches][range]")
{
    RangeScene scene;
    const Fenrir::Math::Quat before = scene.test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;
    PressAt(scene.test, Along(scene.test, scene.Slider(), 0.25f));
    Feed(scene.test).MouseMove(mainWindow, Along(scene.test, scene.Slider(), 0.75f), {100.0f, 0.0f});
    scene.test.Frames();
    Release(scene.test);

    INFO("the drag moved it three quarters of the way");
    REQUIRE(*scene.exposure == Approx(3.0f).margin(0.02));
    INFO("the value is shown beside the slider");
    REQUIRE(scene.test.Texts("demo-range-value") == std::vector<std::string>{std::format("{:.1f}", *scene.exposure)});
    const Fenrir::Math::Quat after = scene.test.demo.FreeFlyCamera().GetComponent<Fenrir::Transform>().rot;
    INFO("the camera looks where it did");
    REQUIRE(after == before);
}

TEST_CASE("Code setting a range's value moves its slider the next frame", "[demo][kit][switches][range]")
{
    RangeScene scene;
    *scene.exposure = 2.0f;
    scene.test.Frames();
    INFO("the slider is at the value");
    REQUIRE(As<Fenrir::Slider>(scene.test, scene.Slider()).GetValue() == Approx(2.0f));
    INFO("and the value beside it shows it");
    REQUIRE(scene.test.Texts("demo-range-value") == std::vector<std::string>{"2.0"});
}

TEST_CASE("A range being dragged is not moved by its value", "[demo][kit][switches][range]")
{
    RangeScene scene;
    PressAt(scene.test, Along(scene.test, scene.Slider(), 0.5f));
    const float dragged = As<Fenrir::Slider>(scene.test, scene.Slider()).GetValue();

    *scene.exposure = 4.0f;
    scene.test.Frames();
    INFO("while the press holds it, the slider stays where the pointer put it");
    REQUIRE(As<Fenrir::Slider>(scene.test, scene.Slider()).GetValue() == dragged);
    Release(scene.test);
}

TEST_CASE("A logarithmic range maps its ends and middle to min, sqrt(min*max) and max", "[demo][kit][switches][range]")
{
    auto distance = std::make_shared<float>(1.0f);
    DemoTestApp test({SceneWithRows([distance](const FenrirDemo::DemoSection& section) {
        section.Range("Distance", distance.get(), 0.1f, 1000.0f, {.logarithmic = true});
    })});
    test.Frames(2);
    const Fenrir::ElementId slider = One(test, "demo-slider");

    for (const auto& [fraction, expected] :
         std::vector<std::pair<float, float>>{{0.0f, 0.1f}, {0.5f, std::sqrt(0.1f * 1000.0f)}, {1.0f, 1000.0f}})
    {
        // The right edge is outside the slider: the end is reached by a drag past it, which clamps.
        PressAt(test, Along(test, slider, std::min(fraction, 0.9f)));
        Feed(test).MouseMove(mainWindow, Along(test, slider, fraction == 1.0f ? 1.2f : fraction), {0.0f, 0.0f});
        test.Frames();
        Release(test);
        INFO("a press at " << fraction << " of the way gives min * (max / min)^t");
        REQUIRE(*distance == Approx(expected).epsilon(0.01));
    }
}

TEST_CASE("A logarithmic range's value set by code sits at its logarithm's fraction", "[demo][kit][switches][range]")
{
    auto distance = std::make_shared<float>(0.1f);
    DemoTestApp test({SceneWithRows([distance](const FenrirDemo::DemoSection& section) {
        section.Range("Distance", distance.get(), 0.1f, 1000.0f, {.logarithmic = true});
    })});
    test.Frames(2);
    *distance = 1.0f;
    test.Frames();
    INFO("1 is a quarter of the way from 0.1 to 1000 in orders of magnitude");
    REQUIRE(As<Fenrir::Slider>(test, One(test, "demo-slider")).GetValue() == Approx(0.25f).margin(0.001));
}

TEST_CASE("A logarithmic range with a non-positive min is logged and drawn linear", "[demo][kit][switches][range]")
{
    auto bias = std::make_shared<float>(0.0f);
    DemoTestApp test({SceneWithRows([bias](const FenrirDemo::DemoSection& section) {
        section.Range("Bias", bias.get(), 0.0f, 2.0f, {.logarithmic = true});
    })});
    test.Frames(2);
    INFO("the kit logs the range as an error");
    REQUIRE_FALSE(test.logged->errors.empty());

    PressAt(test, Along(test, One(test, "demo-slider"), 0.5f));
    Release(test);
    INFO("and maps it linearly");
    REQUIRE(*bias == Approx(1.0f).margin(0.02));
}

TEST_CASE("An integer range steps by ones and shows its value", "[demo][kit][switches][range]")
{
    auto bounces = std::make_shared<int>(0);
    DemoTestApp test({SceneWithRows(
        [bounces](const FenrirDemo::DemoSection& section) { section.Range("Bounces", bounces.get(), 0, 8); })});
    test.Frames(2);

    PressAt(test, Along(test, One(test, "demo-slider"), 0.5f));
    Release(test);
    INFO("half way is four");
    REQUIRE(*bounces == 4);
    INFO("shown with no decimals");
    REQUIRE(test.Texts("demo-range-value") == std::vector<std::string>{"4"});
}

TEST_CASE("A range of an angle shows degrees over its radians", "[demo][kit][switches][range]")
{
    auto rotation = std::make_shared<float>(std::numbers::pi_v<float> / 2.0f);
    DemoTestApp test({SceneWithRows([rotation](const FenrirDemo::DemoSection& section) {
        section.Range("Sky rotation", rotation.get(), 0.0f, 2.0f * std::numbers::pi_v<float>,
                      {.decimals = 0, .angle = true});
    })});
    test.Frames(2);
    INFO("a quarter turn shows as 90 degrees");
    REQUIRE(test.Texts("demo-range-value") == std::vector<std::string>{"90\xC2\xB0"});
}

TEST_CASE("A choice of three is three buttons that set its index", "[demo][kit][switches][choice]")
{
    ChoiceScene scene;
    Fenrir::UIRoot& root = scene.test.Kit().Root();
    const std::vector<Fenrir::ElementId> buttons = All(scene.test, "demo-choice-button");
    INFO("three options are three buttons, and the choice of five has none");
    REQUIRE(scene.test.Texts("demo-choice-button") == std::vector<std::string>{"Lit", "Albedo", "Normals"});
    INFO("the chosen one is marked");
    REQUIRE(root.HasClass(buttons[0], "active"));

    scene.test.Click(scene.test.Centre(buttons[2]));
    INFO("a click on Normals chooses it");
    REQUIRE(*scene.view == 2);
    INFO("and marks it alone");
    REQUIRE(root.HasClass(buttons[2], "active"));
    REQUIRE_FALSE(root.HasClass(buttons[0], "active"));
}

TEST_CASE("A choice of five is a stepper that sets its index, wrapping", "[demo][kit][switches][choice]")
{
    ChoiceScene scene;
    INFO("five options are a stepper round the chosen one's name");
    REQUIRE(scene.test.Texts("demo-choice-name") == std::vector<std::string>{"None"});

    scene.test.Click(scene.test.Centre(One(scene.test, "demo-step-next")));
    INFO("next chooses the second");
    REQUIRE(*scene.tonemapper == 1);
    REQUIRE(scene.test.Texts("demo-choice-name") == std::vector<std::string>{"Reinhard"});

    scene.test.Click(scene.test.Centre(One(scene.test, "demo-step-previous")));
    scene.test.Click(scene.test.Centre(One(scene.test, "demo-step-previous")));
    INFO("previous past the first wraps to the last");
    REQUIRE(*scene.tonemapper == 4);
}

TEST_CASE("Code choosing moves a stepper the next frame", "[demo][kit][switches][choice]")
{
    ChoiceScene scene;
    *scene.tonemapper = 2;
    scene.test.Frames();
    INFO("the stepper shows the chosen option's name");
    REQUIRE(scene.test.Texts("demo-choice-name") == std::vector<std::string>{"ACES"});
}

TEST_CASE("A colour row's sliders set the bound colour, and the swatch follows", "[demo][kit][switches][colour]")
{
    auto tint = std::make_shared<Fenrir::Math::Vec3>(1.0f, 0.0f, 0.0f);
    DemoTestApp test(
        {SceneWithRows([tint](const FenrirDemo::DemoSection& section) { section.Colour("Tint", tint.get()); })});
    test.Frames(2);
    Fenrir::UIRoot& root = test.Kit().Root();
    const Fenrir::ElementId swatch = One(test, "demo-swatch");
    const auto swatchColour = [&] {
        const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(swatch);
        INFO("the swatch has a resolved style");
        REQUIRE(style != nullptr);
        return Fenrir::Math::Vec3(style->backgroundColor.r, style->backgroundColor.g, style->backgroundColor.b);
    };
    INFO("pure red is hue 0, saturation 1 and value 1");
    REQUIRE(As<Fenrir::Slider>(test, One(test, "demo-hue")).GetValue() == Approx(0.0f));
    REQUIRE(As<Fenrir::Slider>(test, One(test, "demo-saturation")).GetValue() == Approx(1.0f));
    REQUIRE(As<Fenrir::Slider>(test, One(test, "demo-value")).GetValue() == Approx(1.0f));
    INFO("and the swatch shows it");
    REQUIRE(swatchColour().x == Approx(1.0f).margin(0.01));
    REQUIRE(swatchColour().y == Approx(0.0f).margin(0.01));

    PressAt(test, Along(test, One(test, "demo-saturation"), 0.0f));
    Release(test);
    INFO("saturation 0 makes the colour white");
    REQUIRE(tint->x == Approx(1.0f));
    REQUIRE(tint->y == Approx(1.0f));
    REQUIRE(tint->z == Approx(1.0f));
    PressAt(test, Along(test, One(test, "demo-value"), 0.5f));
    Release(test);
    INFO("value half way makes it a linear grey of a half");
    REQUIRE(tint->x == Approx(0.5f).margin(0.01));
    REQUIRE(tint->z == Approx(0.5f).margin(0.01));
    test.Frames();
    INFO("and the swatch follows, the linear colour encoded for display and read back linear");
    REQUIRE(swatchColour().x == Approx(tint->x).margin(0.01));
    REQUIRE(swatchColour().y == Approx(tint->y).margin(0.01));
}

TEST_CASE("A button row calls its function when clicked", "[demo][kit][switches]")
{
    auto presses = std::make_shared<int>(0);
    DemoTestApp test({SceneWithRows(
        [presses](const FenrirDemo::DemoSection& section) { section.Button("No grade", [presses] { ++*presses; }); })});
    test.Frames(2);
    INFO("the button is captioned with its label");
    REQUIRE(test.Texts("demo-button") == std::vector<std::string>{"No grade"});
    test.Click(test.Centre(One(test, "demo-button")));
    INFO("a click calls the function once");
    REQUIRE(*presses == 1);
}

TEST_CASE("A text entry shows its bound string", "[demo][kit][switches][text]")
{
    TextEntryScene scene;
    INFO("the entry holds the string");
    REQUIRE(As<Fenrir::TextEntry>(scene.test, scene.Entry()).GetValue() == "Hi");
}

TEST_CASE("Typing into a text entry sets its string", "[demo][kit][switches][text]")
{
    TextEntryScene scene;
    scene.Edit();
    Feed(scene.test).Text(mainWindow, U"!");
    scene.test.Frames();
    INFO("the typed text is written to the string");
    REQUIRE(*scene.label == "Hi!");
}

TEST_CASE("A focused text entry claims the keyboard and text input, cancelling a held scene key",
          "[demo][kit][switches][keyboard]")
{
    TextEntryScene scene;
    Feed(scene.test).KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    scene.test.Frames();
    INFO("the scene has W held");
    REQUIRE(SceneHolds(scene.test, Fenrir::Key::W));

    scene.Edit();
    INFO("the entry claims text input too");
    REQUIRE(Claims(scene.test).textInput);
    INFO("which lets go of the scene's W");
    REQUIRE_FALSE(SceneHolds(scene.test, Fenrir::Key::W));
}

TEST_CASE("F1 types into a focused text entry rather than hiding the kit", "[demo][kit][switches][keyboard]")
{
    TextEntryScene scene;
    scene.Edit();
    scene.test.Tap(Fenrir::Key::F1);
    INFO("the kit still shows");
    REQUIRE(scene.test.Kit().IsVisible());
}

TEST_CASE("Code does not change a text entry while it is edited", "[demo][kit][switches][text]")
{
    TextEntryScene scene;
    scene.Edit();
    *scene.label = "Bye";
    scene.test.Frames();
    INFO("the focused entry keeps what it holds");
    REQUIRE(As<Fenrir::TextEntry>(scene.test, scene.Entry()).GetValue() == "Hi");
}

TEST_CASE("A click on the scene gives a text entry's keyboard back", "[demo][kit][switches][keyboard]")
{
    TextEntryScene scene;
    scene.Edit();
    scene.test.Click(besidePanels);
    INFO("the kit's keyboard and text claims are dropped");
    REQUIRE_FALSE(Claims(scene.test).keyboardFocus);
    REQUIRE_FALSE(Claims(scene.test).textInput);

    Feed(scene.test).KeyDown(mainWindow, Fenrir::Key::W, Fenrir::Scancode::W);
    scene.test.Frames();
    INFO("W is the scene's again");
    REQUIRE(SceneHolds(scene.test, Fenrir::Key::W));
}

TEST_CASE("A text entry no longer edited shows its string as code set it", "[demo][kit][switches][text]")
{
    TextEntryScene scene;
    scene.Edit();
    *scene.label = "Bye";
    scene.test.Click(besidePanels);
    INFO("once blurred, the entry shows the string");
    REQUIRE(As<Fenrir::TextEntry>(scene.test, scene.Entry()).GetValue() == "Bye");
}

TEST_CASE("Escape in a text entry gives the keyboard back", "[demo][kit][switches][keyboard]")
{
    TextEntryScene scene;
    scene.Edit();
    scene.test.Tap(Fenrir::Key::Escape);
    INFO("Escape blurs the entry, which drops the keyboard claim");
    REQUIRE_FALSE(Claims(scene.test).keyboardFocus);
}

TEST_CASE("The hint line is empty with no row hovered or focused", "[demo][kit][hint]")
{
    ToggleScene scene;
    INFO("the line is empty");
    REQUIRE(Hint(scene.test).empty());
}

TEST_CASE("The hint line shows a hovered row's hint and clears when the pointer leaves", "[demo][kit][hint]")
{
    ToggleScene scene;
    Feed(scene.test).MouseMove(mainWindow, scene.test.Centre(scene.Box()), {0.0f, 0.0f});
    scene.test.Frames();
    INFO("hovering the toggle shows its hint");
    REQUIRE(Hint(scene.test) == "Casts the sun's shadows");

    Feed(scene.test).MouseMove(mainWindow, besidePanels, {0.0f, 0.0f});
    scene.test.Frames();
    INFO("the pointer leaving clears it");
    REQUIRE(Hint(scene.test).empty());
}

TEST_CASE("The hint line shows the focused row's hint while nothing is hovered", "[demo][kit][hint]")
{
    RangeScene scene;
    FocusOn(scene.test, scene.Slider());
    INFO("the focused range's hint shows");
    REQUIRE(Hint(scene.test) == "Brightens the image before tonemapping");
}

TEST_CASE("The gamepad's directions reach the game while a kit row has the focus", "[demo][kit][gamepad]")
{
    RangeScene scene;
    *scene.exposure = 1.0f;
    Feed(scene.test).GamepadConnected(testPad);
    FocusOn(scene.test, scene.Slider());

    Feed(scene.test).GamepadButtonDown(testPad, Fenrir::GamepadButton::DPadRight);
    Feed(scene.test).GamepadButtonDown(testPad, Fenrir::GamepadButton::DPadUp);
    scene.test.Frames();
    const Fenrir::Gamepads& pads = scene.test.app.GetResource<Fenrir::Gamepads>();
    INFO("the D-pad is the game's");
    REQUIRE(pads.IsDown(testPad, Fenrir::GamepadButton::DPadRight));
    REQUIRE(pads.IsDown(testPad, Fenrir::GamepadButton::DPadUp));
    INFO("and the focused slider did not step");
    REQUIRE(*scene.exposure == 1.0f);
}
