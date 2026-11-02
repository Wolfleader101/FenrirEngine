// The lighting spec from C# (ticket 22): every component and scene resource the lighting tickets added or grew
// round-trips between C++ and C#, field by field, both ways through the ECS, with every field off its default; an
// instance list made in C# draws as one instanced draw and draws again when C# replaces it; C# reads RendererStats
// whole; and C#'s shadow budgets decide which lights draw shadows.

#include <catch2/catch_test_macros.hpp>

#include <Iris/FieldDifferences.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRendererScripting/Generated/FenrirRendererMappings.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* RoundTripCallsType = "RendererScriptFixture.RoundTripCalls";
    constexpr const char* InstanceListCallsType = "RendererScriptFixture.InstanceListCalls";

    // Of a tagged union only the tag and the kind it selects are compared (the generated defaults test's layouts).
    const std::vector<Iris::UnionLayout> taggedUnions = {
        {"Fenrir.Renderer.CameraBackground", {{0u, 1}, {1u, 2}}},
        {"Fenrir.Renderer.CameraProjection", {{0u, 1}, {1u, 2}}},
        {"Fenrir.Renderer.RenderTarget", {{0u, 1}, {1u, 2}}},
        {"Fenrir.Renderer.Sky", {{0u, 1}, {1u, 2}, {2u, 3}}},
    };

    // The fields that differ between two values, one per line.
    template <typename T>
    std::string Differences(const T& expected, const T& actual)
    {
        std::string text;
        for (const std::string& difference : Iris::FieldDifferences(expected, actual, taggedUnions))
        {
            text += "\n  " + difference;
        }
        return text;
    }

    // `value` crosses both ways: C++ adds it to one entity and C# reads it back with Get<name>, then C# adds it to
    // another with Add<name> and C++ reads it back. Each must be `value`, field by field. `value` must differ from T{}
    // in every field, so a field C# dropped or misplaced cannot match by chance.
    template <typename T>
    void RoundTrip(RendererScriptingApp& test, std::string_view name, const T& value)
    {
        {
            INFO(name << ": the value must differ from its default");
            REQUIRE_FALSE(Iris::FieldDifferences(T{}, value, taggedUnions).empty());
        }
        Entity fromCpp = test.Entities().CreateEntity();
        fromCpp.AddComponent<T>(value);
        const std::string get = "Get" + std::string(name);
        const T read = test.Call<T>(RoundTripCallsType, get.c_str(), fromCpp.GetId());
        {
            INFO(name << " C++ wrote reads back in C# with these fields changed:" << Differences(value, read));
            REQUIRE(Iris::FieldDifferences(value, read, taggedUnions).empty());
        }

        const std::uint32_t fromCSharp = test.Entities().CreateEntity().GetId();
        const std::string add = "Add" + std::string(name);
        test.Call<void>(RoundTripCallsType, add.c_str(), fromCSharp, value);
        test.ApplyChanges();
        const T& written = test.Entities().GetEntity(fromCSharp).GetComponent<T>();
        INFO(name << " C# wrote reads back in C++ with these fields changed:" << Differences(value, written));
        REQUIRE(Iris::FieldDifferences(value, written, taggedUnions).empty());
    }

    // `sky` crosses both ways as the active scene's Sky: written by C++, read by C#, then written by C# over a default.
    void SkyRoundTrip(RendererScriptingApp& test, std::string_view kind, const Sky& sky)
    {
        Scene& scene = test.Get().GetActiveScene();
        scene.AddResource<Sky>(sky);
        const Sky read = test.Call<Sky>(RoundTripCallsType, "GetSky");
        {
            INFO("A " << kind << " C++ wrote reads back in C# with these fields changed:" << Differences(sky, read));
            REQUIRE(Iris::FieldDifferences(sky, read, taggedUnions).empty());
        }
        *scene.GetResource<Sky>() = Sky{};
        test.Call<void>(RoundTripCallsType, "SetSky", sky);
        test.ApplyChanges();
        const Sky& written = *scene.GetResource<Sky>();
        INFO("A " << kind << " C# wrote reads back in C++ with these fields changed:" << Differences(sky, written));
        REQUIRE(Iris::FieldDifferences(sky, written, taggedUnions).empty());
    }

    // A Camera with every field off its default, each union on its other kind.
    Camera LitCamera()
    {
        Camera camera;
        camera.projection = Orthographic{7.0f, 0.5f, 300.0f};
        camera.enabled = false;
        camera.order = 3;
        camera.viewport = {0.1f, 0.2f, 0.3f, 0.4f};
        camera.target = ImageTarget{AssetHandle<Image>{5, 6}};
        camera.background = ColourBackground{{0.2f, 0.3f, 0.4f, 0.5f}};
        camera.drawDebug = false;
        camera.exposure = 1.5f;
        camera.tonemapper = Tonemapper::Aces;
        camera.bloom = {0.2f, 0.3f};
        camera.ambientOcclusion = {true, 0.8f, 1.7f};
        camera.debugView = CameraDebugView::LightClusters;
        return camera;
    }
} // namespace

TEST_CASE("Every lighting component round-trips between C++ and C#, field by field, both ways", "[renderer][scripting]")
{
    RendererScriptingApp test;
    RoundTrip(test, "Camera", LitCamera());
    {
        // The Camera's other kinds: a perspective projection off its defaults, and the scene background.
        Camera perspective = LitCamera();
        perspective.projection = Perspective{1.1f, 0.25f};
        perspective.background = SceneBackground{};
        perspective.target = WindowTarget{WindowId{4}};
        RoundTrip(test, "Camera", perspective);
    }
    RoundTrip(test, "DirectionalLight", DirectionalLight{{0.9f, 0.8f, 0.7f, 0.6f}, 3.0f, false, 40.0f, 0.5f, 1.5f});
    RoundTrip(test, "PointLight", PointLight{{0.1f, 0.2f, 0.3f, 0.4f}, 5.0f, 12.0f, true, 0.25f, 3.0f});
    RoundTrip(test, "SpotLight", SpotLight{{0.4f, 0.3f, 0.2f, 0.1f}, 7.0f, 14.0f, 0.2f, 0.6f, true, 0.75f, 2.5f});
    RoundTrip(test, "RectLight", RectLight{{0.5f, 0.6f, 0.7f, 0.8f}, 9.0f, 2.0f, 0.5f, true, 6.0f});
    RoundTrip(test, "MeshRenderer", MeshRenderer{AssetHandle<Mesh>{1, 2}, AssetHandle<Material>{3, 4}, false, false});
    RoundTrip(test, "InstancedMeshRenderer",
              InstancedMeshRenderer{AssetHandle<Mesh>{1, 2}, AssetHandle<Material>{3, 4},
                                    AssetHandle<InstanceList>{5, 6}, false, false});
}

TEST_CASE("Each kind of Sky, and the SkyLight, round-trip between C++ and C#, field by field, both ways",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    SkyRoundTrip(test, "ColourSky", ColourSky{{0.3f, 0.2f, 0.1f, 0.9f}});
    SkyRoundTrip(test, "CubemapSky", CubemapSky{AssetHandle<Image>{7, 9}, 2.5f, 1.25f});
    SkyRoundTrip(test, "PanoramaSky", PanoramaSky{AssetHandle<Image>{11, 13}, 0.5f, -0.75f});

    Scene& scene = test.Get().GetActiveScene();
    const SkyLight light{PanoramaSky{AssetHandle<Image>{3, 4}, 1.5f, 0.5f}};
    scene.AddResource<SkyLight>(light);
    const SkyLight read = test.Call<SkyLight>(RoundTripCallsType, "GetSkyLight");
    {
        INFO("The SkyLight C++ wrote reads back in C# with these fields changed:" << Differences(light, read));
        REQUIRE(Iris::FieldDifferences(light, read, taggedUnions).empty());
    }
    *scene.GetResource<SkyLight>() = SkyLight{};
    const SkyLight cubemap{CubemapSky{AssetHandle<Image>{8, 1}, 3.0f, 2.0f}};
    test.Call<void>(RoundTripCallsType, "SetSkyLight", cubemap);
    test.ApplyChanges();
    INFO("The SkyLight C# wrote reads back in C++ with these fields changed:"
         << Differences(cubemap, *scene.GetResource<SkyLight>()));
    REQUIRE(Iris::FieldDifferences(cubemap, *scene.GetResource<SkyLight>(), taggedUnions).empty());
}

TEST_CASE("An instance list made in C# draws as one instanced draw of its copies, and again when C# replaces it",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    const std::uint32_t drawn = test.Entities().CreateEntity().GetId();
    const std::uint32_t camera = test.Entities().CreateEntity().GetId();
    test.Call<void>(InstanceListCallsType, "Draw", drawn, camera, 5);
    test.ApplyChanges();
    test.Frame();
    {
        const std::vector<RecordedDraw> opaque = DrawsIn(test.Submitted(), "Opaque");
        INFO("The Opaque pass should hold one draw of C#'s five copies");
        REQUIRE(opaque.size() == 1);
        REQUIRE(opaque.front().draw.instanceCount == 5);
        REQUIRE(Renderer::Stats(test.Get()).instances == 5);
    }

    const AssetHandle<InstanceList> list{test.Call<std::uint32_t>(InstanceListCallsType, "Index"),
                                         test.Call<std::uint32_t>(InstanceListCallsType, "Generation")};
    {
        const InstanceListData* data = test.Store().TryGet(list);
        INFO("The list C# made should hold its five transforms and one colour each");
        REQUIRE(data != nullptr);
        REQUIRE(data->transforms.size() == 5);
        REQUIRE(data->colours.size() == 5);
    }

    {
        INFO("C#'s replace of the drawn list should succeed");
        REQUIRE(test.Call<bool>(InstanceListCallsType, "Replace", 8));
    }
    test.Frame();
    const std::vector<RecordedDraw> opaque = DrawsIn(test.Submitted(), "Opaque");
    INFO("The Opaque pass should draw the replaced list's eight copies, uploaded again");
    REQUIRE(opaque.size() == 1);
    REQUIRE(opaque.front().draw.instanceCount == 8);
    REQUIRE(Renderer::Stats(test.Get()).uploads >= 1);
    REQUIRE(test.Store().Revision(list) == 2);
}

TEST_CASE("C# reads RendererStats as C++ does, every count", "[renderer][scripting]")
{
    RendererScriptingApp test;
    const std::uint32_t drawn = test.Entities().CreateEntity().GetId();
    const std::uint32_t camera = test.Entities().CreateEntity().GetId();
    test.Call<void>(InstanceListCallsType, "Draw", drawn, camera, 3);
    test.Call<void>(SceneBuilderType, "Arm");
    test.ApplyChanges();
    test.Frame();
    test.Frame();

    const RendererStats stats = Renderer::Stats(test.Get());
    {
        INFO("The frame should have drawn something to count: the list's three copies and the builder's mesh");
        REQUIRE(stats.draws > 0);
        REQUIRE(stats.instances > 3);
    }
    const RendererStats read = test.Call<RendererStats>(RoundTripCallsType, "Stats");
    INFO("C#'s RendererStats differ from C++'s in:" << Differences(stats, read));
    REQUIRE(Iris::FieldDifferences(stats, read, taggedUnions).empty());
}

TEST_CASE("The shadow budgets C# sets decide whether a shadowed spot or point light draws shadows",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    // A casting mesh at (1, 2, 3) and a camera at (0, 0, 5).
    test.Call<void>(SceneBuilderType, "Arm");
    test.ApplyChanges();
    test.Frame();

    const auto shadowDraws = [&] {
        test.Frame();
        test.Frame();
        return Renderer::Stats(test.Get()).shadowDraws;
    };

    // A spot light behind the mesh, pointing at it along -Z.
    Entity spot = test.Entities().CreateEntity();
    spot.AddComponent<Transform>().pos = {1.0f, 2.0f, 6.0f};
    SpotLight spotLight;
    spotLight.castShadows = true;
    spot.AddComponent<SpotLight>(spotLight);
    {
        INFO("Under the default budget the shadowed spot light should draw the mesh into its shadow");
        REQUIRE(shadowDraws() > 0);
    }
    {
        INFO("C# setting the spot budget to 0 should leave nothing drawn into shadows");
        REQUIRE(test.Call<bool>(RoundTripCallsType, "SpotShadowBudget", 0u));
        REQUIRE(shadowDraws() == 0);
    }
    REQUIRE(test.Call<bool>(RoundTripCallsType, "SpotShadowBudget", 8u));
    {
        INFO("C# setting it back to 8 should draw the spot light's shadow again");
        REQUIRE(shadowDraws() > 0);
    }
    spot.GetComponent<SpotLight>().castShadows = false;

    Entity point = test.Entities().CreateEntity();
    point.AddComponent<Transform>().pos = {1.0f, 2.0f, 5.0f};
    PointLight pointLight;
    pointLight.castShadows = true;
    point.AddComponent<PointLight>(pointLight);
    {
        INFO("Under the default budget the shadowed point light should draw the mesh into its shadow");
        REQUIRE(shadowDraws() > 0);
    }
    INFO("C# setting the point budget to 0 should leave nothing drawn into shadows");
    REQUIRE(test.Call<bool>(RoundTripCallsType, "PointShadowBudget", 0u));
    REQUIRE(shadowDraws() == 0);
}
