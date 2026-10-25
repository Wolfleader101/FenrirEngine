// The C# mirrors' shapes (spec seam 7: each extension's layouts, and `new` equal to `{}`): the components and resources
// are checked field by field, offsets, unions and nested enums included, whenever the fixture loads; these cases check
// the values the native functions pass through pointers, and that what C#'s `new` makes is what C++'s `{}` is.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>

#include "FenrirRendererScripting/RendererMappings.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace RendererScriptingTests;

namespace
{
    template <typename T>
    void RequireSameLayout(RendererScriptingApp& test, const char* name)
    {
        const auto result = ScriptingProbe::Component(test.scripting)->ValidateStruct<T>();
        INFO("C#'s " << name << " is not laid out as C++'s: " << result.diagnostic);
        REQUIRE(result.Succeeded());
    }

    bool Same(const Colour& a, const Colour& b)
    {
        return Exactly(a.r, b.r) && Exactly(a.g, b.g) && Exactly(a.b, b.b) && Exactly(a.a, b.a);
    }

    template <typename T>
    bool SameHandle(AssetHandle<T> a, AssetHandle<T> b)
    {
        return a.index == b.index && a.generation == b.generation;
    }

    bool Same(const Projection& a, const Projection& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        if (a.Is<Perspective>())
        {
            return Exactly(a.As<Perspective>().verticalFov, b.As<Perspective>().verticalFov) &&
                   Exactly(a.As<Perspective>().near, b.As<Perspective>().near);
        }
        return Exactly(a.As<Orthographic>().height, b.As<Orthographic>().height) &&
               Exactly(a.As<Orthographic>().near, b.As<Orthographic>().near) &&
               Exactly(a.As<Orthographic>().far, b.As<Orthographic>().far);
    }

    bool Same(const RenderTarget& a, const RenderTarget& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        return a.Is<WindowTarget>() ? a.As<WindowTarget>().window == b.As<WindowTarget>().window
                                    : SameHandle(a.As<ImageTarget>().image, b.As<ImageTarget>().image);
    }

    bool Same(const CameraBackground& a, const CameraBackground& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        return a.Is<SceneBackground>() || Same(a.As<ColourBackground>().colour, b.As<ColourBackground>().colour);
    }

    bool Same(const Camera& a, const Camera& b)
    {
        return Same(a.projection, b.projection) && a.enabled == b.enabled && a.order == b.order &&
               Exactly(a.viewport.x, b.viewport.x) && Exactly(a.viewport.y, b.viewport.y) &&
               Exactly(a.viewport.width, b.viewport.width) && Exactly(a.viewport.height, b.viewport.height) &&
               Same(a.target, b.target) && Same(a.background, b.background) && a.drawDebug == b.drawDebug;
    }

    bool Same(const Sky& a, const Sky& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        return a.Is<ColourSky>() ? Same(a.As<ColourSky>().colour, b.As<ColourSky>().colour)
                                 : SameHandle(a.As<CubemapSky>().cubemap, b.As<CubemapSky>().cubemap);
    }
} // namespace

TEST_CASE("The values the renderer's native functions pass have the C++ layout", "[renderer][scripting]")
{
    RendererScriptingApp test;
    RequireSameLayout<RendererStats>(test, "RendererStats");
    RequireSameLayout<Math::Ray>(test, "Ray");
    RequireSameLayout<Math::AABB>(test, "Bounds");
}

TEST_CASE("A new MeshRenderer, DirectionalLight and Camera from C# are C++'s, for each union kind's new",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    const std::uint32_t meshEntity = test.Entities().CreateEntity().GetId();
    const std::uint32_t lightEntity = test.Entities().CreateEntity().GetId();
    test.Call<void>(NewCallsType, "Add", meshEntity, 0);
    test.Call<void>(NewCallsType, "Add", lightEntity, 5);
    test.ApplyChanges();
    {
        const MeshRenderer& made = test.Entities().GetEntity(meshEntity).GetComponent<MeshRenderer>();
        INFO("A new MeshRenderer should name no mesh or material and be visible, as C++'s");
        REQUIRE(SameHandle(made.mesh, AssetHandle<Mesh>{}));
        REQUIRE(SameHandle(made.material, AssetHandle<Material>{}));
        REQUIRE(made.visible);
    }
    {
        const DirectionalLight& made = test.Entities().GetEntity(lightEntity).GetComponent<DirectionalLight>();
        INFO("A new DirectionalLight should be white at intensity 1, as C++'s");
        REQUIRE(Same(made.colour, Colour::White));
        REQUIRE(Exactly(made.intensity, 1.0f));
    }

    struct Kind
    {
        int number;
        Camera expected;
    };
    Camera orthographic;
    orthographic.projection = Orthographic{};
    Camera imageTarget;
    imageTarget.target = ImageTarget{};
    Camera colourBackground;
    colourBackground.background = ColourBackground{};
    const Kind kinds[] = {{1, Camera{}}, {2, orthographic}, {3, imageTarget}, {4, colourBackground}};
    for (const Kind& kind : kinds)
    {
        const std::uint32_t entity = test.Entities().CreateEntity().GetId();
        test.Call<void>(NewCallsType, "Add", entity, kind.number);
        test.ApplyChanges();
        INFO("C# case " << kind.number
                        << " (1 the Camera's own new, then a new Orthographic, ImageTarget and "
                           "ColourBackground in it) should be C++'s Camera with the same kind's {}");
        REQUIRE(Same(test.Entities().GetEntity(entity).GetComponent<Camera>(), kind.expected));
    }
}

TEST_CASE("A new AmbientLight, Sky and CubemapSky inserted from C# are C++'s", "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(NewCallsType, "InsertResources", 0);
    test.ApplyChanges();
    Scene& scene = test.Get().GetActiveScene();
    {
        const AmbientLight* ambient = scene.GetResource<AmbientLight>();
        INFO("A new AmbientLight should be C++'s dim grey at intensity 1");
        REQUIRE(ambient != nullptr);
        REQUIRE(Same(ambient->colour, AmbientLight{}.colour));
        REQUIRE(Exactly(ambient->intensity, AmbientLight{}.intensity));
    }
    {
        const Sky* sky = scene.GetResource<Sky>();
        INFO("A new Sky should be C++'s default dark grey colour sky");
        REQUIRE(sky != nullptr);
        REQUIRE(Same(*sky, Sky{}));
    }

    test.Call<void>(NewCallsType, "InsertResources", 1);
    test.ApplyChanges();
    const Sky* sky = scene.GetResource<Sky>();
    INFO("A new CubemapSky should be C++'s, naming no cubemap");
    REQUIRE(sky != nullptr);
    REQUIRE(Same(*sky, Sky{CubemapSky{}}));
}
