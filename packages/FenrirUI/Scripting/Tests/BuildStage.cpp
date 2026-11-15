// The build stage from C# (spec: "FenrirUI: the build stage"; ADR-0068): a [StageSystem(UIStages.Build)] makes an
// element under a root, and a paint stage system reads its laid-out rectangle in the same frame; the entity it made in
// the stage gets its components when the stage's changes play back; and without the UI installed the script still
// loads and the build stage system never runs.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* BuildProbeType = "UIScriptFixture.BuildProbe";

    bool Near(float a, float b)
    {
        return std::abs(a - b) <= 1e-3f;
    }

    std::uint64_t Packed(ElementId element)
    {
        return (static_cast<std::uint64_t>(element.index) << 32u) | element.version;
    }
} // namespace

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

TEST_CASE("A build stage system's structural changes play back when the stage ends", "[ui][scripting][build]")
{
    UIScriptingApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target);
    test.Call<void>(BuildProbeType, "Arm", root.GetId().value);
    test.Frame();

    const auto made = test.Call<std::uint32_t>(BuildProbeType, "MadeEntity");
    const Entity entity = test.Entities().GetEntity(made);
    INFO("The entity it made in the stage has its Transform: the stage's changes were played back");
    REQUIRE((entity.IsValid() && entity.HasComponent<Transform>()));
}

TEST_CASE("Without the UI installed a script with a build stage system loads and the system never runs",
          "[ui][scripting][build]")
{
    ExtensionTestApp<NoPackages> test(FixtureFromEnvironment("FENRIR_TEST_UI_FIXTURE"), UIExtensions());
    test.Run(3);
    INFO("The fixture loaded (ExtensionTestApp requires it), and its build stage system ran no frame");
    REQUIRE(test.Call<int>(BuildProbeType, "RunCount") == 0);
}
