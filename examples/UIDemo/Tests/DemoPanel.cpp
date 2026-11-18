#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoApp.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/UIRoot.hpp"

// UIDemo on the demo UI kit (DemoApp.hpp): the panel's "UIDemo" section, the room's key rows in the help, and the
// room's demo tutorial.
namespace
{
    using UIDemoTest::DemoApp;
    using UIDemoTest::RoomTutorial;

    // More than the menu's panels' intro lasts.
    constexpr int IntroFrames = 30;

    // The kit's switch row of class `kind` whose label reads `label`, or nothing.
    Fenrir::ElementId KitRow(DemoApp& demo, std::string_view kind, std::string_view label)
    {
        const Fenrir::UIRoot& root = demo.Kit().Root();
        for (const Fenrir::ElementId row : root.FindByClass(kind))
        {
            for (const Fenrir::ElementId child : root.GetChildren(row))
            {
                if (root.HasClass(child, "demo-row-label") && root.GetText(child) == label)
                {
                    return row;
                }
            }
        }
        return {};
    }

    // The checkbox of the kit's toggle row labelled `label`.
    Fenrir::ElementId Checkbox(DemoApp& demo, std::string_view label)
    {
        const Fenrir::UIRoot& root = demo.Kit().Root();
        const Fenrir::ElementId row = KitRow(demo, "demo-toggle", label);
        INFO("the panel has no toggle row labelled " << label);
        REQUIRE(row.IsValid());
        Fenrir::ElementId box;
        for (const Fenrir::ElementId child : root.GetChildren(row))
        {
            if (root.HasClass(child, "demo-checkbox"))
            {
                box = child;
            }
        }
        INFO("the toggle row " << label << " has no checkbox");
        REQUIRE(box.IsValid());
        return box;
    }

    // The value of the kit's readout labelled `label`, or "(none)".
    std::string Readout(DemoApp& demo, std::string_view label)
    {
        const Fenrir::UIRoot& root = demo.Kit().Root();
        for (const Fenrir::ElementId row : root.FindByClass("demo-readout"))
        {
            const auto children = root.GetChildren(row);
            if (children.size() == 2 && root.GetText(children[0]) == label)
            {
                return std::string(root.GetText(children[1]));
            }
        }
        return "(none)";
    }

    bool Contains(const std::vector<std::string>& texts, std::string_view text)
    {
        return std::ranges::find(texts, text) != texts.end();
    }

    // The step title the tutorial card shows, or "(no card)" while no card is there or it is going out.
    std::string ShownStep(DemoApp& demo)
    {
        const Fenrir::UIRoot& root = demo.Kit().Root();
        const std::vector<Fenrir::ElementId> cards = root.FindByClass("demo-tutorial");
        const bool live = std::ranges::any_of(
            cards, [&](Fenrir::ElementId card) { return !root.HasPseudoClass(card, Fenrir::PseudoClass::Outro); });
        if (!live)
        {
            return "(no card)";
        }
        const std::vector<std::string> titles = demo.KitTexts("demo-tutorial-step-title");
        return titles.empty() ? "(no step title)" : titles.back();
    }

    // Clicks the tutorial card's Next button.
    void ClickNext(DemoApp& demo)
    {
        const Fenrir::UIRoot& root = demo.Kit().Root();
        const std::vector<Fenrir::ElementId> next = root.FindByClass("demo-tutorial-next");
        INFO("the card has no Next button");
        REQUIRE(next.size() == 1);
        demo.Click(DemoApp::Centre(root, next.front()));
    }

    // The volume and field of view as the panel shows them, from the menu's settings.
    std::string VolumeText(float volume)
    {
        return std::to_string(static_cast<int>(std::lround(volume))) + " (no audio yet)";
    }
    std::string FieldOfViewText(float degrees)
    {
        return std::to_string(static_cast<int>(std::lround(degrees))) + " degrees";
    }
} // namespace

TEST_CASE("The demo panel's UIDemo section shows the HUD's and the menu's stylesheets and the settings")
{
    DemoApp demo;
    demo.Frames(2);
    {
        INFO("the demo logged errors: " << (demo.errors->empty() ? "" : demo.errors->front()));
        REQUIRE(demo.errors->empty());
    }
    {
        INFO("the panel has no UIDemo section");
        REQUIRE(Contains(demo.KitTexts("demo-section-title"), "UIDemo"));
    }
    const std::vector<std::string> lines = demo.KitTexts("demo-text");
    {
        INFO("the panel does not show the HUD's stylesheet " << demo.app.GetResource<UIDemo::Hud>().StylesheetPath());
        REQUIRE(Contains(lines, demo.app.GetResource<UIDemo::Hud>().StylesheetPath()));
    }
    {
        INFO("the panel does not show the menu's stylesheet " << demo.Menu().StylesheetPath());
        REQUIRE(Contains(lines, demo.Menu().StylesheetPath()));
    }
    // The paths are longer than the panel is wide, so they wrap inside it: each is within the panel and at least two
    // lines high. Content boxes, so a row's padding is not counted as a line; the paths' length is the checkout's, so
    // only "wrapped at least once" holds everywhere.
    const Fenrir::UIRoot& kit = demo.Kit().Root();
    const Fenrir::Rect2D panel = kit.GetBoxes(kit.FindById("demo-panel")).content;
    const std::vector<Fenrir::ElementId> labels = kit.FindByClass("demo-readout-label");
    {
        INFO("the panel has no readout, so no label line to measure a wrapped path against");
        REQUIRE_FALSE(labels.empty());
    }
    // The shortest label is one line: a long one wraps in its 140-unit column.
    float lineHeight = kit.GetBoxes(labels.front()).content.height;
    for (const Fenrir::ElementId label : labels)
    {
        lineHeight = std::min(lineHeight, kit.GetBoxes(label).content.height);
    }
    for (const Fenrir::ElementId line : kit.FindByClass("demo-text"))
    {
        const Fenrir::ElementBoxes boxes = kit.GetBoxes(line);
        {
            INFO("the line " << kit.GetText(line) << " runs past the panel's right edge");
            REQUIRE(boxes.border.x + boxes.border.width <= panel.x + panel.width + 0.5f);
        }
        {
            INFO("the line " << kit.GetText(line) << " is not wrapped: its text is " << boxes.content.height
                             << " high, one line " << lineHeight);
            REQUIRE(boxes.content.height >= lineHeight * 1.9f);
        }
    }
    // The settings' defaults: a volume of 80 and a field of view of 60 degrees.
    {
        INFO("the volume reads " << Readout(demo, "Volume"));
        REQUIRE(Readout(demo, "Volume") == "80 (no audio yet)");
    }
    {
        INFO("the field of view reads " << Readout(demo, "Field of view"));
        REQUIRE(Readout(demo, "Field of view") == "60 degrees");
    }
}

TEST_CASE("The demo panel's hot reload toggles turn the HUD's and the menu's reloading off and on")
{
    DemoApp demo;
    demo.Frames(2);
    Fenrir::UIRoot& hud = demo.HudRoot();
    Fenrir::UIRoot& menu = demo.Menu().Root();
    INFO("the HUD starts with hot reload off");
    REQUIRE(hud.IsHotReloadOn());
    INFO("the menu starts with hot reload off");
    REQUIRE(menu.IsHotReloadOn());

    demo.Click(DemoApp::Centre(demo.Kit().Root(), Checkbox(demo, "HUD hot reload")));
    INFO("the HUD's toggle did not turn its hot reload off");
    REQUIRE_FALSE(hud.IsHotReloadOn());
    INFO("the HUD's toggle turned the menu's hot reload off");
    REQUIRE(menu.IsHotReloadOn());

    demo.Click(DemoApp::Centre(demo.Kit().Root(), Checkbox(demo, "Menu hot reload")));
    INFO("the menu's toggle did not turn its hot reload off");
    REQUIRE_FALSE(menu.IsHotReloadOn());

    demo.Click(DemoApp::Centre(demo.Kit().Root(), Checkbox(demo, "HUD hot reload")));
    demo.Click(DemoApp::Centre(demo.Kit().Root(), Checkbox(demo, "Menu hot reload")));
    INFO("the HUD's toggle did not turn its hot reload on again");
    REQUIRE(hud.IsHotReloadOn());
    INFO("the menu's toggle did not turn its hot reload on again");
    REQUIRE(menu.IsHotReloadOn());
}

TEST_CASE("The demo panel's volume and field of view follow the menu's settings")
{
    DemoApp demo;
    demo.Frames(2);
    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(IntroFrames);
    Fenrir::UIRoot& root = demo.Menu().Root();
    demo.Click(DemoApp::Centre(root, root.FindById("settings")));
    INFO("Settings did not open the settings panel");
    REQUIRE(root.FindById("settings-panel").IsValid());

    const float volume = demo.Menu().GetSettings().volume;
    const float fieldOfView = demo.Menu().GetSettings().fieldOfView;
    INFO("the volume slider does not take the focus");
    REQUIRE(root.Focus(root.FindById("volume")));
    demo.Tap(Fenrir::Key::Left, Fenrir::Scancode::Left);
    demo.Frames(2);
    INFO("the field of view slider does not take the focus");
    REQUIRE(root.Focus(root.FindById("field-of-view")));
    demo.Tap(Fenrir::Key::Right, Fenrir::Scancode::Right);
    demo.Frames(2);
    INFO("Left on the volume slider did not lower the volume");
    REQUIRE(demo.Menu().GetSettings().volume < volume);
    INFO("Right on the field of view slider did not widen the field of view");
    REQUIRE(demo.Menu().GetSettings().fieldOfView > fieldOfView);
    INFO("the volume reads " << Readout(demo, "Volume"));
    REQUIRE(Readout(demo, "Volume") == VolumeText(demo.Menu().GetSettings().volume));
    INFO("the field of view reads " << Readout(demo, "Field of view"));
    REQUIRE(Readout(demo, "Field of view") == FieldOfViewText(demo.Menu().GetSettings().fieldOfView));
}

TEST_CASE("The kit's panel and help cover none of the HUD's parts in a 1920 by 1080 window")
{
    DemoApp demo;
    demo.Frames(2);
    const Fenrir::UIRoot& kit = demo.Kit().Root();
    const Fenrir::UIRoot& hud = demo.HudRoot();
    const auto overlap = [](const Fenrir::Rect2D& a, const Fenrir::Rect2D& b) {
        return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
    };
    for (const std::string_view box : {"demo-panel", "demo-help"})
    {
        const Fenrir::Rect2D kitBox = kit.GetBoxes(kit.FindById(box)).border;
        for (const std::string_view part : {"vitals", "ammo-box", "minimap-frame", "compass"})
        {
            const Fenrir::Rect2D hudBox = hud.GetBoxes(hud.FindById(part)).border;
            INFO("the kit's #" << box << " covers the HUD's #" << part);
            REQUIRE_FALSE(overlap(kitBox, hudBox));
        }
    }
}

TEST_CASE("The help lists the room's keys")
{
    DemoApp demo;
    demo.Frames(2);
    const std::vector<std::string> keys = demo.KitTexts("demo-keys");
    for (const std::string_view key : {"F / left click", "R", "H", "Escape"})
    {
        INFO("the help has no row for " << key);
        REQUIRE(Contains(keys, key));
    }
}

TEST_CASE("The room's tutorial shows on its first entry and steps on a shot, a reload and a hit")
{
    DemoApp demo(RoomTutorial::On);
    demo.Frames(2);
    INFO("the tutorial did not show on the room's first entry");
    REQUIRE(demo.Kit().IsTutorialShown());
    REQUIRE(ShownStep(demo) == "Fire");

    demo.Tap(Fenrir::Key::F, Fenrir::Scancode::F);
    demo.Frames(2);
    INFO("a shot did not end the Fire step");
    REQUIRE(ShownStep(demo) == "Reload");
    demo.Tap(Fenrir::Key::R, Fenrir::Scancode::R);
    demo.Frames(2);
    INFO("a reload did not end the Reload step");
    REQUIRE(ShownStep(demo) == "Take a hit");
    demo.Tap(Fenrir::Key::H, Fenrir::Scancode::H);
    demo.Frames(2);
    INFO("a hit did not end the Take a hit step");
    REQUIRE(ShownStep(demo) == "The light terminal");
    ClickNext(demo);
    INFO("Next did not step on to the pause menu");
    REQUIRE(ShownStep(demo) == "The pause menu");
}

TEST_CASE("The Escape that closes the room's tutorial does not open the pause menu")
{
    DemoApp demo(RoomTutorial::On);
    demo.Frames(2);
    INFO("the room's tutorial does not show on its first entry");
    REQUIRE(demo.Kit().IsTutorialShown());
    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(2);
    INFO("Escape did not close the tutorial");
    REQUIRE_FALSE(demo.Kit().IsTutorialShown());
    INFO("the Escape that closed the tutorial opened the menu");
    REQUIRE_FALSE(demo.Menu().IsOpen());

    demo.Tap(Fenrir::Key::Escape, Fenrir::Scancode::Escape);
    demo.Frames(2);
    INFO("the next Escape did not open the menu");
    REQUIRE(demo.Menu().IsOpen());
}
