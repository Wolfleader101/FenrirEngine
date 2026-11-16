// Razor components from C# (spec: "Components, mounting and the dispatcher", "The builder"; ADR-0067, ADR-0068): the
// fixture's Scoreboard.razor, compiled by the stock SDK's Razor generator against the stand-in, is mounted from a
// system's OnCreate on a root C++ made before the script loaded, and its first build in Fenrir.UI.Build makes
// elements C++ reads back as tags, classes, inline style and text. A script reload removes it at once, the released
// generation is collected, and the new generation mounts it again.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirUI/Style.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* RazorProbeType = "UIScriptFixture.RazorProbe";
    // The overlay layer the fixture's RazorMounter finds its root at (RazorMounting.cs).
    constexpr std::int32_t RazorLayer = 7;

    /**
     * @brief The UI packages and, made before scripting loads the fixture, a root on window 1's overlay at RazorLayer.
     * With no window it lays out nothing, but its elements, classes, inline style and text are all there.
     */
    struct RazorPackages : UIPackages
    {
        explicit RazorPackages(App& app) : UIPackages(app)
        {
            UIRootConfig config;
            config.window = WindowId{1};
            config.layer = RazorLayer;
            root = &Roots().CreateRoot(config);
        }

        UIRoot* root = nullptr;
    };

    struct RazorScriptingApp : ExtensionTestApp<RazorPackages>
    {
        RazorScriptingApp() : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_UI_FIXTURE"), UIExtensions())
        {
        }

        /** @brief The host element of the Scoreboard the loaded generation mounted. */
        ElementId Host()
        {
            return Unpacked(Call<std::uint64_t>(RazorProbeType, "Host"));
        }
    };

    constexpr const char* RebuildProbeType = "UIScriptFixture.RebuildProbe";
    // The overlay layer the fixture's RebuildMounter finds its root at (RazorRebuilds.cs).
    constexpr std::int32_t RebuildLayer = 8;

    /** @brief The UI packages and a root on window 1's overlay at RebuildLayer, made before scripting loads. */
    struct RebuildPackages : UIPackages
    {
        explicit RebuildPackages(App& app) : UIPackages(app)
        {
            UIRootConfig config;
            config.window = WindowId{1};
            config.layer = RebuildLayer;
            root = &Roots().CreateRoot(config);
        }

        UIRoot* root = nullptr;
    };

    /** @brief The fixture with a Ticker and a Fragile mounted from OnCreate (RebuildMounter). */
    struct RebuildScriptingApp : ExtensionTestApp<RebuildPackages>
    {
        RebuildScriptingApp() : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_UI_FIXTURE"), UIExtensions())
        {
        }

        int Builds()
        {
            return Call<std::int32_t>(RebuildProbeType, "Builds");
        }

        std::string TickerText()
        {
            return std::string(root->GetText(root->FindById("ticker")));
        }

        std::string FragileText()
        {
            return std::string(root->GetText(root->FindById("fragile")));
        }
    };

    bool HasClasses(const UIRoot& root, ElementId element, std::string_view first, std::string_view second)
    {
        return root.HasClass(element, first) && root.HasClass(element, second);
    }
} // namespace

TEST_CASE("A Razor component mounted from OnCreate is on its root after the first frame", "[ui][scripting][razor]")
{
    RazorScriptingApp test;
    UIRoot& root = *test.root;
    const ElementId host = test.Host();
    {
        INFO("Mounting made the host under the root element, with the Razor component's readable class");
        REQUIRE(root.IsAlive(host));
        REQUIRE(root.GetParent(host) == root.GetRootElement());
        REQUIRE(root.HasClass(host, "scoreboard"));
    }
    {
        INFO("Its markup builds in the build stage, not at the mount");
        REQUIRE(!root.IsAlive(root.FindById("board")));
    }

    test.Frame();

    const ElementId board = root.FindById("board");
    {
        INFO("The first frame built the panel under the host, with its classes");
        REQUIRE(root.IsAlive(board));
        REQUIRE(root.GetParent(board) == host);
        REQUIRE(root.GetTag(board) == "panel");
        REQUIRE(HasClasses(root, board, "board", "dark"));
    }
    {
        INFO("Its inline style holds both declarations, the width from a field");
        const StyleDeclarations* style = root.GetInlineStyle(board);
        REQUIRE(style != nullptr);
        REQUIRE(style->Values().width == Length::Px(200.0f));
        REQUIRE(style->Values().height == Length::Px(40.0f));
    }
    const ElementId score = root.FindById("score");
    const ElementId go = root.FindById("go");
    {
        INFO("The label and the button are the panel's children, in markup order");
        REQUIRE(root.GetChildren(board).size() == 2);
        REQUIRE(root.GetChildren(board)[0] == score);
        REQUIRE(root.GetChildren(board)[1] == go);
        REQUIRE(root.GetTag(score) == "label");
        REQUIRE(root.GetTag(go) == "button");
        REQUIRE(root.HasClass(score, "score"));
        REQUIRE(root.HasClass(go, "go"));
    }
    {
        INFO("The label's text joins its literal and the field the mount set; the button's is a field's");
        REQUIRE(root.GetText(score) == "Score: 42");
        REQUIRE(root.GetText(go) == "Go");
    }
}

TEST_CASE("A script reload removes a Razor component's elements at once", "[ui][scripting][razor]")
{
    RazorScriptingApp test;
    UIRoot& root = *test.root;
    test.Frame();
    const ElementId oldHost = test.Host();
    const ElementId oldBoard = root.FindById("board");
    {
        INFO("The first frame did not build the Razor component");
        REQUIRE(root.IsAlive(oldBoard));
    }

    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    INFO("The old generation's host and its elements are gone before any frame, with no outro");
    REQUIRE(!root.IsAlive(oldHost));
    REQUIRE(!root.IsAlive(oldBoard));
}

TEST_CASE("A script generation with a mounted Razor component is collected when released", "[ui][scripting][razor]")
{
    RazorScriptingApp test;
    test.Frame();
    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    INFO("The released generation was not collected: the Razor runtime still holds something of it");
    REQUIRE(test.scripting.PendingUnloadCount() == 0);
}

TEST_CASE("After a script reload the new generation mounts its Razor component again", "[ui][scripting][razor]")
{
    RazorScriptingApp test;
    UIRoot& root = *test.root;
    test.Frame();
    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }

    const ElementId newHost = test.Host();
    {
        INFO("The new generation's OnCreate mounted a host again, the only child of the root element");
        REQUIRE(root.IsAlive(newHost));
        REQUIRE(root.GetChildren(root.GetRootElement()).size() == 1);
    }
    test.Frame();
    const ElementId board = root.FindById("board");
    INFO("Its first frame built the Razor component under the new host");
    REQUIRE(root.IsAlive(board));
    REQUIRE(root.GetParent(board) == newHost);
    REQUIRE(root.GetText(root.FindById("score")) == "Score: 42");
}

TEST_CASE("StateHasChanged from an Update system rebuilds the same frame", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    test.Frame();
    {
        INFO("The first frame did not build the Ticker exactly once");
        REQUIRE(test.TickerText() == "Value: 0");
        REQUIRE(test.Builds() == 1);
    }

    test.Call<void>(RebuildProbeType, "ShowFromUpdate", std::int32_t{5});
    test.Frame();

    INFO("The Update system's StateHasChanged was not built in that frame's build stage");
    REQUIRE(test.TickerText() == "Value: 5");
    REQUIRE(test.Builds() == 2);
}

TEST_CASE("A changed BuildHash rebuilds a Razor component once", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    test.Frame();

    test.Call<void>(RebuildProbeType, "SetHash", std::int32_t{42});
    test.Frame();
    {
        INFO("The changed BuildHash did not rebuild the Ticker");
        REQUIRE(test.Builds() == 2);
    }
    test.Frame();
    INFO("The same BuildHash a second frame rebuilt it again");
    REQUIRE(test.Builds() == 2);
}

TEST_CASE("An unchanged BuildHash does not rebuild a Razor component", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    test.Frame();
    test.Frame();
    test.Frame();

    INFO("An unchanged BuildHash rebuilt the Ticker");
    REQUIRE(test.Builds() == 1);
}

TEST_CASE("Unmount removes a Razor component's host with its outro", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    UIRoot& root = *test.root;
    test.Frame();
    const ElementId host = Unpacked(test.Call<std::uint64_t>(RebuildProbeType, "TickerHost"));

    {
        INFO("Unmount reported nothing mounted");
        REQUIRE(test.Call<bool>(RebuildProbeType, "UnmountTicker"));
    }
    INFO("The host was removed at once, or without its outro");
    REQUIRE(root.IsAlive(host));
    REQUIRE(root.HasPseudoClass(host, PseudoClass::Outro));
}

TEST_CASE("A throwing build turns off only its Razor component", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    test.Frame();

    test.Call<void>(RebuildProbeType, "RebuildFragile", std::int32_t{1}, std::int32_t{1});
    test.Call<void>(RebuildProbeType, "ShowFromUpdate", std::int32_t{1});
    test.Frame();
    {
        INFO("The throwing build changed the Fragile's elements, or stopped the Ticker");
        REQUIRE(test.FragileText() == "Fragile: 0");
        REQUIRE(test.TickerText() == "Value: 1");
    }

    test.Call<void>(RebuildProbeType, "RebuildFragile", std::int32_t{2}, std::int32_t{0});
    test.Call<void>(RebuildProbeType, "ShowFromUpdate", std::int32_t{2});
    test.Frame();
    INFO("The turned-off Fragile built again before a reload, or the Ticker stopped");
    REQUIRE(test.FragileText() == "Fragile: 0");
    REQUIRE(test.TickerText() == "Value: 2");
}

TEST_CASE("A script reload brings back a Razor component a throw turned off", "[ui][scripting][razor]")
{
    RebuildScriptingApp test;
    test.Frame();
    test.Call<void>(RebuildProbeType, "RebuildFragile", std::int32_t{1}, std::int32_t{1});
    test.Frame();
    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    test.Frame();

    test.Call<void>(RebuildProbeType, "RebuildFragile", std::int32_t{3}, std::int32_t{0});
    test.Frame();
    INFO("The new generation's Fragile did not rebuild");
    REQUIRE(test.FragileText() == "Fragile: 3");
}
