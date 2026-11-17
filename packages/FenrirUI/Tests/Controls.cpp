// The controls (spec: "FenrirUI" controls, seam 1): a headless App with FenrirInput's test feed drives buttons,
// checkboxes, sliders, progress bars, images and scroll views, and reads back their values, pseudo-classes, UIEvents
// and rectangles. Each test lays the tree out on one frame and feeds input for the next, since a router hit-tests
// against the last frame's layout. The default stylesheet is on, as a game's root has it.

#include <cstddef>
#include <cstdint>
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
        INFO("the placed element takes its position and size");
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
        INFO("the element was not made as the control its tag names");
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
    {
        INFO("<button> is not made as a Button");
        (void)As<Fenrir::Button>(root, root.Create(parent, "button"));
    }
    {
        INFO("<checkbox> is not made as a Checkbox");
        (void)As<Fenrir::Checkbox>(root, root.Create(parent, "checkbox"));
    }
    {
        INFO("<slider> is not made as a Slider");
        (void)As<Fenrir::Slider>(root, root.Create(parent, "slider"));
    }
    {
        INFO("<progress> is not made as a ProgressBar");
        (void)As<Fenrir::ProgressBar>(root, root.Create(parent, "progress"));
    }
    {
        INFO("<image> is not made as an ImageView");
        (void)As<Fenrir::ImageView>(root, root.Create(parent, "image"));
    }
    {
        INFO("<scroll> is not made as a ScrollView");
        (void)As<Fenrir::ScrollView>(root, root.Create(parent, "scroll"));
    }
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
    INFO("the button takes the focus before it is disabled");
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

TEST_CASE("A slider dragged reports the value under the pointer snapped to its step", "[ui][controls]")
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
    {
        INFO("the press did not set the value where it lands: a tenth of the way is 10, with one change of 10");
        REQUIRE(slider.GetValue() == 10.0f);
        std::vector<UIEvent> changes = Of(events, test.app, UIEventKind::Change, element);
        REQUIRE(changes.size() == 1);
        REQUIRE(changes[0].value == 10.0f);
    }
    Move(test, {103.0f, 25.0f});
    test.Frame();
    {
        INFO("dragged to 103 of 200 is 51.5, which should snap to 50 with one change while dragging (integer 0)");
        REQUIRE(slider.GetValue() == 50.0f);
        std::vector<UIEvent> changes = Of(events, test.app, UIEventKind::Change, element);
        REQUIRE(changes.size() == 1);
        REQUIRE(changes[0].value == 50.0f);
        REQUIRE(changes[0].integer == 0);
    }

    Move(test, {500.0f, 25.0f});
    Release(test);
    test.Frame();
    INFO("dragged past its end it should stop at max, and the release send its final change (integer 1)");
    REQUIRE(slider.GetValue() == 100.0f);
    std::vector<UIEvent> changes = Of(events, test.app, UIEventKind::Change, element);
    REQUIRE(changes.size() == 2);
    REQUIRE(changes.back().value == 100.0f);
    REQUIRE(changes.back().integer == 1);
}

TEST_CASE("Arrow keys step a slider its press focused by its step", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 10.0f, 200.0f, 20.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 100.0f);
    slider.SetStep(10.0f);
    test.Frame();
    Press(test, {20.0f, 20.0f});
    test.Frame();
    Move(test, {500.0f, 25.0f});
    Release(test);
    test.Frame();
    {
        INFO("dragged past its end the slider is not at its max of 100");
        REQUIRE(slider.GetValue() == 100.0f);
    }

    {
        INFO("the press did not focus the slider");
        REQUIRE(root.GetFocused() == element);
    }
    test.Type(Key::Left, Scancode::Left);
    test.Frame();
    {
        INFO("Left did not step it down by 10");
        REQUIRE(slider.GetValue() == 90.0f);
    }
    test.Type(Key::Right, Scancode::Right);
    test.Frame();
    {
        INFO("Right did not step it back up by 10");
        REQUIRE(slider.GetValue() == 100.0f);
    }
    test.Type(Key::Down, Scancode::Down);
    test.Frame();
    INFO("Down did not step it down by 10");
    REQUIRE(slider.GetValue() == 90.0f);
}

TEST_CASE("A wheel notch towards the user over a focused slider steps it down", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 10.0f, 200.0f, 20.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 100.0f);
    slider.SetStep(10.0f);
    slider.SetValue(90.0f);
    test.Frame();
    {
        INFO("the slider could not be focused");
        REQUIRE(root.Focus(element));
    }

    Move(test, {100.0f, 20.0f});
    test.Feed().Scroll(testWindow, {0.0f, -1.0f});
    test.Frame();
    INFO("a wheel notch towards the user did not step it down from 90 to 80");
    REQUIRE(slider.GetValue() == 80.0f);
}

TEST_CASE("A slider's thumb is placed at its value", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 10.0f, 200.0f, 20.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 100.0f);
    slider.SetStep(10.0f);
    slider.SetValue(80.0f);
    test.Frame();

    INFO("its thumb is not at the value, 80% along (the default stylesheet's transform centres it there)");
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
    INFO("the checked checkbox lays out a mark to click");
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
    INFO("the filler that makes the scroll view scroll takes its height");
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
    INFO("a slider set vertical carries .vertical");
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

    INFO("a value of 10 in a range of 0 to 40 is a quarter of the track");
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

TEST_CASE("An image with no size measures its asset's size under each fit", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Fenrir::AssetHandle<Fenrir::Image> picture = SolidImage(test, 40, 30);
    for (const Fenrir::ImageFit fit : {Fenrir::ImageFit::Stretch, Fenrir::ImageFit::Contain, Fenrir::ImageFit::Cover})
    {
        INFO("fit " << static_cast<int>(fit));
        const ElementId element = root.Create(root.GetRootElement(), "image");
        {
            INFO("the image's align-self was not set");
            REQUIRE(root.SetStyleProperty(element, "align-self", "flex-start"));
        }
        As<Fenrir::ImageView>(root, element).SetFit(fit);
        As<Fenrir::ImageView>(root, element).SetSource(picture);
        test.Frame();
        INFO("with no size given it is not the image's 40 by 30");
        const Fenrir::Rect2D content = root.GetBoxes(element).content;
        REQUIRE(Near(content.width, 40.0f));
        REQUIRE(Near(content.height, 30.0f));
        root.Remove(element, Fenrir::RemoveMode::Immediate);
    }
}

TEST_CASE("An image given only a width takes its height from its asset's aspect", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Fenrir::AssetHandle<Fenrir::Image> picture = SolidImage(test, 40, 30);
    const ElementId wide = root.Create(root.GetRootElement(), "image");
    {
        INFO("the image's width and align-self were not set");
        REQUIRE(root.SetStyleProperty(wide, "width", "80px"));
        REQUIRE(root.SetStyleProperty(wide, "align-self", "flex-start"));
    }
    As<Fenrir::ImageView>(root, wide).SetSource(picture);
    test.Frame();
    INFO("given only a width of 80, a 40 by 30 image's height is not 60");
    REQUIRE(Near(root.GetBoxes(wide).content.height, 60.0f));
}

TEST_CASE("A contained image draws at its fit, centred in its box", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    INFO("a 40 by 20 image contained in a 100 square should draw 100 by 50, centred");
    const ElementId boxed = Place(root, root.GetRootElement(), "image", 0.0f, 0.0f, 100.0f, 100.0f);
    As<Fenrir::ImageView>(root, boxed).SetFit(Fenrir::ImageFit::Contain);
    As<Fenrir::ImageView>(root, boxed).SetSource(SolidImage(test, 40, 20));
    test.Frame();
    const Fenrir::DrawList2D& list = target->lists.back();
    std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
    std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
    {
        INFO("the image is not drawn as one quad");
        REQUIRE(vertices.size() == 4);
    }
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

TEST_CASE("A covering image is drawn under its ancestor's rounded overflow clip", "[ui][controls]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    // A round frame, as a minimap's, holding a 40 by 20 image covering its 100 square: drawn 200 by 100 at x -50.
    const ElementId frame = Place(root, root.GetRootElement(), "panel", 0.0f, 0.0f, 100.0f, 100.0f);
    INFO("the round frame and its image take their style");
    REQUIRE(root.SetStyleProperty(frame, "overflow", "hidden"));
    REQUIRE(root.SetStyleProperty(frame, "border-radius", "50px"));
    const ElementId image = root.Create(frame, "image");
    REQUIRE(root.SetStyleProperty(image, "width", "100%"));
    REQUIRE(root.SetStyleProperty(image, "height", "100%"));
    As<Fenrir::ImageView>(root, image).SetFit(Fenrir::ImageFit::Cover);
    As<Fenrir::ImageView>(root, image).SetSource(SolidImage(test, 40, 20));
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
    std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
    INFO("the frame has no background, so the image is the one quad");
    REQUIRE(vertices.size() == 4);
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("vertex at (" << vertex.position.x << ", " << vertex.position.y << "), uv (" << vertex.uv.x << ", "
                           << vertex.uv.y << ")");
        INFO("the quad is painted with the image");
        REQUIRE(vertex.kinds[1] == static_cast<std::uint8_t>(Fenrir::PaintKind::Image));
        INFO("the image is clipped per pixel by the frame's round clip, not a rectangle of its own: radii "
             << vertex.clipRadii.x << ", " << vertex.clipRadii.y << ", " << vertex.clipRadii.z << ", "
             << vertex.clipRadii.w);
        REQUIRE((vertex.kinds[2] & Fenrir::PainterFlags::Clipped) != 0);
        REQUIRE(Near(vertex.clipRadii.x, 50.0f));
        REQUIRE(Near(vertex.clipRadii.z, 50.0f));
        INFO("it draws over its box and the quad's one-unit margin, not the whole 200-wide image");
        REQUIRE(vertex.position.x >= -1.5f);
        REQUIRE(vertex.position.x <= 101.5f);
        INFO("its box shows the image's middle half across: u from 0.25 to 0.75, past them only by the margin");
        REQUIRE(vertex.uv.x >= 0.24f);
        REQUIRE(vertex.uv.x <= 0.76f);
    }
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
            INFO("the scroll view and its rows take their style");
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
    INFO("the scroll view can scroll 200 down and not across");
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
    INFO("the scroll view has a vertical bar to drag and no horizontal one");
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
    INFO("the scroll view takes the focus");
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
    {
        INFO("the first row's width was not set");
        REQUIRE(fixture.root.SetStyleProperty(fixture.rows[0], "width", "250px"));
    }
    test.Frame();
    {
        INFO("a 250-wide row in a 100-wide view does not give it 150 to scroll across");
        REQUIRE(fixture.root.GetScrollRange(fixture.view) == Vec2(150.0f, 200.0f));
    }
    {
        INFO("content wider than the view does not show a horizontal bar");
        REQUIRE(fixture.root.GetScrollbars(fixture.view).horizontal);
    }

    Move(test, {50.0f, 50.0f});
    test.Feed().Scroll(testWindow, {1.0f, 0.0f});
    test.Frame();
    {
        INFO("a sideways wheel notch does not scroll it a step across");
        REQUIRE(fixture.Scroll().GetOffset() == Vec2(Fenrir::ScrollView::ScrollStep, 0.0f));
    }
    test.Frame();
    INFO("its rows do not move left by the offset");
    REQUIRE(Near(fixture.root.GetBoxes(fixture.rows[0]).border.x, -Fenrir::ScrollView::ScrollStep));
}

TEST_CASE("A scroll view's bars take their look from its pseudo-elements", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.SetIdString(fixture.view, "list");
    fixture.root.AddStylesheet("#list::scrollbar { width: 14px; padding: 0; }", "game.css");
    fixture.test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    INFO("the vertical track is the sheet's 14 wide down the scroll view's right edge");
    REQUIRE(Near(bars.verticalTrack, {86.0f, 0.0f, 14.0f, 100.0f}));
    INFO("with no padding on the track the thumb fills its width");
    REQUIRE(Near(bars.verticalThumb.width, 14.0f));

    fixture.root.AddStylesheet("#list::scrollbar { width: 0; }", "hide.css");
    fixture.test.Frame();
    INFO("a bar with no width does not show");
    REQUIRE_FALSE(fixture.root.GetScrollbars(fixture.view).vertical);
}

namespace
{
    // The colour of the latest frame's last quad drawn over `box`, or 0 when none is.
    std::uint32_t ColourOver(const CaptureTarget& target, const Fenrir::Rect2D& box)
    {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(target.lists.back());
        std::uint32_t colour = 0;
        for (std::size_t quad = 0; quad < vertices.size() / 4; ++quad)
        {
            if (Near(QuadBox(vertices, quad), box, 0.01f))
            {
                colour = vertices[quad * 4].colour;
            }
        }
        return colour;
    }
} // namespace

TEST_CASE("scrollbar-width thin draws the bar at half the default stylesheet's width, its inset halved too",
          "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    const Fenrir::ScrollbarBoxes before = fixture.root.GetScrollbars(fixture.view);
    INFO("the default stylesheet's ::scrollbar is 10 wide down the view's right edge, its thumb inset 2");
    REQUIRE(Near(before.verticalTrack, {90.0f, 0.0f, 10.0f, 100.0f}));
    REQUIRE(Near(before.verticalThumb.width, 6.0f));

    {
        INFO("the scroll view takes scrollbar-width");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-width", "thin"));
    }
    fixture.test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    INFO("thin is half of it, 5 wide: the track is " << bars.verticalTrack.width << " wide at x "
                                                     << bars.verticalTrack.x);
    REQUIRE(Near(bars.verticalTrack, {95.0f, 0.0f, 5.0f, 100.0f}));
    INFO("its thumb is inset half the 2, so 3 wide, not " << bars.verticalThumb.width);
    REQUIRE(Near(bars.verticalThumb.width, 3.0f));
    INFO("the track draws at that width");
    REQUIRE(ColourOver(*fixture.target, bars.verticalTrack) != 0u);
}

TEST_CASE("scrollbar-width thin wins over a ::scrollbar rule's width", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.SetIdString(fixture.view, "list");
    fixture.root.AddStylesheet("#list::scrollbar { width: 14px; }", "game.css");
    {
        INFO("the scroll view takes scrollbar-width");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-width", "thin"));
    }
    fixture.test.Frame();
    const float width = fixture.root.GetScrollbars(fixture.view).verticalTrack.width;
    INFO("the bar is thin's 5 wide, not the rule's 14: " << width);
    REQUIRE(Near(width, 5.0f));
}

TEST_CASE("scrollbar-width none draws and hits no bar, and the wheel still scrolls the view", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UITestApp& test = fixture.test;
    const Fenrir::ScrollbarBoxes shown = fixture.root.GetScrollbars(fixture.view);
    {
        INFO("the scroll view takes scrollbar-width");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-width", "none"));
    }
    test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    INFO("GetScrollbars reports no bar");
    REQUIRE_FALSE(bars.vertical);
    REQUIRE_FALSE(bars.horizontal);
    INFO("no track or thumb draws where the bar was");
    REQUIRE(ColourOver(*fixture.target, shown.verticalTrack) == 0u);
    REQUIRE(ColourOver(*fixture.target, shown.verticalThumb) == 0u);
    INFO("a point where the bar was hits the row under it");
    REQUIRE(fixture.root.HitTest(Centre(shown.verticalThumb)) == fixture.rows[0]);

    Move(test, {50.0f, 50.0f});
    test.Feed().Scroll(testWindow, {0.0f, -1.0f});
    test.Frame();
    INFO("the wheel still scrolls it a step");
    REQUIRE(fixture.Scroll().GetOffset() == Vec2(0.0f, Fenrir::ScrollView::ScrollStep));
}

TEST_CASE("scrollbar-color colours the thumb and the track over the pseudo-elements' rules", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.SetIdString(fixture.view, "list");
    fixture.root.AddStylesheet("#list::scrollbar-thumb { background-color: #00ff00; }", "game.css");
    {
        INFO("the scroll view takes scrollbar-color");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-color", "red blue"));
    }
    fixture.test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    INFO("the thumb is red, its first colour, over the rule's green");
    REQUIRE(ColourOver(*fixture.target, bars.verticalThumb) == Rgba8(255, 0, 0, 255));
    INFO("the track is blue, its second colour");
    REQUIRE(ColourOver(*fixture.target, bars.verticalTrack) == Rgba8(0, 0, 255, 255));
    INFO("GetPseudoElementStyle still reads the thumb's own style, the rule's green");
    REQUIRE(
        fixture.root.GetPseudoElementStyle(fixture.view, Fenrir::PseudoElement::ScrollbarThumb)->backgroundColor.g ==
        1.0f);
}

TEST_CASE("scrollbar-color auto leaves the pseudo-elements' colours", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.SetIdString(fixture.view, "list");
    fixture.root.AddStylesheet("#list::scrollbar-thumb { background-color: #00ff00; }", "game.css");
    {
        INFO("the scroll view takes scrollbar-color");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-color", "red blue"));
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-color", "auto"));
    }
    fixture.test.Frame();
    const Fenrir::ScrollbarBoxes bars = fixture.root.GetScrollbars(fixture.view);
    INFO("auto gives the thumb the rule's green");
    REQUIRE(ColourOver(*fixture.target, bars.verticalThumb) == Rgba8(0, 255, 0, 255));
}

TEST_CASE("scrollbar-width and scrollbar-color read their keywords and colours and refuse the rest",
          "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    UIRoot& root = fixture.root;
    for (const char* refused : {"6px", "thick"})
    {
        INFO("scrollbar-width '" << refused << "' is refused");
        REQUIRE_FALSE(root.SetStyleProperty(fixture.view, "scrollbar-width", refused));
    }
    for (const char* refused : {"red", "red blue green", "thin"})
    {
        INFO("scrollbar-color '" << refused << "' is refused");
        REQUIRE_FALSE(root.SetStyleProperty(fixture.view, "scrollbar-color", refused));
    }
    {
        INFO("the scroll view takes both");
        REQUIRE(root.SetStyleProperty(fixture.view, "scrollbar-width", "thin"));
        REQUIRE(root.SetStyleProperty(fixture.view, "scrollbar-color", "rgba(255, 0, 0, 0.5) #0000ff"));
    }
    fixture.test.Frame();
    const Fenrir::ResolvedStyle* view = root.GetResolvedStyle(fixture.view);
    INFO("thin resolves as Thin");
    REQUIRE(view->scrollbarWidth == Fenrir::ScrollbarWidth::Thin);
    INFO("the colours resolve as a half-transparent red thumb and a blue track");
    REQUIRE_FALSE(view->scrollbarColor.isAuto);
    REQUIRE(view->scrollbarColor.thumb.a == 0.5f);
    REQUIRE(view->scrollbarColor.track.b == 1.0f);
}

TEST_CASE("The scrollbar properties are not inherited", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    {
        INFO("the scroll view takes both");
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-width", "none"));
        REQUIRE(fixture.root.SetStyleProperty(fixture.view, "scrollbar-color", "red blue"));
    }
    fixture.test.Frame();
    const Fenrir::ResolvedStyle* row = fixture.root.GetResolvedStyle(fixture.rows[0]);
    INFO("a row of the view keeps auto for both");
    REQUIRE(row->scrollbarWidth == Fenrir::ScrollbarWidth::Auto);
    REQUIRE(row->scrollbarColor.isAuto);
}

TEST_CASE("The scrollbar properties' typed setters give them", "[ui][controls][scroll]")
{
    ScrollFixture fixture;
    fixture.root.EditInlineStyle(fixture.view)
        ->SetScrollbarWidth(Fenrir::ScrollbarWidth::None)
        .SetScrollbarColor(Fenrir::ScrollbarColor::Of(Fenrir::Colour::White, Fenrir::Colour::Black));
    fixture.test.Frame();
    const Fenrir::ResolvedStyle* view = fixture.root.GetResolvedStyle(fixture.view);
    INFO("SetScrollbarWidth gives none");
    REQUIRE(view->scrollbarWidth == Fenrir::ScrollbarWidth::None);
    INFO("SetScrollbarColor gives a white thumb and a black track");
    REQUIRE(view->scrollbarColor.thumb.g == 1.0f);
    REQUIRE(view->scrollbarColor.track.g == 0.0f);
}
