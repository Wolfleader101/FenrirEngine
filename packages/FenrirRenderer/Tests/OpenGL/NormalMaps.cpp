// builtin:lit's normal maps on OpenGL (ADR-0048): a quad with a tangent stream is lit as the same quad without one,
// through Schueler's derivative frame, whether its UVs run as the image does, are mirrored in U (handedness -1), the
// quad is mirrored by its world matrix (the draw's determinant sign), or its back face is seen through a double-sided
// material. Each frame is one quad filling the centre, lit by one white light under a black sky, and reads back its
// SceneColor, linear.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;

    /** @brief How a test quad is laid out and placed. Each faces the camera at distance 2. */
    enum class Placement
    {
        /** @brief UV0 as RendererTest::TexturedQuad's: U along +X, the image's up along +Y. */
        Plain,
        /** @brief U mirrored, so it runs along -X: the tangent is -X with handedness -1. */
        MirroredUv,
        /** @brief TexturedQuad's UVs and tangents, wound clockwise and drawn through a world matrix scaled by -1 in X,
         * so it faces the camera counter-clockwise with U along -X. */
        MirroredWorld,
        /** @brief Plain's quad turned half a turn about +Y, showing its back face through a double-sided material:
         * U runs along -X. */
        BackFacing,
    };

    /** @brief How a test quad's normal is made. */
    enum class Lighting
    {
        /** @brief No normal map: the interpolated normal. */
        Flat,
        /** @brief The normal map, in the derivative frame: the mesh has no tangents. */
        DerivativeFrame,
        /** @brief The normal map, in the frame of the mesh's tangent stream. */
        TangentStream,
    };

    /** @brief A unit quad for `placement`, at distance 2 in its local space, with a tangent stream when `tangents`. */
    Fenrir::MeshData TestQuad(Placement placement, bool tangents)
    {
        Fenrir::MeshData mesh = RendererTest::TexturedQuad(1.0f, -2.0f);
        Vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
        if (placement == Placement::MirroredUv)
        {
            for (Fenrir::Math::Vec2& uv : mesh.uv0)
            {
                uv.x = 1.0f - uv.x;
            }
            tangent = Vec4(-1.0f, 0.0f, 0.0f, -1.0f);
        }
        if (placement == Placement::MirroredWorld)
        {
            mesh.indices = {0, 2, 1, 0, 3, 2};
        }
        if (tangents)
        {
            mesh.tangents.assign(mesh.positions.size(), tangent);
        }
        return mesh;
    }

    /**
     * @brief A scene lit by one white light from (1, 1, 1), under a black sky, and an entity whose quad and material
     * are swapped between frames. Its normal map tilts the normal to (0.6, 0.6, 0.53): towards +U and up the image.
     */
    class NormalMapScene
    {
      public:
        NormalMapScene()
        {
            test.RequireOpenGL();
            test.AddCamera();
            // Its entity's -Z turned to (-1, -1, -1) / sqrt 3: pitched down by asin(1 / sqrt 3), then turned 45 degrees
            // about +Y.
            Fenrir::Entity light = test.app.GetActiveScene().GetEntityList().CreateEntity();
            light.GetComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(45.0f), Vec3(0.0f, 1.0f, 0.0f)) *
                Fenrir::Math::AngleAxis(-0.6154797f, Vec3(1.0f, 0.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();
            test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::Black});

            Fenrir::AssetStore& store = test.assets.Store();
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle;
            // Fully rough, so GGX's specular adds little.
            m_flat = store.CreateMaterial(lit, Fenrir::AssetOwner::App());
            store.SetFloat(m_flat, "roughness", 1.0f);
            // (204, 204, 195) / 255 x 2 - 1 = (0.6, 0.6, 0.53), within a byte of unit length.
            const auto normalMap =
                store.Add<Fenrir::Image>(RendererTest::Picture(1, 1, {204, 204, 195, 255}, Fenrir::ColourSpace::Linear),
                                         Fenrir::AssetOwner::App());
            for (Fenrir::AssetHandle<Fenrir::Material>* mapped : {&m_mapped, &m_twoSided})
            {
                *mapped = store.CreateMaterial(lit, Fenrir::AssetOwner::App());
                store.SetFloat(*mapped, "roughness", 1.0f);
                REQUIRE(store.SetTexture(*mapped, "normalTexture", normalMap));
            }
            store.SetDoubleSided(m_twoSided, true);
            m_quad = test.app.GetActiveScene().GetEntityList().CreateEntity();
            m_quad.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
                store.Add<Fenrir::Mesh>(TestQuad(Placement::Plain, false), Fenrir::AssetOwner::App()), m_flat, true});
        }

        /** @brief The centre pixel of the quad for `placement`, its normal made as `lighting` says. */
        GlProbe::Pixel Centre(Placement placement, Lighting lighting)
        {
            Fenrir::MeshRenderer& renderer = m_quad.GetComponent<Fenrir::MeshRenderer>();
            renderer.mesh = test.assets.Store().Add<Fenrir::Mesh>(
                TestQuad(placement, lighting != Lighting::DerivativeFrame), Fenrir::AssetOwner::App());
            renderer.material =
                lighting == Lighting::Flat ? m_flat : (placement == Placement::BackFacing ? m_twoSided : m_mapped);
            Fenrir::Transform& transform = m_quad.GetComponent<Fenrir::Transform>();
            transform.scale = placement == Placement::MirroredWorld ? Vec3(-1.0f, 1.0f, 1.0f) : Vec3(1.0f);
            // Half a turn takes the quad to z = +2, behind the camera, so it is moved back to z = -2.
            const bool back = placement == Placement::BackFacing;
            transform.rot =
                Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(back ? 180.0f : 0.0f), Vec3(0.0f, 1.0f, 0.0f));
            transform.pos = back ? Vec3(0.0f, 0.0f, -4.0f) : Vec3(0.0f);
            return test.Frame().At(Size / 2, Size / 2);
        }

        GlScene test;

      private:
        Fenrir::AssetHandle<Fenrir::Material> m_flat;
        Fenrir::AssetHandle<Fenrir::Material> m_mapped;
        Fenrir::AssetHandle<Fenrir::Material> m_twoSided;
        Fenrir::Entity m_quad;
    };

    // Where the tangent and derivative frames may differ: the derivative frame's rounding, and a byte of the map.
    constexpr float Tolerance = 0.02f;
} // namespace

TEST_CASE("A normal-mapped quad is lit the same through its tangent stream as through the derivative frame",
          "[renderer][gl][normal-maps]")
{
    NormalMapScene scene;
    const GlProbe::Pixel flat = scene.Centre(Placement::Plain, Lighting::Flat);
    const GlProbe::Pixel derivative = scene.Centre(Placement::Plain, Lighting::DerivativeFrame);
    const GlProbe::Pixel tangent = scene.Centre(Placement::Plain, Lighting::TangentStream);

    INFO("flat " << flat[0] << ", derivative frame " << derivative[0] << ", tangent stream " << tangent[0]);
    {
        INFO("the map turns the normal towards the light: N.L from 0.58 flat to nearly 1");
        REQUIRE(derivative[0] > flat[0] + 0.25f);
    }
    {
        INFO("and the tangent stream gives the derivative frame's colour");
        REQUIRE(GlProbe::Near(tangent, derivative, Tolerance));
    }
    INFO("GL reported no errors");
    REQUIRE(scene.test.messages->errors.empty());
}

TEST_CASE("A quad mirrored in U or by its world matrix, or seen from behind, keeps its tangent frame's handedness",
          "[renderer][gl][normal-maps]")
{
    NormalMapScene scene;
    // Mirrored, +U runs along -X while the image's up stays +Y: the mapped normal is (-0.6, 0.6, 0.53), N.L about 0.3,
    // and lost handedness would flip the bitangent to -Y and the normal to (-0.6, -0.6, 0.53), facing away: black.
    // From behind, the front's frame (U along -X, up +Y, the normal -Z) is turned over whole: (0.6, -0.6, 0.53), N.L
    // about 0.3 again, and lost handedness would give (0.6, 0.6, 0.53), N.L nearly 1.
    const Placement placement = GENERATE(Placement::MirroredUv, Placement::MirroredWorld, Placement::BackFacing);
    CAPTURE(placement == Placement::MirroredUv      ? "mirrored in U"
            : placement == Placement::MirroredWorld ? "mirrored by its world matrix"
                                                    : "seen from behind");
    const GlProbe::Pixel derivative = scene.Centre(placement, Lighting::DerivativeFrame);
    const GlProbe::Pixel tangent = scene.Centre(placement, Lighting::TangentStream);

    INFO("derivative frame " << derivative[0] << ", tangent stream " << tangent[0]);
    {
        INFO("the derivative frame lights it at N.L about 0.3");
        REQUIRE(derivative[0] > 0.2f);
        REQUIRE(derivative[0] < 0.4f);
    }
    {
        INFO("and the tangent stream gives the same colour");
        REQUIRE(GlProbe::Near(tangent, derivative, Tolerance));
    }
    INFO("GL reported no errors");
    REQUIRE(scene.test.messages->errors.empty());
}
