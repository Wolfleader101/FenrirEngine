// UI listeners (ADR-0065; spec: "FenrirUI" events, seam 1): C++ per element per kind, run on the target then bubbling
// to the root with stop-propagation, while the engine event is sent once at the target.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirUI/UIEvent.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIListenerContext;
using Fenrir::UIRoot;
using namespace UITest;

namespace
{
    // A panel taking the pointer with a button in its top-left corner, the button inheriting pointer-events.
    struct Nested
    {
        ElementId panel;
        ElementId button;
    };

    Nested MakeNested(UIRoot& root)
    {
        Nested nested;
        nested.panel = root.Create(root.GetRootElement(), "div");
        root.SetStyleProperty(nested.panel, "width", "200px");
        root.SetStyleProperty(nested.panel, "height", "200px");
        root.SetStyleProperty(nested.panel, "pointer-events", "all");
        nested.button = root.Create(nested.panel, "button");
        root.SetStyleProperty(nested.button, "width", "50px");
        root.SetStyleProperty(nested.button, "height", "50px");
        return nested;
    }

    std::size_t CountClicks(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app)
    {
        std::size_t clicks = 0;
        for (const UIEvent& event : reader.Read(app))
        {
            clicks += event.kind == UIEventKind::Click ? 1 : 0;
        }
        return clicks;
    }
} // namespace

TEST_CASE("A click runs the target's listener, then its parent's", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    std::vector<std::string> ran;
    root.AddListener(nested.panel, UIEventKind::Click, [&](UIListenerContext& context) {
        INFO("the parent's listener sees the button as the target and itself as current");
        REQUIRE(context.GetEvent().target == nested.button);
        REQUIRE(context.GetCurrent() == nested.panel);
        ran.emplace_back("panel");
    });
    root.AddListener(nested.button, UIEventKind::Click, [&](UIListenerContext&) { ran.emplace_back("button"); });
    root.AddListener(nested.button, UIEventKind::PointerPress, [&](UIListenerContext&) { ran.emplace_back("press"); });
    test.Frame();

    test.Click({25.0f, 25.0f});
    test.Frame();

    INFO("the press listener ran for the press; for the click the button's ran, then the panel's");
    REQUIRE(ran == std::vector<std::string>{"press", "button", "panel"});
}

TEST_CASE("Stop-propagation on the target keeps its parent's listener from running", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    std::vector<std::string> ran;
    root.AddListener(nested.panel, UIEventKind::Click, [&](UIListenerContext&) { ran.emplace_back("panel"); });
    root.AddListener(nested.button, UIEventKind::Click, [&](UIListenerContext& context) {
        ran.emplace_back("button");
        context.StopPropagation();
    });
    root.AddListener(nested.button, UIEventKind::Click, [&](UIListenerContext&) { ran.emplace_back("button again"); });
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({25.0f, 25.0f});
    test.Frame();

    INFO("the button's listeners both ran, the panel's did not");
    REQUIRE(ran == std::vector<std::string>{"button", "button again"});
    INFO("the engine event was sent once, at the button, stopped or not");
    REQUIRE(CountClicks(events, test.app) == 1);
}

TEST_CASE("The engine event is sent once however many listeners it bubbles through", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    int heard = 0;
    for (const ElementId element : {nested.button, nested.panel, root.GetRootElement()})
    {
        root.AddListener(element, UIEventKind::Click, [&](UIListenerContext&) { ++heard; });
    }
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({25.0f, 25.0f});
    test.Frame();

    INFO("three listeners heard it, and the queue has it once");
    REQUIRE(heard == 3);
    REQUIRE(CountClicks(events, test.app) == 1);
}

TEST_CASE("A removed listener no longer runs", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    int heard = 0;
    const Fenrir::UIListenerId id =
        root.AddListener(nested.button, UIEventKind::Click, [&](UIListenerContext&) { ++heard; });
    test.Frame();

    INFO("removing it works once");
    REQUIRE(root.RemoveListener(id));
    REQUIRE_FALSE(root.RemoveListener(id));
    test.Click({25.0f, 25.0f});
    test.Frame();
    REQUIRE(heard == 0);
}

TEST_CASE("A listener may remove its own element as the event bubbles", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    bool panelHeard = false;
    root.AddListener(nested.button, UIEventKind::Click,
                     [&](UIListenerContext& context) { root.Remove(context.GetCurrent()); });
    root.AddListener(nested.panel, UIEventKind::Click, [&](UIListenerContext&) { panelHeard = true; });
    test.Frame();

    test.Click({25.0f, 25.0f});
    test.Frame();

    INFO("the button is gone and the click still bubbled to the panel");
    REQUIRE_FALSE(root.IsAlive(nested.button));
    REQUIRE(panelHeard);
}

TEST_CASE("Code can send a UI event through the listeners and the queue", "[ui][listeners]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Nested nested = MakeNested(root);
    float heard = 0.0f;
    root.AddListener(nested.panel, UIEventKind::Change,
                     [&](UIListenerContext& context) { heard = context.GetEvent().value; });
    Fenrir::EventReader<UIEvent> events;

    UIEvent change;
    change.target = nested.button;
    change.kind = UIEventKind::Change;
    change.value = 0.75f;
    root.SendEvent(change);

    INFO("the panel heard the change bubble up, and the queue has it with the root filled in");
    REQUIRE(Near(heard, 0.75f));
    const std::vector<UIEvent> read = ReadAll(events, test.app);
    REQUIRE(read.size() == 1);
    REQUIRE(read[0].root == root.GetId());
}
