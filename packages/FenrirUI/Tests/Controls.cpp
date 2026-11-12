// The controls (spec: "FenrirUI" controls, seam 1): a headless App with FenrirInput's test feed drives buttons,
// checkboxes, sliders, progress bars, images and scroll views, and reads back their values, pseudo-classes, UIEvents
// and rectangles. Each test lays the tree out on one frame and feeds input for the next, since a router hit-tests
// against the last frame's layout. The default stylesheet is on, as a game's root has it.

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::Key;
using Fenrir::MouseButton;
using Fenrir::Scancode;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using Fenrir::Math::Vec2;
using namespace UITest;

namespace
{
    // An element of `tag` placed absolutely at `x`, `y`, sized `width` by `height`.
    ElementId Place(UIRoot& root, ElementId parent, std::string_view tag, float x, float y, float width, float height)
    {
        const ElementId element = root.Create(parent, tag);
        REQUIRE(root.SetStyleProperty(element, "position", "absolute"));
        REQUIRE(root.SetStyleProperty(element, "left", std::to_string(x) + "px"));
        REQUIRE(root.SetStyleProperty(element, "top", std::to_string(y) + "px"));
        REQUIRE(root.SetStyleProperty(element, "width", std::to_string(width) + "px"));
        REQUIRE(root.SetStyleProperty(element, "height", std::to_string(height) + "px"));
        return element;
    }

    template <typename TControl>
    TControl& As(UIRoot& root, ElementId element)
    {
        auto* control = dynamic_cast<TControl*>(root.GetElement(element));
        REQUIRE(control != nullptr);
        return *control;
    }

    // The UIEvents of `kind` at `target` that `reader` has not read yet.
    std::vector<UIEvent> Of(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app, UIEventKind kind, ElementId target)
    {
        std::vector<UIEvent> found;
        for (const UIEvent& event : reader.Read(app))
        {
            if (event.kind == kind && event.target == target)
            {
                found.push_back(event);
            }
        }
        return found;
    }

    void Move(UITestApp& test, Vec2 position)
    {
        test.Feed().MouseMove(testWindow, position, {0.0f, 0.0f});
    }

    void Press(UITestApp& test, Vec2 position)
    {
        Move(test, position);
        test.Feed().MouseButtonDown(testWindow, MouseButton::Left);
    }

    void Release(UITestApp& test)
    {
        test.Feed().MouseButtonUp(testWindow, MouseButton::Left);
    }

    Vec2 Centre(const Fenrir::Rect2D& rect)
    {
        return {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
    }

    // A `width` by `height` opaque image in the app's asset store.
    Fenrir::AssetHandle<Fenrir::Image> SolidImage(UITestApp& test, std::uint32_t width, std::uint32_t height)
    {
        Fenrir::ImageData pixels;
        pixels.width = width;
        pixels.height = height;
        pixels.pixels.assign(static_cast<std::size_t>(width) * height * 4, std::byte{0xFF});
        return test.app.GetResource<Fenrir::AssetStore>().Add<Fenrir::Image>(std::move(pixels),
                                                                             Fenrir::AssetOwner::App());
    }
} // namespace

TEST_CASE("Every control is made under its tag name as its own class", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId parent = root.GetRootElement();
    (void)As<Fenrir::Button>(root, root.Create(parent, "button"));
    (void)As<Fenrir::Checkbox>(root, root.Create(parent, "checkbox"));
    (void)As<Fenrir::Slider>(root, root.Create(parent, "slider"));
    (void)As<Fenrir::ProgressBar>(root, root.Create(parent, "progress"));
    (void)As<Fenrir::ImageView>(root, root.Create(parent, "image"));
    (void)As<Fenrir::ScrollView>(root, root.Create(parent, "scroll"));
}

TEST_CASE("A button clicked sends click, and not when the release is outside it", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId button = Place(root, root.GetRootElement(), "button", 10.0f, 10.0f, 100.0f, 40.0f);
    root.SetText(button, "Play");
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({50.0f, 30.0f});
    test.Frame();
    INFO("a press and release inside it is one click");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).size() == 1);
    REQUIRE(root.GetFocused() == button);

    Press(test, {50.0f, 30.0f});
    test.Frame();
    Move(test, {300.0f, 200.0f});
    Release(test);
    test.Frame();
    INFO("a press inside released outside is no click");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).empty());
}

TEST_CASE("Space and Enter on a focused button click it", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId button = Place(root, root.GetRootElement(), "button", 10.0f, 10.0f, 100.0f, 40.0f);
    test.Frame();
    INFO("a button makes itself focusable");
    REQUIRE(root.IsFocusable(button));
    REQUIRE(root.Focus(button));
    Fenrir::EventReader<UIEvent> events;

    test.Type(Key::Space, Scancode::Space);
    test.Frame();
    INFO("Space clicks it once");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).size() == 1);

    test.Type(Key::Enter, Scancode::Enter);
    test.Frame();
    INFO("Enter clicks it once");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).size() == 1);
}

TEST_CASE("A disabled button carries :disabled, takes no focus and sends no click", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId button = Place(root, root.GetRootElement(), "button", 10.0f, 10.0f, 100.0f, 40.0f);
    REQUIRE(root.Focus(button));
    As<Fenrir::Button>(root, button).SetDisabled(true);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::MouseButtonEvent> buttons;

    REQUIRE(root.HasPseudoClass(button, Fenrir::PseudoClass::Disabled));
    INFO("disabling it took the focus away");
    REQUIRE_FALSE(root.GetFocused().IsValid());
    REQUIRE_FALSE(root.Focus(button));

    test.Click({50.0f, 30.0f});
    test.Frame();
    INFO("the click is still the UI's, so the game does not see it, but the button sends nothing");
    REQUIRE(buttons.Read(test.app).empty());
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).empty());

    As<Fenrir::Button>(root, button).SetDisabled(false);
    test.Frame();
    test.Click({50.0f, 30.0f});
    test.Frame();
    INFO("enabled again, it clicks");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).size() == 1);
}

TEST_CASE("A checkbox toggles on click and Space, with :checked and change", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId box = Place(root, root.GetRootElement(), "checkbox", 10.0f, 10.0f, 20.0f, 20.0f);
    auto& checkbox = As<Fenrir::Checkbox>(root, box);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({20.0f, 20.0f});
    test.Frame();
    INFO("a click checks it, with :checked and a change of 1");
    REQUIRE(checkbox.IsChecked());
    REQUIRE(root.HasPseudoClass(box, Fenrir::PseudoClass::Checked));
    std::vector<UIEvent> changes = Of(events, test.app, UIEventKind::Change, box);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].value == 1.0f);

    INFO("the click focused it, so Space toggles it back");
    test.Type(Key::Space, Scancode::Space);
    test.Frame();
    REQUIRE_FALSE(checkbox.IsChecked());
    REQUIRE_FALSE(root.HasPseudoClass(box, Fenrir::PseudoClass::Checked));
    changes = Of(events, test.app, UIEventKind::Change, box);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].value == 0.0f);

    INFO("setting it from code checks it and sends no change");
    checkbox.SetChecked(true);
    REQUIRE(root.HasPseudoClass(box, Fenrir::PseudoClass::Checked));
    test.Frame();
    REQUIRE(Of(events, test.app, UIEventKind::Change, box).empty());
}

TEST_CASE("A slider dragged to its middle reports the mid value snapped to its step, and arrows move a step",
          "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 10.0f, 200.0f, 20.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 100.0f);
    slider.SetStep(10.0f);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    Press(test, {20.0f, 20.0f});
    test.Frame();
    INFO("the press sets the value where it lands: a tenth of the way is 10");
    REQUIRE(slider.GetValue() == 10.0f);
    std::vector<UIEvent> changes = Of(events, test.app, UIEventKind::Change, element);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].value == 10.0f);
    Move(test, {103.0f, 25.0f});
    test.Frame();
    INFO("dragged to 103 of 200 is 51.5, snapped to 50, with a change while dragging");
    REQUIRE(slider.GetValue() == 50.0f);
    changes = Of(events, test.app, UIEventKind::Change, element);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].value == 50.0f);
    REQUIRE(changes[0].integer == 0);

    Move(test, {500.0f, 25.0f});
    Release(test);
    test.Frame();
    INFO("dragged past its end it stops at max, and the release sends its final change");
    REQUIRE(slider.GetValue() == 100.0f);
    changes = Of(events, test.app, UIEventKind::Change, element);
    REQUIRE(changes.size() == 2);
    REQUIRE(changes.back().value == 100.0f);
    REQUIRE(changes.back().integer == 1);

    INFO("the press focused it: Left steps it down by 10, Right back up");
    REQUIRE(root.GetFocused() == element);
    test.Type(Key::Left, Scancode::Left);
    test.Frame();
    REQUIRE(slider.GetValue() == 90.0f);
    test.Type(Key::Right, Scancode::Right);
    test.Frame();
    REQUIRE(slider.GetValue() == 100.0f);
    test.Type(Key::Down, Scancode::Down);
    test.Frame();
    REQUIRE(slider.GetValue() == 90.0f);

    INFO("a wheel notch towards the user over it steps it down");
    Move(test, {100.0f, 20.0f});
    test.Feed().Scroll(testWindow, {0.0f, -1.0f});
    test.Frame();
    REQUIRE(slider.GetValue() == 80.0f);

    INFO("its thumb is placed at the value, 80% along (the default stylesheet's transform centres it there)");
    REQUIRE(Near(root.GetBoxes(slider.GetThumb()).border.x, 160.0f, 0.5f));
}

TEST_CASE("A click on a control's own part names the control", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId box = Place(root, root.GetRootElement(), "checkbox", 10.0f, 10.0f, 20.0f, 20.0f);
    As<Fenrir::Checkbox>(root, box).SetChecked(true);
    test.Frame();
    const Fenrir::Rect2D mark = root.GetBoxes(As<Fenrir::Checkbox>(root, box).GetMark()).border;
    REQUIRE(mark.width > 0.0f);
    Fenrir::EventReader<UIEvent> events;

    test.Click(Centre(mark));
    test.Frame();
    INFO("the mark takes no pointer input, so the checkbox is hit, clicked and toggled");
    REQUIRE(Of(events, test.app, UIEventKind::Click, box).size() == 1);
    REQUIRE_FALSE(As<Fenrir::Checkbox>(root, box).IsChecked());
}

TEST_CASE("The wheel over a slider without the focus passes to the scroll view round it", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId view = Place(root, root.GetRootElement(), "scroll", 0.0f, 0.0f, 300.0f, 100.0f);
    const ElementId element = root.Create(view, "slider");
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 10.0f);
    slider.SetStep(1.0f);
    slider.SetValue(5.0f);
    const ElementId filler = root.Create(view, "div");
    REQUIRE(root.SetStyleProperty(filler, "height", "400px"));
    test.Frame();
    const Vec2 over = Centre(root.GetBoxes(element).border);

    Move(test, over);
    test.Feed().Scroll(testWindow, {0.0f, -1.0f});
    test.Frame();
    INFO("the view scrolled and the slider kept its value");
    REQUIRE(root.GetScrollOffset(view).y == Fenrir::ScrollView::ScrollStep);
    REQUIRE(slider.GetValue() == 5.0f);
}

TEST_CASE("A vertical slider runs from min at the bottom to max at the top", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "slider", 10.0f, 0.0f, 20.0f, 200.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetOrientation(Fenrir::Orientation::Vertical);
    slider.SetRange(0.0f, 10.0f);
    slider.SetStep(1.0f);
    test.Frame();
    REQUIRE(root.HasClass(element, "vertical"));

    Press(test, {20.0f, 150.0f});
    test.Frame();
    INFO("150 down of 200 is a quarter of the way up");
    REQUIRE(slider.GetValue() == 3.0f);
    Move(test, {20.0f, 99.0f});
    Release(test);
    test.Frame();
    REQUIRE(slider.GetValue() == 5.0f);
    test.Type(Key::Up, Scancode::Up);
    test.Frame();
    REQUIRE(slider.GetValue() == 6.0f);
}

TEST_CASE("A progress bar's fill is the fraction of its track", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "progress", 0.0f, 0.0f, 200.0f, 10.0f);
    auto& progress = As<Fenrir::ProgressBar>(root, element);
    progress.SetRange(0.0f, 40.0f);
    progress.SetValue(10.0f);
    test.Frame();

    REQUIRE(progress.GetFraction() == 0.25f);
    const Fenrir::Rect2D track = root.GetBoxes(element).content;
    const Fenrir::Rect2D fill = root.GetBoxes(progress.GetFill()).border;
    REQUIRE(Near(fill.x, track.x));
    REQUIRE(Near(fill.width, track.width * 0.25f));
    REQUIRE(Near(fill.height, track.height));

    progress.SetValue(100.0f);
    test.Frame();
    INFO("past max it is full");
    REQUIRE(Near(root.GetBoxes(progress.GetFill()).border.width, track.width));
}

TEST_CASE("An image measures its asset's size under each fit, and draws by its fit", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Fenrir::AssetHandle<Fenrir::Image> picture = SolidImage(test, 40, 30);
    for (const Fenrir::ImageFit fit : {Fenrir::ImageFit::Stretch, Fenrir::ImageFit::Contain, Fenrir::ImageFit::Cover})
    {
        const ElementId element = root.Create(root.GetRootElement(), "image");
        REQUIRE(root.SetStyleProperty(element, "align-self", "flex-start"));
        As<Fenrir::ImageView>(root, element).SetFit(fit);
        As<Fenrir::ImageView>(root, element).SetSource(picture);
        test.Frame();
        INFO("with no size given it is the image's 40 by 30");
        const Fenrir::Rect2D content = root.GetBoxes(element).content;
        REQUIRE(Near(content.width, 40.0f));
        REQUIRE(Near(content.height, 30.0f));
        root.Remove(element, Fenrir::RemoveMode::Immediate);
    }

    INFO("given only a width, its height follows the image's aspect");
    const ElementId wide = root.Create(root.GetRootElement(), "image");
    REQUIRE(root.SetStyleProperty(wide, "width", "80px"));
    REQUIRE(root.SetStyleProperty(wide, "align-self", "flex-start"));
    As<Fenrir::ImageView>(root, wide).SetSource(picture);
    test.Frame();
    REQUIRE(Near(root.GetBoxes(wide).content.height, 60.0f));
    root.Remove(wide, Fenrir::RemoveMode::Immediate);

    INFO("a 40 by 20 image contained in a 100 square draws 100 by 50, centred");
    const ElementId boxed = Place(root, root.GetRootElement(), "image", 0.0f, 0.0f, 100.0f, 100.0f);
    As<Fenrir::ImageView>(root, boxed).SetFit(Fenrir::ImageFit::Contain);
    As<Fenrir::ImageView>(root, boxed).SetSource(SolidImage(test, 40, 20));
    test.Frame();
    const Fenrir::DrawList2D& list = target->lists.back();
    std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
    std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
    REQUIRE(vertices.size() == 4);
    float minY = 1e9f;
    float maxY = -1e9f;
    float maxX = -1e9f;
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
        maxX = std::max(maxX, vertex.position.x);
    }
    INFO("with the quad's one-unit anti-aliasing margin round it");
    REQUIRE(Near(maxY - minY, 52.0f));
    REQUIRE(Near((minY + maxY) / 2.0f, 50.0f));
    REQUIRE(Near(maxX, 101.0f));
}

namespace
{
    // A 100-square scroll view at the root's top-left, with no border or padding, holding three 100-square rows (the
    // last a button): 300 high, so it scrolls 200.
    struct ScrollFixture
    {
        UITestApp test;
        std::shared_ptr<CaptureTarget> target;
        UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
        ElementId view = Place(root, root.GetRootElement(), "scroll", 0.0f, 0.0f, 100.0f, 100.0f);
        ElementId rows[3]{};

        ScrollFixture()
        {
            REQUIRE(root.SetStyleProperty(view, "border", "0"));
            REQUIRE(root.SetStyleProperty(view, "padding", "0"));
            for (int i = 0; i < 3; ++i)
            {
                rows[i] = root.Create(view, i == 2 ? "button" : "div");
                REQUIRE(root.SetStyleProperty(rows[i], "height", "100px"));
                REQUIRE(root.SetStyleProperty(rows[i], "padding", "0"));
                REQUIRE(root.SetStyleProperty(rows[i], "border", "0"));
            }
            test.Frame();
        }

        Fenrir::ScrollView& Scroll()
        {
            return As<Fenrir::ScrollView>(root, view);
        }
    };
} // namespace

TEST_CASE("A scroll view scrolls by the wheel and clamps at its content's end", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    REQUIRE(fixture.root.GetScrollRange(fixture.view) == Vec2(0.0f, 200.0f));

    Move(test, {50.0f, 50.0f});
    test.Feed().Scroll(testWindow, {0.0f, -1.0f});
    test.Frame();
    INFO("a notch towards the user scrolls a step down");
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(0.0f, Fenrir::ScrollView::ScrollStep));
    test.Frame();
    INFO("its rows move up by the offset");
    REQUIRE(Near(fixture.root.GetBoxes(fixture.rows[1]).border.y, 100.0f - Fenrir::ScrollView::ScrollStep));

    test.Feed().Scroll(testWindow, {0.0f, -10.0f});
    test.Frame();
    INFO("ten more stop at the content's end");
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(0.0f, 200.0f));

    fixture.Scroll().SetOffset({0.0f, 500.0f});
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(0.0f, 200.0f));
    fixture.Scroll().SetOffset({0.0f, -5.0f});
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(0.0f, 0.0f));
}

TEST_CASE("A scroll view hit-tests and paints its children at their scrolled place, clipped to it",
          "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.Scroll().SetOffset({0.0f, 200.0f});
    fixture.test.Frame();

    INFO("scrolled to the end, the third row is where the first was");
    REQUIRE(Near(fixture.root.GetBoxes(fixture.rows[2]).border, {0.0f, 0.0f, 100.0f, 100.0f}));
    INFO("the rows above the view are clipped to it");
    REQUIRE(Near(fixture.root.GetBoxes(fixture.rows[0]).clip, {0.0f, 0.0f, 100.0f, 100.0f}));
    REQUIRE(fixture.root.HitTest({50.0f, 50.0f}) == fixture.rows[2]);
    INFO("nothing of it is hit below the view, where the third row was before it scrolled");
    REQUIRE_FALSE(fixture.root.HitTest({50.0f, 250.0f}).IsValid());

    Fenrir::EventReader<UIEvent> events;
    fixture.test.Click({50.0f, 50.0f});
    fixture.test.Frame();
    REQUIRE(Of(events, fixture.test.app, UIEventKind::Click, fixture.rows[2]).size() == 1);
}

TEST_CASE("A scroll view's bar drags it", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    // The thumb fills its track's width and runs its whole length.
    fixture.root.AddStylesheet("::scrollbar { padding: 0; }", "game.css");
    test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    REQUIRE(bars.vertical);
    REQUIRE_FALSE(bars.horizontal);
    INFO("the thumb is a third of the track: the view shows a third of its content");
    REQUIRE(Near(bars.verticalThumb.height, bars.verticalTrack.height / 3.0f, 1.0f));
    REQUIRE(Near(bars.verticalThumb.y, bars.verticalTrack.y));
    INFO("the bar is hit before the row under it");
    REQUIRE(fixture.root.HitTest(Centre(bars.verticalThumb)) == fixture.view);

    const float travel = bars.verticalTrack.height - bars.verticalThumb.height;
    Press(test, Centre(bars.verticalThumb));
    test.Frame();
    Move(test, Centre(bars.verticalThumb) + Vec2(0.0f, travel / 2.0f));
    test.Frame();
    INFO("dragged half its travel, it is scrolled half its range");
    REQUIRE(Near(fixture.Scroll().GetOffset().y, 100.0f, 0.01f));
    Move(test, Centre(bars.verticalThumb) + Vec2(0.0f, travel * 3.0f));
    Release(test);
    test.Frame();
    REQUIRE(Near(fixture.Scroll().GetOffset().y, 200.0f));
    test.Frame();
    const Fenrir::ScrollbarBoxes moved = fixture.root.GetScrollbars(fixture.view);
    INFO("the thumb follows to the track's end");
    REQUIRE(Near(moved.verticalThumb.y + moved.verticalThumb.height, moved.verticalTrack.y + moved.verticalTrack.height,
                 0.01f));
}

TEST_CASE("A scroll view's content drags past a threshold, and the row pressed sends no click",
          "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    fixture.Scroll().SetOffset({0.0f, 200.0f});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    Press(test, {40.0f, 20.0f});
    test.Frame();
    Move(test, {40.0f, 20.0f + Fenrir::ScrollView::DragThreshold - 2.0f});
    test.Frame();
    INFO("under the threshold nothing scrolls");
    REQUIRE(fixture.Scroll().GetOffset().y == 200.0f);
    Move(test, {40.0f, 80.0f});
    test.Frame();
    INFO("past it the content follows the pointer: 60 down is 60 back");
    REQUIRE(Near(fixture.Scroll().GetOffset().y, 140.0f));
    Release(test);
    test.Frame();
    INFO("the button under the press was not clicked");
    REQUIRE(Of(events, test.app, UIEventKind::Click, fixture.rows[2]).empty());
}

TEST_CASE("A focused scroll view scrolls by the keyboard", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    REQUIRE(fixture.root.Focus(fixture.view));

    test.Type(Key::Down, Scancode::Down);
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset().y == Fenrir::ScrollView::ScrollStep);
    test.Type(Key::End, Scancode::End);
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset().y == 200.0f);
    test.Type(Key::PageUp, Scancode::PageUp);
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset().y == 100.0f);
    test.Type(Key::Home, Scancode::Home);
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset().y == 0.0f);

    INFO("a focused row that does not take the arrows passes them to the view");
    REQUIRE(fixture.root.Focus(fixture.rows[2]));
    test.Type(Key::Down, Scancode::Down);
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset().y == Fenrir::ScrollView::ScrollStep);
}

TEST_CASE("A scroll view scrolls horizontally when its content is wider", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    REQUIRE(fixture.root.SetStyleProperty(fixture.rows[0], "width", "250px"));
    test.Frame();
    REQUIRE(fixture.root.GetScrollRange(fixture.view) == Vec2(150.0f, 200.0f));
    REQUIRE(fixture.root.GetScrollbars(fixture.view).horizontal);

    Move(test, {50.0f, 50.0f});
    test.Feed().Scroll(testWindow, {1.0f, 0.0f});
    test.Frame();
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(Fenrir::ScrollView::ScrollStep, 0.0f));
    test.Frame();
    REQUIRE(Near(fixture.root.GetBoxes(fixture.rows[0]).border.x, -Fenrir::ScrollView::ScrollStep));
}

TEST_CASE("A scroll view's bars take their look from its pseudo-elements", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.SetIdString(fixture.view, "list");
    fixture.root.AddStylesheet("#list::scrollbar { width: 14px; padding: 0; }", "game.css");
    fixture.test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    REQUIRE(Near(bars.verticalTrack, {86.0f, 0.0f, 14.0f, 100.0f}));
    INFO("with no padding on the track the thumb fills its width");
    REQUIRE(Near(bars.verticalThumb.width, 14.0f));

    fixture.root.AddStylesheet("#list::scrollbar { width: 0; }", "hide.css");
    fixture.test.Frame();
    INFO("a bar with no width does not show");
    REQUIRE_FALSE(fixture.root.GetScrollbars(fixture.view).vertical);
}
