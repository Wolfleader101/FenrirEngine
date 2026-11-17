// UI roots driven from C# (spec: "Scripting"; seam 1 through the scripting harness): a script builds the three-panel
// tree a C++ test builds and reads the rectangles C++ reads; it loads a document by path and finds an element by id;
// a click fed through FenrirInput's test feed reaches its EventReader<UIEvent> with the target; its Fenrir.UI.Paint
// stage system paints into an element after the element's own box; and a stale id's natives do nothing and return
// false. C# makes image and world roots too (spec: "Image and world roots from C#"): an image root's image is there at
// its size, draws, and is unloaded with the root; a world root on a quad entity hovers and clicks by the ray C# gives
// it; the wrong kind of root and a missing quad warn once; and releasing the script generation ends what it made.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UIImageTarget.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UIWorldTarget.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    // The fixture's three-panel tree (TreeCalls.BuildThreePanels), built in C++: a row 600 by 200 with 10 of padding
    // holding three panels that share its width, each with a margin of 5, the middle one twice as wide.
    std::vector<ElementId> CppThreePanels(UIRoot& root)
    {
        const ElementId row = root.Create(root.GetRootElement(), "panel");
        INFO("the three-panel tree takes its style");
        for (const auto& [name, value] :
             {std::pair{"flex-direction", "row"}, {"width", "600px"}, {"height", "200px"}, {"padding", "10px"}})
        {
            REQUIRE(root.SetStyleProperty(row, name, value));
        }
        std::vector<ElementId> panels;
        for (const char* grow : {"1", "2", "1"})
        {
            const ElementId panel = root.Create(row, "panel");
            REQUIRE(root.SetStyleProperty(panel, "flex-grow", grow));
            REQUIRE(root.SetStyleProperty(panel, "margin", "5px"));
            panels.push_back(panel);
        }
        return panels;
    }
} // namespace

TEST_CASE("A script builds the three-panel tree C++ builds and reads the same rectangles", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> cppTarget;
    std::shared_ptr<CaptureTarget> scriptTarget;
    UIRoot& cpp = test.MakeRoot(cppTarget);
    UIRoot& scripted = test.MakeRoot(scriptTarget);
    const std::vector<ElementId> panels = CppThreePanels(cpp);
    const std::uint32_t root = scripted.GetId().value;
    {
        INFO("The script did not build its three panels");
        REQUIRE(test.Call<int>(TreeCallsType, "BuildThreePanels", root) == 3);
    }
    test.ApplyChanges();
    test.Frame();

    for (int panel = 0; panel < 3; ++panel)
    {
        const Rect2D box = cpp.GetBoxes(panels[static_cast<std::size_t>(panel)]).border;
        INFO("Panel " << panel << ": C++ laid it out at " << box.x << ", " << box.y << ", " << box.width << " by "
                      << box.height);
        REQUIRE(Near(test.Call<float>(TreeCallsType, "PanelBox", root, panel, 0), box.x));
        REQUIRE(Near(test.Call<float>(TreeCallsType, "PanelBox", root, panel, 1), box.y));
        REQUIRE(Near(test.Call<float>(TreeCallsType, "PanelBox", root, panel, 2), box.width));
        REQUIRE(Near(test.Call<float>(TreeCallsType, "PanelBox", root, panel, 3), box.height));
    }
    INFO("The script's tree is the same shape: a row of three under the root element");
    REQUIRE(test.Call<int>(TreeCallsType, "RowChildren", root) == 3);
}

TEST_CASE("A script loads a document by path and finds an element by id", "[ui][scripting]")
{
    UIScriptingApp test;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-ui-scripting-menu.ui";
    {
        std::ofstream file(path);
        file << "<panel id=\"menu\">\n  <button id=\"go\">Go</button>\n</panel>\n";
    }
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    const std::uint32_t id = root.GetId().value;
    const std::string where = path.generic_string();

    INFO("The document loads with no errors");
    REQUIRE(test.Call<bool>(TreeCallsType, "LoadMenu", id, std::string_view(where)));
    const ElementId go = root.FindById("go");
    REQUIRE(go.IsValid());
    INFO("C# finds the button C++ finds, by its id string");
    REQUIRE(test.Call<std::uint64_t>(TreeCallsType, "Find", id, std::string_view("go")) == Packed(go));
    INFO("...and reads its tag and text");
    REQUIRE(test.Call<bool>(TreeCallsType, "TagAndTextAre", id, std::string_view("go"), std::string_view("button"),
                            std::string_view("Go")));
    std::filesystem::remove(path);
}

TEST_CASE("A click fed through the test feed reaches the script's EventReader<UIEvent> with its target",
          "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    const ElementId button = root.Create(root.GetRootElement(), "button");
    INFO("the button's size is taken");
    REQUIRE(root.SetStyleProperty(button, "width", "100px"));
    REQUIRE(root.SetStyleProperty(button, "height", "40px"));
    test.Frame();
    test.Call<void>(ClicksType, "Clear");

    test.Feed().MouseMove(WindowId{1}, {50.0f, 20.0f}, {0.0f, 0.0f});
    test.Feed().MouseButtonDown(WindowId{1}, MouseButton::Left);
    test.Feed().MouseButtonUp(WindowId{1}, MouseButton::Left);
    test.Frame();

    INFO("C# read one click, at the button, of this root");
    REQUIRE(test.Call<int>(ClicksType, "Count") == 1);
    REQUIRE(test.Call<std::uint64_t>(ClicksType, "LastTarget") == Packed(button));
    INFO("The click names the root it happened in");
    REQUIRE(test.Call<std::uint32_t>(ClicksType, "LastRoot") == root.GetId().value);
}

TEST_CASE("A script reads the cursor its root's hovered element wants", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    const ElementId button = root.Create(root.GetRootElement(), "button");
    INFO("the button's size is taken");
    REQUIRE(root.SetStyleProperty(button, "width", "100px"));
    REQUIRE(root.SetStyleProperty(button, "height", "40px"));
    test.Frame();
    const std::uint32_t id = root.GetId().value;
    INFO("with nothing hovered, C# reads auto");
    REQUIRE(test.Call<int>(CursorCallsType, "HoveredCursor", id) == static_cast<int>(Cursor::Auto));

    test.Feed().MouseMove(WindowId{1}, {50.0f, 20.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("over the button, C# reads the default stylesheet's pointer");
    REQUIRE(test.Call<int>(CursorCallsType, "HoveredCursor", id) == static_cast<int>(Cursor::Pointer));
}

namespace
{
    // The fixture's gauge (PaintProbe's element): a red panel of 100 by 100 at the root's top-left, with an empty
    // child panel, which draws nothing.
    UIRoot& GaugeRoot(UIScriptingApp& test, std::shared_ptr<CaptureTarget>& target)
    {
        UIRoot& root = test.MakeRoot(target);
        const ElementId panel = root.Create(root.GetRootElement(), "panel");
        root.SetIdString(panel, "gauge");
        INFO("the gauge takes its style and its child");
        REQUIRE(root.SetStyleProperty(panel, "width", "100px"));
        REQUIRE(root.SetStyleProperty(panel, "height", "100px"));
        REQUIRE(root.SetStyleProperty(panel, "background-color", "#f00"));
        REQUIRE(root.Create(panel, "panel").IsValid());
        return root;
    }

} // namespace

TEST_CASE("A script's paint stage system paints into an element after the element's own box", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = GaugeRoot(test, target);

    test.Call<void>(PaintProbeType, "Arm", root.GetId().value, std::string_view("gauge"));
    test.Frame();
    INFO("The stage system ran and was given a painter");
    REQUIRE(test.Call<int>(PaintProbeType, "Painted") == 1);
    INFO("The element's box, then the script's square: the empty child draws nothing");
    REQUIRE(QuadColours(target->lists.back()) == std::vector<std::uint32_t>{Red, Green});
    INFO("The square is where the script drew it: 20 wide, 10 in from the element's top-left");
    const Math::Vec2 centre = QuadCentre(target->lists.back(), 1);
    REQUIRE(Near(centre.x, 20.0f));
    REQUIRE(Near(centre.y, 20.0f));
}

TEST_CASE("Outside the paint stage a script's PaintInto gives no painter", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = GaugeRoot(test, target);
    test.Frame();
    INFO("Called from the test, outside any stage system, it gives a painter id that names none");
    REQUIRE(test.Call<std::uint64_t>(PaintProbeType, "PainterOutsideStage", root.GetId().value,
                                     std::string_view("gauge")) == 0);
}

TEST_CASE("A paint stage system that paints nothing leaves the element as C++ paints it", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = GaugeRoot(test, target);
    test.Call<void>(PaintProbeType, "Arm", root.GetId().value, std::string_view("gauge"));
    test.Frame();
    test.Call<void>(PaintProbeType, "Disarm");
    test.Frame();
    INFO("Disarmed, the element paints its own box alone");
    REQUIRE(QuadColours(target->lists.back()) == std::vector<std::uint32_t>{Red});
}

TEST_CASE("A stale element's natives do nothing and return false", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    const ElementId removed = root.Create(root.GetRootElement(), "panel");
    root.SetIdString(removed, "gone");
    const std::uint32_t id = root.GetId().value;
    {
        INFO("The script did not find and keep the element by its id string");
        REQUIRE(test.Call<int>(TreeCallsType, "Keep", id, std::string_view("gone")) == 1);
    }
    {
        INFO("The kept element could not be removed");
        REQUIRE(root.Remove(removed, RemoveMode::Immediate));
    }

    {
        INFO("A call on the kept, now stale, id returned true, non-zero or an element");
        REQUIRE(test.Call<int>(TreeCallsType, "StaleCallsThatSucceeded", id) == 0);
    }
    INFO("A call on the stale id changed the tree: the root element has children");
    REQUIRE(root.GetChildren(root.GetRootElement()).empty());
}

TEST_CASE("A destroyed root's natives do nothing and return false", "[ui][scripting]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    const std::uint32_t id = root.GetId().value;

    {
        INFO("The script could not destroy the root");
        REQUIRE(test.Call<bool>(TreeCallsType, "DestroyRoot", id));
    }
    {
        INFO("A second destroy of the destroyed root returned true");
        REQUIRE_FALSE(test.Call<bool>(TreeCallsType, "DestroyRoot", id));
    }
    INFO("A create under the destroyed root made an element");
    REQUIRE(test.Call<std::uint64_t>(TreeCallsType, "CreateUnderRoot", id) == Packed(ElementId{}));
}

namespace
{
    // The fixture's types for image and world roots.
    constexpr const char* RootCallsType = "UIScriptFixture.RootCalls";

    /** @brief The root `id` names among the app's roots, or null. */
    UIRoot* RootNamed(UIScriptingApp& test, std::uint32_t id)
    {
        for (UIRoot* root : test.Roots().GetRoots())
        {
            if (root->GetId().value == id)
            {
                return root;
            }
        }
        return nullptr;
    }

    /** @brief An image handle as the fixture packs it: its index in the high half, its generation in the low. */
    AssetHandle<Image> UnpackedImage(std::uint64_t packed)
    {
        AssetHandle<Image> image;
        image.index = static_cast<std::uint32_t>(packed >> 32u);
        image.generation = static_cast<std::uint32_t>(packed);
        return image;
    }

    /**
     * @brief An entity of the active scene with a 2 by 1 world unit quad at the origin, facing +Z, 100 UI units to a
     * world unit: a world root on it is 200 by 100 UI units.
     */
    Entity QuadEntity(UIScriptingApp& test)
    {
        Entity quad = test.app.GetActiveScene().GetEntityList().CreateEntity();
        quad.AddComponent<WorldQuad2D>(WorldQuad2D{{2.0f, 1.0f}, 100.0f});
        return quad;
    }

    /** @brief How many warnings the app logged that contain `text`. */
    std::size_t Warned(UIScriptingApp& test, const std::string& text)
    {
        return LoggedWith(test.headless, Testing::LogLevel::Warn, text);
    }
} // namespace

TEST_CASE("A script's image root gives an image of its size that its label draws into, unloaded with the root",
          "[ui][scripting][roots]")
{
    UIScriptingApp test;
    const std::uint32_t id = test.Call<std::uint32_t>(RootCallsType, "MakeImageRoot", 256u, 128u);
    UIRoot* root = RootNamed(test, id);
    {
        INFO("The script made no root");
        REQUIRE(root != nullptr);
    }
    auto* target = dynamic_cast<UIImageTarget*>(root->GetConfig().target.get());
    {
        INFO("The root draws into an image target");
        REQUIRE(target != nullptr);
    }
    const AssetHandle<Image> image = UnpackedImage(test.Call<std::uint64_t>(RootCallsType, "Image", id));
    {
        INFO("C#'s GetImage is the target's image, and it is not empty");
        REQUIRE_FALSE(image.IsEmpty());
        REQUIRE(image == target->GetImage());
    }
    const AssetStore& store = test.app.GetResource<AssetStore>();
    const AssetData<Image>* data = store.TryGet(image);
    {
        INFO("The image is in the store at 256 by 128 pixels");
        REQUIRE(data != nullptr);
        REQUIRE(data->width == 256u);
        REQUIRE(data->height == 128u);
    }
    {
        INFO("The image root blends linear and lays out by Height1080, as a C++ image root does");
        REQUIRE(root->GetConfig().blendSpace == BlendSpace2D::Linear);
        REQUIRE(root->GetConfig().scaleRule == ScaleRule::Height1080);
    }

    {
        INFO("The script could not make a label in the root");
        REQUIRE(test.Call<std::uint64_t>(RootCallsType, "AddLabel", id, std::string_view("Score")) !=
                Packed(ElementId{}));
    }
    test.Frame();
    {
        INFO("The frame painted the root element and the label");
        REQUIRE(root->GetStats().elementsPainted == 2u);
    }

    {
        INFO("The script could not destroy its image root");
        REQUIRE(test.Call<bool>(TreeCallsType, "DestroyRoot", id));
    }
    test.Frame();
    INFO("The image went with the root");
    REQUIRE_FALSE(store.IsValid(image));
}

TEST_CASE("A script's world root on a quad entity hovers and clicks a button by the ray it gives",
          "[ui][scripting][roots]")
{
    UIScriptingApp test;
    const Entity quad = QuadEntity(test);
    const std::uint32_t id = test.Call<std::uint32_t>(RootCallsType, "MakeWorldRoot", quad.GetId());
    UIRoot* root = RootNamed(test, id);
    {
        INFO("The script made no root, or not on the quad");
        REQUIRE(root != nullptr);
        auto* target = dynamic_cast<UIWorldTarget*>(root->GetConfig().target.get());
        REQUIRE(target != nullptr);
        REQUIRE(target->GetEntity() == quad);
    }
    // A button over the quad's centre, (100, 50) in UI units: 50 by 50 from (75, 25). The root has no default
    // stylesheet, so the button is told to take pointer input.
    const ElementId button = root->Create(root->GetRootElement(), "button");
    {
        INFO("the button takes its place, size and pointer input");
        for (const auto& [name, value] : {std::pair{"position", "absolute"},
                                          {"left", "75px"},
                                          {"top", "25px"},
                                          {"width", "50px"},
                                          {"height", "50px"},
                                          {"pointer-events", "all"}})
        {
            REQUIRE(root->SetStyleProperty(button, name, value));
        }
    }
    test.Frame();
    test.Call<void>(ClicksType, "Clear");

    {
        INFO("A ray from 5 in front of the quad's centre, straight at it, is over the button");
        REQUIRE(test.Call<bool>(RootCallsType, "AimAtCentre", id, false));
        REQUIRE(root->GetHovered() == button);
    }
    {
        INFO("Pressed and then released along the same ray, still over the button");
        REQUIRE(test.Call<bool>(RootCallsType, "AimAtCentre", id, true));
        REQUIRE(test.Call<bool>(RootCallsType, "AimAtCentre", id, false));
    }
    test.Frame();
    {
        INFO("C# read one click, at the button, of the world root");
        REQUIRE(test.Call<int>(ClicksType, "Count") == 1);
        REQUIRE(test.Call<std::uint64_t>(ClicksType, "LastTarget") == Packed(button));
        REQUIRE(test.Call<std::uint32_t>(ClicksType, "LastRoot") == id);
    }

    INFO("With the ray taken away, nothing is hovered");
    REQUIRE_FALSE(test.Call<bool>(RootCallsType, "ClearRay", id));
    REQUIRE_FALSE(root->GetHovered().IsValid());
}

TEST_CASE("The wrong kind of root and a quad-less entity do nothing from C#, each warned once in a debug build",
          "[ui][scripting][roots]")
{
    UIScriptingApp test;
    const std::uint32_t window = test.Call<std::uint32_t>(RootCallsType, "MakeWindowRoot");
    {
        INFO("The script made no window root");
        REQUIRE(RootNamed(test, window) != nullptr);
    }
    {
        INFO("A window root has no image");
        REQUIRE(UnpackedImage(test.Call<std::uint64_t>(RootCallsType, "Image", window)).IsEmpty());
    }
    {
        INFO("A window root takes no ray, twice, and keeps no pointer");
        REQUIRE_FALSE(test.Call<bool>(RootCallsType, "AimAtCentre", window, false));
        REQUIRE_FALSE(test.Call<bool>(RootCallsType, "AimAtCentre", window, false));
    }

    const std::size_t rootsBefore = test.Roots().GetRoots().size();
    const Entity bare = test.app.GetActiveScene().GetEntityList().CreateEntity();
    {
        INFO("A world root on an entity without a WorldQuad2D, asked for twice, names no root");
        REQUIRE(test.Call<std::uint32_t>(RootCallsType, "MakeWorldRoot", bare.GetId()) == 0u);
        REQUIRE(test.Call<std::uint32_t>(RootCallsType, "MakeWorldRoot", bare.GetId()) == 0u);
        REQUIRE(test.Roots().GetRoots().size() == rootsBefore);
    }

#ifdef NDEBUG
    INFO("A release build warns of nothing");
    REQUIRE(Warned(test, "UINative.") == 0);
#else
    INFO("Each cause warned once, under its native's name");
    REQUIRE(Warned(test, "UINative.SetPointerRay: the root takes no pointer ray") == 1);
    REQUIRE(Warned(test, "UINative.CreateWorldRoot: the entity has no WorldQuad2D") == 1);
#endif
}

TEST_CASE("Releasing the script generation ends the image and world roots it made, and their targets",
          "[ui][scripting][roots]")
{
    UIScriptingApp test;
    const Entity quad = QuadEntity(test);
    const std::size_t rootsBefore = test.Roots().GetRoots().size();
    const std::uint32_t image = test.Call<std::uint32_t>(RootCallsType, "MakeImageRoot", 64u, 32u);
    const std::uint32_t world = test.Call<std::uint32_t>(RootCallsType, "MakeWorldRoot", quad.GetId());
    {
        INFO("The script made its two roots");
        REQUIRE(RootNamed(test, image) != nullptr);
        REQUIRE(RootNamed(test, world) != nullptr);
    }
    const AssetHandle<Image> handle = UnpackedImage(test.Call<std::uint64_t>(RootCallsType, "Image", image));
    std::weak_ptr<UITarget> worldTarget = RootNamed(test, world)->GetConfig().target;
    test.Frame();

    {
        INFO("The script did not reload");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    test.Frame();
    {
        INFO("Both roots are gone");
        REQUIRE(RootNamed(test, image) == nullptr);
        REQUIRE(RootNamed(test, world) == nullptr);
        REQUIRE(test.Roots().GetRoots().size() == rootsBefore);
    }
    INFO("The image was unloaded and the world target ended");
    REQUIRE_FALSE(test.app.GetResource<AssetStore>().IsValid(handle));
    REQUIRE(worldTarget.expired());
}
