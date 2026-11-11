// Shadow views cull on OpenGL (ADR-0057, culling ticket 10), read back from each shadow view's command buffer and
// indirection: each cascade's counts and sets are exactly those Core/ShadowViews.hpp's cull volume keeps on the CPU
// over a seeded scene of casters, every survivor at the level SelectLod chooses for the camera, not the light; a spot
// light's cone and a point light's range sphere the same, the point light's instanced faces counting six a survivor;
// and a caster the camera cannot see but a cascade can still shadows the ground the camera sees, the same pixel for
// pixel with GPU culling off.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LevelOfDetail.hpp"
#include "Core/ShadowViews.hpp"
#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using Fenrir::Render::DrawnShadowView;
using Fenrir::Render::SelectLod;
using Fenrir::Render::ShadowViewKind;
using namespace Fenrir::Graphics;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);
    using RendererTest::NoCopy;

    /** @brief The `n`th word of command `command` of a command buffer's bytes. */
    std::uint32_t Word(const std::vector<std::byte>& commands, std::size_t command, std::size_t n)
    {
        return RendererTest::CommandWord(commands, command * CommandSize, n);
    }

    /** @brief One entry of the indirection: its record and copy. */
    using Entry = std::pair<std::uint32_t, std::uint32_t>;

    /** @brief Entry `i` of an indirection's bytes. */
    Entry EntryOf(const std::vector<std::byte>& entries, std::uint32_t i)
    {
        return {RendererTest::UintAt(entries, std::size_t{i} * 8),
                RendererTest::UintAt(entries, std::size_t{i} * 8 + 4)};
    }

    /** @brief The white square with two hand-made coarser levels, the same triangles at errors of 0.02 and 0.08. */
    Fenrir::MeshData ThreeLevelSquare()
    {
        Fenrir::MeshData mesh = RendererTest::Quad(0.4f, 0.0f);
        mesh.lods.push_back({mesh.indices, 0.02f});
        mesh.lods.push_back({mesh.indices, 0.08f});
        return mesh;
    }

    /**
     * @brief GlScene with a camera entity at the origin looking along -Z, its bloom off, a white unlit double-sided
     * material, a square of side 0.4 and a three-level one, culling by the frustum alone (the shadow views never
     * cull by occlusion, and the camera's second phase is not this test's subject).
     */
    class GlShadowCullScene : public GlTest::GlScene
    {
      public:
        GlShadowCullScene()
        {
            renderer.SetOcclusionCulling(false);
            camera = app.GetActiveScene().GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
            Fenrir::AssetStore& store = assets.Store();
            white = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
                                         Fenrir::AssetOwner::App());
            store.SetColour(white, "baseColour", Fenrir::Colour::White);
            store.SetDoubleSided(white, true);
            square = store.Add<Fenrir::Mesh>(RendererTest::Quad(0.4f, 0.0f), Fenrir::AssetOwner::App());
            levelled = ThreeLevelSquare();
            threeLevel = store.Add<Fenrir::Mesh>(levelled, Fenrir::AssetOwner::App());
        }

        Fenrir::Entity camera;
        Fenrir::AssetHandle<Fenrir::Material> white;
        Fenrir::AssetHandle<Fenrir::Mesh> square;
        Fenrir::MeshData levelled;
        Fenrir::AssetHandle<Fenrir::Mesh> threeLevel;

        /** @brief What SelectLod sees of the camera: at the origin, `Size` pixels high, perspective. */
        [[nodiscard]] Fenrir::Render::LodView CameraLod() const
        {
            const Fenrir::Perspective& perspective =
                camera.GetComponent<Fenrir::Camera>().projection.As<Fenrir::Perspective>();
            return {camera.GetComponent<Fenrir::Transform>().pos,
                    0.5f * static_cast<float>(Size) / std::tan(perspective.verticalFov * 0.5f), perspective.near, true};
        }
    };

    /** @brief The seeded scene: the list's copies (relative to its entity), its entity, and the lone squares. */
    struct SeededScene
    {
        std::vector<Fenrir::Math::Mat4> copies;
        Fenrir::Entity entity;
        std::vector<Vec3> lone;
    };

    /**
     * @brief One list of 150 three-level squares scattered around and behind the camera, each turned and scaled,
     * under an entity that is itself moved and turned, and eight lone squares that merge into one run, all casting.
     * Seeded, so every run of it is the same scene.
     */
    SeededScene AddSeededScene(GlShadowCullScene& test)
    {
        std::mt19937 random(23);
        std::uniform_real_distribution<float> across(-8.0f, 8.0f);
        std::uniform_real_distribution<float> along(-10.0f, 4.0f);
        std::uniform_real_distribution<float> angle(0.0f, 6.28f);
        std::uniform_real_distribution<float> scale(0.5f, 2.0f);
        Fenrir::InstanceListData list;
        for (int i = 0; i < 150; ++i)
        {
            const Fenrir::Math::Mat4 placed =
                Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {across(random), across(random), along(random)});
            const Fenrir::Math::Mat4 turned =
                glm::mat4_cast(Fenrir::Math::AngleAxis(angle(random), glm::normalize(Vec3(0.3f, 1.0f, 0.5f))));
            list.transforms.push_back(placed * turned * glm::scale(Fenrir::Math::Mat4(1.0f), Vec3(scale(random))));
        }
        SeededScene scene;
        scene.copies = list.transforms;
        const auto handle = test.assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
        scene.entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        scene.entity.GetComponent<Fenrir::Transform>().pos = {0.3f, -0.2f, -1.0f};
        scene.entity.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.4f, Vec3(0.0f, 1.0f, 0.0f));
        scene.entity.AddComponent<Fenrir::InstancedMeshRenderer>(
            Fenrir::InstancedMeshRenderer{test.threeLevel, test.white, handle, true, true});
        for (int i = 0; i < 8; ++i)
        {
            scene.lone.push_back({across(random), across(random), along(random)});
            Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = scene.lone.back();
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{test.square, test.white, true});
        }
        return scene;
    }

    /** @brief The sun, travelling along `direction`, white at intensity 1, casting. */
    Fenrir::DirectionalLight& AddSun(GlTest::GlScene& test, Vec3 direction)
    {
        Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot = glm::quatLookAt(
            glm::normalize(direction),
            std::abs(glm::normalize(direction).y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f));
        return sun.AddComponent<Fenrir::DirectionalLight>();
    }

    /** @brief Whether a shadow view's candidate of `box` under `transform` is inside its volume: planes and sphere. */
    bool Inside(const DrawnShadowView& view, const Fenrir::Math::AABB& box, const Fenrir::Math::Mat4& transform)
    {
        const Fenrir::Render::CullVolume& volume = view.volume;
        return volume.frustum.Intersects(box, transform) &&
               (volume.sphere.radius <= 0.0f || Fenrir::Render::WithinRange(volume.sphere, box, transform));
    }

    /** @brief Whether two LodViews are the same to a part in a million. */
    bool SameLod(const Fenrir::Render::LodView& a, const Fenrir::Render::LodView& b)
    {
        return glm::length(a.position - b.position) < 1e-5f &&
               std::abs(a.pixelsPerUnit - b.pixelsPerUnit) < 1e-3f * a.pixelsPerUnit &&
               std::abs(a.near - b.near) < 1e-6f && a.perspective == b.perspective;
    }

    /**
     * @brief Checks one shadow view's readback against the CPU oracle over the seeded scene: the list's three level
     * commands count and hold exactly the copies the volume keeps at the levels the camera chooses (each `perCopy`
     * instances), and the lone squares' run counts the lone squares the volume keeps. Returns how many copies the
     * view kept, so a caller can see the scene tested something.
     */
    std::size_t CheckView(GlShadowCullScene& test, const DrawnShadowView& view, const SeededScene& scene,
                          std::uint32_t perCopy)
    {
        CAPTURE(static_cast<int>(view.kind), view.index);
        INFO("the view culled the two runs and every caster, with no coarse cull by the camera");
        REQUIRE(view.cullRuns == 2);
        REQUIRE(view.cullCandidates == 158);
        INFO("and chose levels by the camera, not the light");
        REQUIRE(SameLod(view.lod, test.CameraLod()));

        const Fenrir::Math::AABB box = test.assets.Store().TryGet(test.threeLevel)->bounds;
        const Fenrir::Math::AABB loneBox = test.assets.Store().TryGet(test.square)->bounds;
        const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(scene.entity);
        const std::vector<float> errors{0.0f, test.levelled.lods[0].error, test.levelled.lods[1].error};
        std::array<std::set<std::uint32_t>, 3> expected;
        for (std::uint32_t i = 0; i < scene.copies.size(); ++i)
        {
            const Fenrir::Math::Mat4 transform = world * scene.copies[i];
            if (Inside(view, box, transform))
            {
                const std::optional<std::uint32_t> level = SelectLod(errors, box, transform, view.lod, 1.0f);
                REQUIRE(level.has_value());
                expected.at(*level).insert(i);
            }
        }
        std::uint32_t lonesKept = 0;
        for (const Vec3& position : scene.lone)
        {
            if (Inside(view, loneBox, Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), position)))
            {
                ++lonesKept;
            }
        }
        const std::size_t kept = expected[0].size() + expected[1].size() + expected[2].size();

        GraphicsDevice& device = RendererProbe::Device(test.renderer);
        const std::vector<std::byte> commands = GlProbe::ReadBuffer(device, view.cullCommands);
        const std::vector<std::byte> entries = GlProbe::ReadBuffer(device, view.cullIndirection);
        INFO("its command buffer holds the four commands: the list's three levels and the lone squares' one");
        REQUIRE(commands.size() >= 4 * CommandSize);
        // Which commands are the list's three levels: the one whose base instance is followed by one 150 entries on
        // (each level's region is the whole list long), and the next two; the other is the lone squares' run.
        std::optional<std::size_t> first;
        for (std::size_t c = 0; c + 1 < 4; ++c)
        {
            if (Word(commands, c + 1, 4) == Word(commands, c, 4) + 150)
            {
                first = c;
                break;
            }
        }
        INFO("the list's three level commands are found by their regions, each 150 entries long");
        REQUIRE(first.has_value());
        const std::size_t loneCommand = *first == 0 ? 3 : 0;
        for (std::uint32_t level = 0; level < 3; ++level)
        {
            CAPTURE(level);
            const std::size_t command = *first + level;
            INFO("the level's command counts exactly the copies the oracle keeps at it, " << perCopy << " each");
            REQUIRE(Word(commands, command, 1) == expected.at(level).size() * perCopy);
            INFO("and its region of the indirection holds exactly those copies, each once");
            std::set<std::uint32_t> found;
            const std::uint32_t base = Word(commands, command, 4);
            for (std::uint32_t i = 0; i < expected.at(level).size(); ++i)
            {
                const Entry entry = EntryOf(entries, base + i);
                REQUIRE(entry.second != NoCopy);
                REQUIRE(found.insert(entry.second).second);
            }
            REQUIRE(found == expected.at(level));
        }
        INFO("the lone squares' run counts the lone squares the volume keeps: " << lonesKept);
        REQUIRE(Word(commands, loneCommand, 1) == lonesKept * perCopy);
        for (std::uint32_t i = 0; i < lonesKept; ++i)
        {
            REQUIRE(EntryOf(entries, Word(commands, loneCommand, 4) + i).second == NoCopy);
        }
        return kept;
    }
} // namespace

TEST_CASE("Each cascade's cull keeps exactly the casters its open box keeps on the CPU, at the levels the camera "
          "chooses: the counts and the indirection match the oracle over a seeded scene",
          "[renderer][gl][culling][shadow-views][readback]")
{
    GlShadowCullScene test;
    test.RequireOpenGL();
    const SeededScene scene = AddSeededScene(test);
    // A low sun from one side, its cascades near, so the copies spread over and beyond them.
    Fenrir::DirectionalLight& sun = AddSun(test, {0.6f, -0.5f, -0.3f});
    sun.shadowDistance = 12.0f;

    test.RunFrame();
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    INFO("the camera's four cascades were culled");
    REQUIRE(views.size() == 4);
    std::size_t least = 150;
    std::size_t most = 0;
    for (const DrawnShadowView& view : views)
    {
        REQUIRE(view.kind == ShadowViewKind::Cascade);
        REQUIRE(view.view == 0);
        INFO("its planes are five: the box's sides and its far plane, open towards the sun");
        REQUIRE(view.volume.frustum.Planes().size() == 5);
        REQUIRE(view.volume.sphere.radius == 0.0f);
        const std::size_t kept = CheckView(test, view, scene, 1);
        least = std::min(least, kept);
        most = std::max(most, kept);
    }
    INFO("the scene is a real test: some cascade drops copies and some keeps many (" << least << " to " << most
                                                                                     << " of 150)");
    REQUIRE(least < 140);
    REQUIRE(most > 20);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A spot light's cone and a point light's range sphere keep exactly the casters the CPU keeps, the point "
          "light's instanced faces counting six a survivor",
          "[renderer][gl][culling][shadow-views][readback][spot-shadows][point-shadows]")
{
    GlShadowCullScene test;
    test.RequireOpenGL();
    const SeededScene scene = AddSeededScene(test);
    // A spot light above the copies pointing down, and a point light among them with a short range.
    Fenrir::Entity spot = test.app.GetActiveScene().GetEntityList().CreateEntity();
    spot.GetComponent<Fenrir::Transform>().pos = {1.0f, 9.0f, -3.0f};
    spot.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
    Fenrir::SpotLight spotLight;
    spotLight.castShadows = true;
    spotLight.range = 20.0f;
    spot.AddComponent<Fenrir::SpotLight>(spotLight);
    Fenrir::Entity point = test.app.GetActiveScene().GetEntityList().CreateEntity();
    point.GetComponent<Fenrir::Transform>().pos = {-1.0f, 0.5f, -4.0f};
    Fenrir::PointLight pointLight;
    pointLight.castShadows = true;
    pointLight.range = 4.0f;
    point.AddComponent<Fenrir::PointLight>(pointLight);

    test.RunFrame();
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    INFO("one spot view and one point view, no sun");
    REQUIRE(views.size() == 2);
    REQUIRE(views[0].kind == ShadowViewKind::Spot);
    REQUIRE(views[1].kind == ShadowViewKind::Point);
    INFO("the spot light's cone is five planes and no sphere");
    REQUIRE(views[0].volume.frustum.Planes().size() == 5);
    REQUIRE(views[0].volume.sphere.radius == 0.0f);
    const std::size_t spotKept = CheckView(test, views[0], scene, 1);
    INFO("the point light's volume is its range sphere alone, at its position");
    REQUIRE(views[1].volume.frustum.Planes().empty());
    REQUIRE(glm::length(views[1].volume.sphere.centre - Vec3(-1.0f, 0.5f, -4.0f)) < 1e-5f);
    REQUIRE(views[1].volume.sphere.radius == 4.0f);
    // Where the GPU draws the six faces as instances, each survivor counts six; without that, a face at a time, one.
    const bool instanced = RendererProbe::Device(test.renderer).Features().vertexLayer;
    const std::size_t pointKept = CheckView(test, views[1], scene, instanced ? 6u : 1u);
    INFO("the scene is a real test: each light keeps some copies and drops some (" << spotKept << " and " << pointKept
                                                                                   << " of 150)");
    REQUIRE(spotKept > 10);
    REQUIRE(spotKept < 140);
    REQUIRE(pointKept > 3);
    REQUIRE(pointKept < 100);
    REQUIRE(test.messages->errors.empty());
}

namespace
{
    /** @brief A lit material of `colour`, rough and dielectric, so the sun lights it alike from every side. */
    Fenrir::AssetHandle<Fenrir::Material> Lit(GlTest::GlScene& test, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        store.SetFloat(material, "roughness", 1.0f);
        return material;
    }

    /** @brief A box of `size` centred on `centre`. */
    void AddBox(GlTest::GlScene& test, Vec3 centre, Vec3 size, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh = test.assets.Store().Add<Fenrir::Mesh>(RendererTest::Cube(), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = centre;
        entity.GetComponent<Fenrir::Transform>().scale = size;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A floor of side `size` at y = -1 facing up, centred under (`x`, `z`). */
    void AddFloor(GlTest::GlScene& test, float size, float x, float z, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh =
            test.assets.Store().Add<Fenrir::Mesh>(RendererTest::Quad(size, 0.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = {x, -1.0f, z};
        entity.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief The linear value of an sRGB-encoded byte. */
    float Linear(std::uint8_t encoded)
    {
        const float c = static_cast<float>(encoded) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief Pixel (`x`, `y`) of `image`'s green, linear. */
    float GreenAt(const RendererTest::Rgba8Image& image, std::uint32_t x, std::uint32_t y)
    {
        return Linear(image.rgba.at((std::size_t{y} * image.width + x) * 4 + 1));
    }

    // The top-down probe of Shadows.cpp: a camera 6 units above the floor looking straight down, so floor point
    // (x, z) shows at pixel (32 + (x - CameraX) / HalfSpan x 32, 32 + (z - CameraZ) / HalfSpan x 32).
    constexpr std::uint32_t Side = 64;
    constexpr float CameraX = 1.5f;
    constexpr float CameraZ = -6.0f;
    const float HalfSpan = 6.0f * std::tan(1.04719755f * 0.5f);

    /** @brief The pixel the top-down camera shows floor point (`x`, `z`) at. */
    std::array<std::uint32_t, 2> FloorPixel(float x, float z)
    {
        const auto pixel = [](float offset) {
            return static_cast<std::uint32_t>(
                std::lround(static_cast<float>(Side) * 0.5f * (1.0f + offset / HalfSpan) - 0.5f));
        };
        return {pixel(x - CameraX), pixel(z - CameraZ)};
    }
} // namespace

TEST_CASE("A caster above and behind the camera, outside its view but inside a cascade, still shadows the floor the "
          "camera sees, the same with GPU culling off",
          "[renderer][gl][culling][shadow-views][shadows]")
{
    GlTest::GlScene test;
    test.RequireOpenGL();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.02f, 0.02f, 0.02f, 1.0f}});
    const auto white = Lit(test, Fenrir::Colour::White);
    AddFloor(test, 30.0f, 0.0f, -6.0f, white);
    // A unit box from y 6 to 7 over (-5, -6), above the camera at y 5 and so behind it; the sun travels +X and down
    // at 45 degrees, so its shadow on the floor at y = -1 runs from x 1.5 (its bottom's near edge, moved 7 along) to
    // 3.5 (its top's far edge, moved 8), between z -6.5 and -5.5: in the camera's view, which spans x -2 to 5.
    AddBox(test, {-5.0f, 6.5f, -6.0f}, Vec3(1.0f), white);
    AddSun(test, {1.0f, -1.0f, 0.0f});
    Fenrir::ImageData data;
    data.width = Side;
    data.height = Side;
    const auto target = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    Fenrir::Entity eye = test.app.GetActiveScene().GetEntityList().CreateEntity();
    eye.GetComponent<Fenrir::Transform>().pos = {CameraX, 5.0f, CameraZ};
    eye.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
    Fenrir::Camera& camera = eye.AddComponent<Fenrir::Camera>();
    camera.look.bloom.intensity = 0.0f;
    camera.target = Fenrir::ImageTarget{target};
    camera.look.tonemapper = Fenrir::Tonemapper::None;

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    const RendererTest::Rgba8Image culled = test.ReadTarget(target);
    INFO("the box is no candidate of the camera's cull, but is one of every cascade's");
    REQUIRE(RendererProbe::Culls(test.renderer).front().candidates == 1);
    for (const DrawnShadowView& view : RendererProbe::ShadowCulls(test.renderer))
    {
        REQUIRE(view.cullCandidates == 2);
    }
    const auto at = [&](const RendererTest::Rgba8Image& frame, float x, float z) {
        const std::array<std::uint32_t, 2> pixel = FloorPixel(x, z);
        return GreenAt(frame, pixel[0], pixel[1]);
    };
    for (const auto& [x, z] : {std::array<float, 2>{1.9f, -6.0f}, std::array<float, 2>{2.5f, -6.0f},
                               std::array<float, 2>{3.1f, -6.0f}, std::array<float, 2>{2.5f, -6.3f}})
    {
        CAPTURE(x, z);
        INFO("inside the shadow the floor has only the sky's light: " << at(culled, x, z));
        REQUIRE(at(culled, x, z) < 0.1f);
    }
    for (const auto& [x, z] : {std::array<float, 2>{0.5f, -6.0f}, std::array<float, 2>{4.5f, -6.0f},
                               std::array<float, 2>{2.5f, -7.2f}, std::array<float, 2>{2.5f, -4.8f}})
    {
        CAPTURE(x, z);
        INFO("outside it the floor is lit by the sun: " << at(culled, x, z));
        REQUIRE(at(culled, x, z) > 0.5f);
    }

    test.renderer.SetGpuCulling(false);
    test.RunFrame();
    REQUIRE(test.messages->errors.empty());
    const RendererTest::Rgba8Image unculled = test.ReadTarget(target);
    INFO("with GPU culling off the frame is the same pixel for pixel");
    REQUIRE(unculled.rgba.size() == culled.rgba.size());
    for (std::size_t i = 0; i < culled.rgba.size(); ++i)
    {
        if (culled.rgba[i] != unculled.rgba[i])
        {
            INFO("pixel byte " << i << ": " << int{culled.rgba[i]} << " culled, " << int{unculled.rgba[i]}
                               << " unculled");
            REQUIRE(culled.rgba[i] == unculled.rgba[i]);
        }
    }
}
