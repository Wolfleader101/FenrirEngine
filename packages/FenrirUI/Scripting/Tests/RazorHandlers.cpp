// Razor handlers and two-way binding from the input feed (spec: "Handlers and binding"; ADR-0069): the fixture's
// Mixer.razor, mounted on a root that lays out, takes clicks, a pointer press, a slider drag and typing through
// FenrirInput's test feed, and C++ reads back the fields its handlers and bindings wrote and the elements they changed.
// Input fed before a frame is routed into UI events ahead of the UI's frame, so the Razor dispatcher reads them in
// that frame's build stage, where the handler's Razor component rebuilds: the change shows in the frame it was fed.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* MixerProbeType = "UIScriptFixture.MixerProbe";
    constexpr WindowId window{1};

    /** @brief The fixture with a Mixer mounted on a capturing root, built and laid out. */
    struct MixerScriptingApp : UIScriptingApp
    {
        MixerScriptingApp() : root(MakeRoot(target))
        {
            Call<std::uint64_t>(MixerProbeType, "Mount", root.GetId().value);
            // The first frame builds and lays it out; input routes against the last frame's layout.
            Frame();
        }

        ElementId Find(std::string_view id)
        {
            const ElementId element = root.FindById(id);
            INFO("The Mixer built no element with the id " << id);
            REQUIRE(root.IsAlive(element));
            return element;
        }

        Math::Vec2 Centre(ElementId element)
        {
            const Rect2D border = root.GetBoxes(element).border;
            return {border.x + border.width / 2.0f, border.y + border.height / 2.0f};
        }

        void Press(Math::Vec2 at)
        {
            Feed().MouseMove(window, at, {0.0f, 0.0f});
            Feed().MouseButtonDown(window, MouseButton::Left);
        }

        void Click(Math::Vec2 at)
        {
            Press(at);
            Feed().MouseButtonUp(window, MouseButton::Left);
        }

        /** @brief Runs the frame whose build stage runs the fed input's handlers. */
        void Handle()
        {
            Frame();
        }

        int Count(const char* field)
        {
            return Call<std::int32_t>(MixerProbeType, field);
        }

        float Value(const char* field)
        {
            return Call<float>(MixerProbeType, field);
        }

        std::shared_ptr<CaptureTarget> target;
        UIRoot& root;
    };
} // namespace

TEST_CASE("A click on a button's label runs the button's @onclick once, and its text shows it that frame",
          "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const ElementId label = test.Find("add-label");
    const ElementId count = test.Find("count");
    {
        INFO("The label is the button's child, and the count starts at none");
        REQUIRE(test.root.GetParent(label) == test.Find("add"));
        REQUIRE(test.root.GetText(count) == "Count 0");
    }

    test.Click(test.Centre(label));
    test.Frame();

    INFO("The button's handler ran once, and the count label shows it in the frame the click was fed");
    REQUIRE(test.Count("Count") == 1);
    REQUIRE(test.root.GetText(count) == "Count 1");
    INFO("The row's handler, a sibling's, did not run");
    REQUIRE(test.Count("RowClicks") == 0);
}

TEST_CASE("An Action<UIEvent> handler receives the event's kind and position", "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const Math::Vec2 at = test.Centre(test.Find("cell"));

    test.Press(at);
    test.Handle();

    INFO("The cell's handler was handed the press, at the place it was fed");
    REQUIRE(test.Count("PressedKind") == static_cast<int>(UIEventKind::PointerPress));
    REQUIRE(Near(test.Value("PressedX"), at.x));
    REQUIRE(Near(test.Value("PressedY"), at.y));
}

TEST_CASE("An ancestor's handler runs when nothing nearer handles the kind", "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;

    // The cell handles pointer presses only; its row handles the click.
    test.Click(test.Centre(test.Find("cell")));
    test.Handle();

    INFO("The row's handler took the click once, and the button's did not run");
    REQUIRE(test.Count("RowClicks") == 1);
    REQUIRE(test.Count("Count") == 0);
}

TEST_CASE("A slider dragged by the feed writes its bound field, and @bind:after runs once",
          "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const ElementId volume = test.Find("volume");
    const Rect2D track = test.root.GetBoxes(volume).border;
    auto& slider = dynamic_cast<Slider&>(*test.root.GetElement(volume));
    {
        INFO("The first build pushed the field's value");
        REQUIRE(Near(slider.GetValue(), 0.5f));
    }

    // Pressed and released a quarter of the way along: one change, and the release's closing one at the same value.
    test.Click({track.x + track.width * 0.25f, track.y + track.height / 2.0f});
    test.Handle();

    const float dragged = slider.GetValue();
    {
        INFO("The press moved the slider");
        REQUIRE(Near(dragged, 0.25f));
    }
    INFO("The field holds the slider's value, and @bind:after ran once");
    REQUIRE(test.Value("Volume") == dragged);
    REQUIRE(test.Count("VolumeChanges") == 1);
}

TEST_CASE("A field changed by code moves the slider and sends no Change back", "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const ElementId volume = test.Find("volume");
    auto& slider = dynamic_cast<Slider&>(*test.root.GetElement(volume));
    EventReader<UIEvent> events;
    (void)events.Read(test.app);

    test.Call<void>(MixerProbeType, "SetFields", 0.8f, 0.25f);
    test.Frame();
    test.Frame();

    int changes = 0;
    for (const UIEvent& event : events.Read(test.app))
    {
        changes += event.kind == UIEventKind::Change && event.target == volume ? 1 : 0;
    }
    {
        INFO("Pushing the field sent no Change from the slider");
        REQUIRE(changes == 0);
    }
    INFO("The slider shows the field, which nothing wrote back, and @bind:after did not run");
    REQUIRE(Near(slider.GetValue(), 0.8f));
    REQUIRE(Near(test.Value("Volume"), 0.8f));
    REQUIRE(test.Count("VolumeChanges") == 0);
}

TEST_CASE("A text entry binds both ways", "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const ElementId name = test.Find("name");
    auto& entry = dynamic_cast<TextEntry&>(*test.root.GetElement(name));
    {
        INFO("The first build pushed the field's text");
        REQUIRE(entry.GetValue() == "Ada");
    }

    REQUIRE(test.root.Focus(name));
    test.Feed().Text(window, U"!");
    test.Handle();
    const std::string typed(entry.GetValue());
    {
        INFO("Typing changed the entry, and the field holds its text");
        REQUIRE(typed != "Ada");
        REQUIRE(typed.size() == 4);
        REQUIRE(test.Call<bool>(MixerProbeType, "NameIs", std::string_view(typed)));
    }

    test.Call<void>(MixerProbeType, "SetName", std::string_view("Grace"));
    test.Frame();
    INFO("A name set by code shows in the entry");
    REQUIRE(entry.GetValue() == "Grace");
}

TEST_CASE("A child's @bind-Value round-trips through its parent", "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    const ElementId dial = test.Find("dial");
    const ElementId level = test.Find("level");
    auto& slider = dynamic_cast<Slider&>(*test.root.GetElement(dial));
    const Rect2D track = test.root.GetBoxes(dial).border;
    {
        INFO("The Dial shows the parent's level");
        REQUIRE(Near(slider.GetValue(), 0.25f));
        REQUIRE(test.root.GetText(level) == "Level 0.25");
    }

    test.Click({track.x + track.width * 0.75f, track.y + track.height / 2.0f});
    test.Handle();
    {
        INFO("The Dial's change set the parent's field through ValueChanged, and the parent's label shows it");
        REQUIRE(Near(test.Value("Level"), 0.75f));
        REQUIRE(Near(slider.GetValue(), test.Value("Level")));
        REQUIRE(test.root.GetText(level).starts_with("Level 0.7"));
    }

    test.Call<void>(MixerProbeType, "SetFields", 0.5f, 0.1f);
    test.Frame();
    INFO("The parent's field set by code reaches the Dial's slider as its parameter");
    REQUIRE(Near(slider.GetValue(), 0.1f));
}

TEST_CASE("A pointer handler on an element that takes no pointer events warns once in a debug build",
          "[ui][scripting][razor][handlers]")
{
    MixerScriptingApp test;
    test.Frame();
    test.Frame();

#ifdef NDEBUG
    INFO("A release build checks nothing");
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Warn, "takes no pointer events") == 0);
#else
    const std::string bare = "UIScriptFixture.Mixer has @onclick at sequence";
    INFO("The bare panel, with no pointer-events: all, warned once; the button and the styled row and cell did not");
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Warn, "on a <panel> whose style takes no pointer events") ==
            1);
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Warn, "on a <button>") == 0);
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Warn, bare) == 1);
#endif
}
