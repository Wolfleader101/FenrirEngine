// The build stage from C# (spec: "FenrirUI: the build stage"; ADR-0068): a [StageSystem(UIStages.Build)] makes an
// element under a root, and a paint stage system reads its laid-out rectangle in the same frame; the entity it made in
// the stage gets its components when the stage's changes play back; and without the UI installed the script still
// loads and the build stage system never runs.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

TEST_CASE("A script's build stage system makes an element laid out and painted the same frame",
          "[ui][scripting][build]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    test.Call<void>(BuildProbeType, "Arm", root.GetId().value);
    test.Frame();

    INFO("The build stage system made the panel under the root element");
    const ElementId built = root.FindById("built");
    REQUIRE(root.IsAlive(built));
    REQUIRE(test.Call<std::uint64_t>(BuildProbeType, "Built") == Packed(built));
    INFO("The paint stage system read its laid-out rectangle in the same frame");
    REQUIRE(Near(test.Call<float>(BuildProbeType, "SeenWidth"), 120.0f));
    REQUIRE(Near(test.Call<float>(BuildProbeType, "SeenHeight"), 40.0f));
    INFO("And the root painted it that frame");
    REQUIRE(target->lists.back().vertices.size() == 4 * sizeof(PainterVertex));
}

TEST_CASE("A build stage system's structural changes play back when the build stage ends", "[ui][scripting][build]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    test.Call<void>(BuildProbeType, "Arm", root.GetId().value);
    test.Frame();

    INFO("The paint stage of the same frame saw the entity's Transform: the build stage's own end played it back");
    REQUIRE(test.Call<bool>(BuildProbeType, "PlayedBackBeforePaint"));
    const auto made = test.Call<std::uint32_t>(BuildProbeType, "MadeEntity");
    const Entity entity = test.Entities().GetEntity(made);
    INFO("And it keeps it after the frame");
    REQUIRE((entity.IsValid() && entity.HasComponent<Transform>()));
}

TEST_CASE("Without the UI installed a script with a build stage system loads and the system never runs",
          "[ui][scripting][build]")
{
    ExtensionTestApp<NoPackages> test(FixtureFromEnvironment("FENRIR_TEST_UI_FIXTURE"), UIExtensions());
    INFO("The fixture, with its build stage system, loaded with no UI installed");
    REQUIRE(test.scripting.HasScripts());
    test.Run(3);
    INFO("Its build stage system ran no frame");
    REQUIRE(test.Call<int>(BuildProbeType, "RunCount") == 0);
}
