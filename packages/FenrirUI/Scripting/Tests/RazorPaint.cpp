// Razor paint overrides and <Document Src> (spec: "Components, mounting and the dispatcher", "The builder" unowned
// children; ADR-0067, ADR-0068): the fixture's Needle.razor, mounted on a root that lays out and paints into a
// capturing target, paints through its OnPaint override in Fenrir.UI.Paint, called by the Razor dispatcher's paint
// system, and its Pip child paints into its own panel; Quiet.razor, without the override, paints as C++ paints it.
// Credits.razor places a markup document the test wrote, whose elements C++ finds by id, which a rebuild leaves alone,
// a changed Src replaces, and unmounting removes with the host.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* NeedleProbeType = "UIScriptFixture.NeedleProbe";
    constexpr const char* CreditsProbeType = "UIScriptFixture.CreditsProbe";

    /** @brief The fixture with a Needle and a Quiet mounted on a capturing root, built, laid out and painted once. */
    struct NeedleScriptingApp : UIScriptingApp
    {
        NeedleScriptingApp() : root(MakeRoot(target))
        {
            Call<void>(NeedleProbeType, "Mount", root.GetId().value);
            Frame();
        }

        std::shared_ptr<CaptureTarget> target;
        UIRoot& root;
    };

    /** @brief Writes a one-label document with the id string `id`, and returns its path as the fixture loads it. */
    std::string WriteDocument(std::string_view name, std::string_view id)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::ofstream file(path);
        file << "<label id=\"" << id << "\">" << id << "</label>\n";
        return path.generic_string();
    }

    /** @brief The fixture with a Credits placing the first document on a capturing root, built once. */
    struct CreditsScriptingApp : UIScriptingApp
    {
        CreditsScriptingApp()
            : first(WriteDocument("fenrir-razor-credits-first.ui", "credit-first")),
              second(WriteDocument("fenrir-razor-credits-second.ui", "credit-second")), root(MakeRoot(target))
        {
            host =
                Unpacked(Call<std::uint64_t>(CreditsProbeType, "Mount", root.GetId().value, std::string_view(first)));
            Frame();
        }

        ElementId Find(std::string_view id)
        {
            const ElementId element = root.FindById(id);
            INFO("Nothing has the id " << id);
            REQUIRE(root.IsAlive(element));
            return element;
        }

        std::string first;
        std::string second;
        std::shared_ptr<CaptureTarget> target;
        UIRoot& root;
        ElementId host;
    };
} // namespace

TEST_CASE("A Razor component's OnPaint draws into its host after the host's own box and before its children",
          "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    {
        INFO("The paint system called the override once, in the frame the Needle first built");
        REQUIRE(test.Call<int>(NeedleProbeType, "NeedlePaints") == 1);
    }
    {
        INFO("The host's box, the Needle's square, the dot, the Pip's panel and its square, then the Quiet's panel");
        REQUIRE(QuadColours(test.target->lists.back()) ==
                std::vector<std::uint32_t>{Red, Green, Blue, Yellow, Magenta, White});
    }
    const Math::Vec2 centre = QuadCentre(test.target->lists.back(), 1);
    INFO("The square is not where the Needle drew it, 20 wide and 10 in from the host's top-left: at (20, 20)");
    REQUIRE(Near(centre.x, 20.0f));
    REQUIRE(Near(centre.y, 20.0f));
}

TEST_CASE("A Razor component's OnPaint is called again each frame", "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    test.Frame();
    INFO("The second frame did not call the override a second time");
    REQUIRE(test.Call<int>(NeedleProbeType, "NeedlePaints") == 2);
}

TEST_CASE("A child Razor component's OnPaint draws into the first element its markup places",
          "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    {
        INFO("The Pip's override was not called in the first frame");
        REQUIRE(test.Call<int>(NeedleProbeType, "PipPaints") == 1);
    }
    const Rect2D pip = test.root.GetBoxes(test.root.FindById("pip")).border;
    {
        INFO("The Pip's panel is not below the Needle's dot, so its boxes cannot be told from the Needle's host's");
        REQUIRE(pip.y > 0.0f);
    }
    INFO("The Pip was not handed its own panel's boxes");
    REQUIRE(Near(test.Call<float>(NeedleProbeType, "PipBorderY"), pip.y));
}

TEST_CASE("An unmounted Razor component's OnPaint is no longer called", "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    test.Call<void>(NeedleProbeType, "UnmountNeedle");
    test.Frame();
    INFO("The Needle was painted after it was unmounted");
    REQUIRE(test.Call<int>(NeedleProbeType, "NeedlePaints") == 1);
}

TEST_CASE("A Razor component without an OnPaint override paints only as C++ paints its elements",
          "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    test.Call<void>(NeedleProbeType, "UnmountNeedle");
    test.Frame();
    INFO("Something besides the Quiet's panel's box was drawn");
    REQUIRE(QuadColours(test.target->lists.back()) == std::vector<std::uint32_t>{White});
}

TEST_CASE("A script generation with painting Razor components and a placed document is collected when released",
          "[ui][scripting][razor][paint]")
{
    NeedleScriptingApp test;
    const std::string document = WriteDocument("fenrir-razor-credits-released.ui", "credit-released");
    test.Call<std::uint64_t>(CreditsProbeType, "Mount", test.root.GetId().value, std::string_view(document));
    test.Frame();
    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    INFO("The released generation was not collected: the Razor runtime still holds something of it");
    REQUIRE(test.scripting.PendingUnloadCount() == 0);
}

TEST_CASE("<Document Src> places a document's elements in a host panel, which C++ finds by id",
          "[ui][scripting][razor][document]")
{
    CreditsScriptingApp test;
    const ElementId credit = test.Find("credit-first");
    const ElementId panel = test.root.GetParent(credit);
    {
        INFO("The document's label is not the loaded label, with its text");
        REQUIRE(test.root.GetText(credit) == "credit-first");
    }
    const std::span<const ElementId> children = test.root.GetChildren(test.host);
    INFO("The document's label is not in the Document's panel, the Credits' host's child after its title");
    REQUIRE(children.size() == 2);
    REQUIRE(children[0] == test.Find("credits-title"));
    REQUIRE(children[1] == panel);
}

TEST_CASE("A rebuild of the Razor component placing a document leaves the document's elements untouched",
          "[ui][scripting][razor][document]")
{
    CreditsScriptingApp test;
    const ElementId credit = test.Find("credit-first");

    test.Call<void>(CreditsProbeType, "SetTitle", std::string_view("Thanks"));
    test.Frame();

    INFO("The title rebuilt, and the document's label is the same element, alone in its panel");
    REQUIRE(test.root.GetText(test.Find("credits-title")) == "Thanks");
    REQUIRE(test.root.FindById("credit-first") == credit);
    REQUIRE(test.root.GetChildren(test.root.GetParent(credit)).size() == 1);
}

TEST_CASE("Changing a Document's Src replaces its elements with the new document's", "[ui][scripting][razor][document]")
{
    CreditsScriptingApp test;
    const ElementId old = test.Find("credit-first");
    const ElementId panel = test.root.GetParent(old);

    test.Call<void>(CreditsProbeType, "SetSrc", std::string_view(test.second));
    test.Frame();

    INFO("The first document's label is gone at once, and the second's is in the same panel");
    REQUIRE(!test.root.IsAlive(old));
    const ElementId replaced = test.Find("credit-second");
    REQUIRE(test.root.GetParent(replaced) == panel);
    REQUIRE(test.root.GetChildren(panel).size() == 1);
}

TEST_CASE("Unmounting the Razor component placing a document removes the document with the host",
          "[ui][scripting][razor][document]")
{
    CreditsScriptingApp test;
    const ElementId credit = test.Find("credit-first");
    const ElementId panel = test.root.GetParent(credit);

    INFO("the script unmounts the component placing the document");
    REQUIRE(test.Call<bool>(CreditsProbeType, "Unmount"));

    INFO("The host, the Document's panel and the document's label are gone");
    REQUIRE(!test.root.IsAlive(test.host));
    REQUIRE(!test.root.IsAlive(panel));
    REQUIRE(!test.root.IsAlive(credit));
}
