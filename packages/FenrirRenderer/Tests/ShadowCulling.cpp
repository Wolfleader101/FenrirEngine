// Shadow views cull their own casters (ADR-0057, culling ticket 10): each cascade of a camera's sun shadow, each
// shadowed spot light and each shadowed point light gets a Cull pass of its own, one phase, over the casting runs, its
// job carrying the light's volume (a cascade's box open towards the light, a spot light's cone, a point light's range
// sphere) and the camera's level-of-detail numbers, never the light's; the shadow passes then draw from their view's
// command buffer through its indirection, a point light's six faces from one. The volumes' maths is tested on the CPU
// first, as the oracle of the GL readback tests.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Cascades.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/ShadowViews.hpp"
#include "Device/CommandList.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirMath/Frustum.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::CascadeCullVolume;
using Fenrir::Render::CubeFaceCount;
using Fenrir::Render::DrawnShadowView;
using Fenrir::Render::ShadowViewKind;
using Fenrir::Render::WithinRange;
using namespace Fenrir::Graphics;
using RendererTest::CullGroup;
using RendererTest::RecordedCull;
using RendererTest::RecordedDraw;
using RendererTest::RecordedIndirectDraw;
using RendererTest::RendererTestApp;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);
    constexpr float Fov = 1.04719755f;
    constexpr float Aspect = 16.0f / 9.0f;
    constexpr float Near = 0.1f;

    /** @brief Whether two vectors are the same to a millionth. */
    bool Near4(const Vec4& a, const Vec4& b)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (std::abs(a[i] - b[i]) > 1e-6f)
            {
                return false;
            }
        }
        return true;
    }

    /** @brief A sun shining down and along -Z, as most scenes light. */
    Fenrir::Entity AddSun(RendererTestApp& test)
    {
        Fenrir::Entity sun = test.ActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot =
            glm::quatLookAt(glm::normalize(Vec3(0.3f, -1.0f, -0.4f)), Vec3(0, 1, 0));
        sun.AddComponent<Fenrir::DirectionalLight>();
        return sun;
    }

    /** @brief A spot light at `position` pointing straight down, casting. */
    Fenrir::Entity AddSpotLight(RendererTestApp& test, Vec3 position)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
        Fenrir::SpotLight light;
        light.castShadows = true;
        entity.AddComponent<Fenrir::SpotLight>(light);
        return entity;
    }

    /** @brief A point light at `position` of `range`, casting. */
    Fenrir::Entity AddPointLight(RendererTestApp& test, Vec3 position, float range = 10.0f)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        Fenrir::PointLight light;
        light.castShadows = true;
        light.range = range;
        entity.AddComponent<Fenrir::PointLight>(light);
        return entity;
    }

    /** @brief The user shader fixture, UserTint, whose program has no POINT_SHADOW variant. */
    Fenrir::AssetHandle<Fenrir::Shader> UserShader(RendererTestApp& test)
    {
        std::ifstream file(FENRIR_TEST_USER_SHADER, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        REQUIRE_FALSE(bytes.empty());
        Fenrir::ShaderData data;
        for (const char byte : bytes)
        {
            data.bytes.push_back(static_cast<std::byte>(byte));
        }
        return test.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
    }

    /** @brief The buffer a Cull pass counts into, its set 1 binding 2. */
    BufferHandle CommandsOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        return RendererTest::EntryAt(test.Device(), cull.target, 2).buffer;
    }

    /** @brief The Cull pass whose command buffer is `commands`. */
    const RecordedCull& CullOf(const RendererTestApp& test, const std::vector<RecordedCull>& culls,
                               BufferHandle commands)
    {
        const auto found =
            std::ranges::find_if(culls, [&](const RecordedCull& cull) { return CommandsOf(test, cull) == commands; });
        INFO("a shadow view with runs has a Cull pass over its command buffer");
        REQUIRE(found != culls.end());
        return *found;
    }

    /** @brief The bytes of a Cull pass's job block. */
    std::pair<std::vector<std::byte>, std::size_t> JobBytes(const RendererTestApp& test, const RecordedCull& cull)
    {
        const BindingGroupEntry job = RendererTest::EntryAt(test.Device(), cull.job, 0);
        return {RendererProbe::Bytes(test.Device(), job.buffer), job.offset};
    }

    /** @brief The planes a Cull pass's job block carries, as many as its count says. */
    std::vector<Vec4> JobPlanes(const RendererTestApp& test, const RecordedCull& cull)
    {
        const auto [bytes, offset] = JobBytes(test, cull);
        const std::uint32_t count = RendererTest::UintAt(bytes, offset + 8);
        REQUIRE(count <= 6);
        std::vector<Vec4> planes(count);
        REQUIRE(offset + 16 + count * sizeof(Vec4) <= bytes.size());
        std::memcpy(planes.data(), bytes.data() + offset + 16, count * sizeof(Vec4));
        return planes;
    }

    /** @brief The vec4 at byte `at` of a Cull pass's job block: 112 the camera, 128 the lod, 240 the sphere. */
    Vec4 JobVec4(const RendererTestApp& test, const RecordedCull& cull, std::size_t at)
    {
        const auto [bytes, offset] = JobBytes(test, cull);
        Vec4 vector{};
        for (int i = 0; i < 4; ++i)
        {
            vector[i] = RendererTest::FloatAt(bytes, offset + at + static_cast<std::size_t>(i) * 4);
        }
        return vector;
    }

    /** @brief The shadow views of `kind` the last frame culled, in order. */
    std::vector<DrawnShadowView> ShadowViewsOf(const RendererTestApp& test, ShadowViewKind kind)
    {
        std::vector<DrawnShadowView> views;
        for (const DrawnShadowView& view : RendererProbe::ShadowCulls(test.renderer))
        {
            if (view.kind == kind)
            {
                views.push_back(view);
            }
        }
        return views;
    }

    /** @brief The records the groups of a Cull pass name: each run's first, and every item of a merged run. */
    std::set<std::uint32_t> RecordsOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        std::set<std::uint32_t> records;
        for (const CullGroup& group : RendererTest::GroupsOf(test.Device(), cull))
        {
            const std::vector<std::byte> draws =
                RendererProbe::Bytes(test.Device(), RendererTest::EntryAt(test.Device(), cull.target, 0).buffer);
            const std::uint32_t copies = RendererTest::UintAt(
                draws, std::size_t{group.record} * RendererTest::RecordSize + RendererTest::RecordInstanceCountOffset);
            for (std::uint32_t c = 0; c < (copies == 0 ? group.count : 1u); ++c)
            {
                records.insert(group.record + c);
            }
        }
        return records;
    }

    /** @brief The buffer a draw's set 3 binds at binding 3: a view's indirection, or the one that says there is none.
     */
    BufferHandle DrawSetIndirection(const RendererTestApp& test, const std::array<BindingGroupHandle, 4>& groups)
    {
        return RendererTest::EntryAt(test.Device(), groups.at(3), 3).buffer;
    }

    /** @brief Whether `uses` declares `usage` on `buffer`. */
    bool Declares(const std::vector<PassResourceUse>& uses, BufferHandle buffer, ResourceUsage usage)
    {
        return std::ranges::any_of(uses, [&](const PassResourceUse& use) {
            return use.texture.IsEmpty() && use.buffer == buffer && use.usage == usage;
        });
    }

    /** @brief A cube that casts at x = -1, one that does not at x = 1, and the camera and sun over them. */
    void AddCasterAndHidden(RendererTestApp& test)
    {
        test.AddCamera();
        AddSun(test);
        const auto cube = test.Mesh();
        const auto lit = test.Material();
        test.AddMeshRenderer(cube, lit).GetComponent<Fenrir::Transform>().pos = {-1.0f, 0.0f, 0.0f};
        Fenrir::Entity hidden = test.AddMeshRenderer(cube, lit);
        hidden.GetComponent<Fenrir::Transform>().pos = {1.0f, 0.0f, 0.0f};
        hidden.GetComponent<Fenrir::MeshRenderer>().castShadows = false;
    }
} // namespace

TEST_CASE("A cascade's cull volume keeps a caster between the light and the cascade, and drops one beside it or "
          "beyond its far plane",
          "[renderer][culling][shadow-views]")
{
    // A camera at the origin looking along -Z under a sun straight down, its first cascade's sphere a little way in
    // front of it: the cascade's box runs from the sphere's bottom up towards the light by the sphere and the shadow
    // distance, and its cull volume leaves the top open.
    const Mat4 projection = Fenrir::Render::ReversedZPerspective(Fov, Aspect, Near);
    const std::vector<Fenrir::Render::Cascade> cascades =
        Fenrir::Render::FitCascades(Mat4(1.0f), projection, Vec3(0.0f, -1.0f, 0.0f), 20.0f, {4, 2048, 0.75f});
    INFO("the sun shadow fits four cascades");
    REQUIRE(cascades.size() == 4);
    const Fenrir::Render::Cascade& cascade = cascades.front();
    const Fenrir::Math::Frustum closed = Fenrir::Math::Frustum::FromViewProjection(cascade.viewProjection);
    const Fenrir::Math::Frustum open = CascadeCullVolume(cascade);
    INFO("the orthographic box has six planes; the cull volume its five but the near one");
    REQUIRE(closed.Planes().size() == 6);
    REQUIRE(open.Planes().size() == 5);
    for (std::size_t p = 0; p < 4; ++p)
    {
        REQUIRE(Near4(open.Planes()[p], closed.Planes()[p]));
    }
    REQUIRE(Near4(open.Planes()[4], closed.Planes()[5]));

    const Fenrir::Math::AABB unit{Vec3(-0.5f), Vec3(0.5f)};
    const auto at = [](Vec3 position) { return Fenrir::Math::Translate(Mat4(1.0f), position); };
    INFO("a caster in the cascade's sphere is kept by both");
    REQUIRE(closed.Intersects(unit, at(cascade.centre)));
    REQUIRE(open.Intersects(unit, at(cascade.centre)));
    INFO("a caster far above the cascade, towards the light and past the box's near plane, is kept by the cull "
         "volume alone: its shadow still falls into the cascade");
    const Vec3 above = cascade.centre + Vec3(0.0f, cascade.radius + 20.0f + 50.0f, 0.0f);
    REQUIRE_FALSE(closed.Intersects(unit, at(above)));
    REQUIRE(open.Intersects(unit, at(above)));
    INFO("a caster beside the cascade, outside its side planes, is dropped by both");
    const Vec3 beside = cascade.centre + Vec3(cascade.radius + 5.0f, 0.0f, 0.0f);
    REQUIRE_FALSE(closed.Intersects(unit, at(beside)));
    REQUIRE_FALSE(open.Intersects(unit, at(beside)));
    INFO("and one below its far plane, past the sphere away from the light, is dropped by both");
    const Vec3 below = cascade.centre - Vec3(0.0f, cascade.radius + 5.0f, 0.0f);
    REQUIRE_FALSE(closed.Intersects(unit, at(below)));
    REQUIRE_FALSE(open.Intersects(unit, at(below)));
}

TEST_CASE("A point light's range sphere keeps a box whose nearest point is within the range, by its sphere and then "
          "its corners",
          "[renderer][culling][shadow-views]")
{
    const Fenrir::Math::AABB unit{Vec3(-0.5f), Vec3(0.5f)};
    const Mat4 identity(1.0f);
    INFO("a box touching the range along an axis is kept, one a hair past it dropped");
    REQUIRE(WithinRange({{3.0f, 0.0f, 0.0f}, 2.5f}, unit, identity));
    REQUIRE_FALSE(WithinRange({{3.0f, 0.0f, 0.0f}, 2.49f}, unit, identity));
    INFO("a range centre inside the box is within it whatever the radius");
    REQUIRE(WithinRange({{0.2f, -0.3f, 0.1f}, 0.01f}, unit, identity));
    // Along the diagonal the nearest corner is (0.5, 0.5, 0.5), sqrt(3) x 2.5 = 4.33 from (3, 3, 3); the box's sphere
    // (radius 0.866) is 5.196 away, so a range of 4.2 fails the sphere test and 4.4 passes both.
    INFO("on the diagonal, the sphere test drops a range the corners would, and both keep a longer one");
    REQUIRE_FALSE(WithinRange({{3.0f, 3.0f, 3.0f}, 4.2f}, unit, identity));
    REQUIRE(WithinRange({{3.0f, 3.0f, 3.0f}, 4.4f}, unit, identity));
    INFO("the corners are moved by the transform: scaled by two the box reaches 1 along each axis");
    const Mat4 doubled = glm::scale(identity, Vec3(2.0f));
    REQUIRE(WithinRange({{3.0f, 0.0f, 0.0f}, 2.0f}, unit, doubled));
    REQUIRE_FALSE(WithinRange({{3.0f, 0.0f, 0.0f}, 1.99f}, unit, doubled));
    INFO("a range of 0 keeps nothing but a box about its centre (the pass itself reads 0 as no sphere)");
    REQUIRE_FALSE(WithinRange({{3.0f, 0.0f, 0.0f}, 0.0f}, unit, identity));
    REQUIRE(WithinRange({{0.0f, 0.0f, 0.0f}, 0.0f}, unit, identity));
}

TEST_CASE("RendererProbe on null: each cascade, shadowed spot light and shadowed point light gets one Cull dispatch, "
          "before the shadow passes",
          "[renderer][culling][shadow-views]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    test.AddMeshRenderer(test.Mesh(), test.Material());
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddSpotLight(test, {2.0f, 3.0f, 0.0f});
    AddPointLight(test, {-2.0f, 3.0f, 0.0f});

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("eight Cull passes: the camera's, four cascades', two spot lights' and one point light's");
    REQUIRE(std::ranges::count(passes, "Cull") == 8);
    INFO("every one before the first shadow pass");
    const auto firstShadow = std::ranges::find_if(passes, [](const std::string& label) {
        return label == "SpotShadows" || label == "PointShadows" || label == "ShadowCascades";
    });
    REQUIRE(firstShadow != passes.end());
    REQUIRE(std::count(passes.begin(), firstShadow, "Cull") == 8);
    const std::vector<RecordedCull> culls = test.Culls();
    REQUIRE(culls.size() == 8);
    for (const RecordedCull& cull : culls)
    {
        INFO("a compute pass of one copy of its templates and one dispatch");
        REQUIRE(cull.pass.queue == QueueKind::Compute);
        REQUIRE(cull.copies.size() == 1);
        REQUIRE(cull.dispatches.size() == 1);
        REQUIRE(cull.dispatches.front().groupsX == 1);
    }
    INFO("the probe lists the seven shadow views in that order, each with its own buffers, one run and one candidate");
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    REQUIRE(views.size() == 7);
    std::vector<BufferHandle> commands;
    for (const DrawnShadowView& view : views)
    {
        REQUIRE(view.cullRuns == 1);
        REQUIRE(view.cullCandidates == 1);
        REQUIRE_FALSE(view.cullCommands.IsEmpty());
        REQUIRE(view.cullCommands != RendererProbe::Culls(test.renderer).front().commands);
        REQUIRE(std::ranges::find(commands, view.cullCommands) == commands.end());
        commands.push_back(view.cullCommands);
        CullOf(test, culls, view.cullCommands);
    }
    REQUIRE(views[0].kind == ShadowViewKind::Spot);
    REQUIRE(views[1].kind == ShadowViewKind::Spot);
    REQUIRE(views[0].index == 0);
    REQUIRE(views[1].index == 1);
    REQUIRE(views[2].kind == ShadowViewKind::Point);
    for (std::uint32_t c = 0; c < 4; ++c)
    {
        REQUIRE(views[3 + c].kind == ShadowViewKind::Cascade);
        REQUIRE(views[3 + c].view == 0);
        REQUIRE(views[3 + c].index == c);
    }
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("RendererProbe on null: a cascade's job carries its box's five planes without the near one, a spot light's "
          "its cone's five, a point light's its range sphere and no plane",
          "[renderer][culling][shadow-views]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    test.AddMeshRenderer(test.Mesh(), test.Material());
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {-2.0f, 3.0f, 1.0f}, 7.0f);

    test.Frame();
    const std::vector<RecordedCull> culls = test.Culls();
    for (const DrawnShadowView& view : RendererProbe::ShadowCulls(test.renderer))
    {
        CAPTURE(static_cast<int>(view.kind), view.index);
        const RecordedCull& cull = CullOf(test, culls, view.cullCommands);
        const std::vector<Vec4> planes = JobPlanes(test, cull);
        INFO("the job's planes are the probe's, in order");
        REQUIRE(planes.size() == view.volume.frustum.Planes().size());
        for (std::size_t p = 0; p < planes.size(); ++p)
        {
            REQUIRE(Near4(planes[p], view.volume.frustum.Planes()[p]));
        }
        const Vec4 sphere = JobVec4(test, cull, 240);
        INFO("and its sphere is the probe's");
        REQUIRE(Near4(sphere, Vec4(view.volume.sphere.centre, view.volume.sphere.radius)));
        switch (view.kind)
        {
        case ShadowViewKind::Cascade: {
            INFO("five planes: the two pairs of sides facing each other, and one plane without a partner, the far "
                 "plane, whose inward normal points back along the sun towards it; the near plane is not there");
            REQUIRE(planes.size() == 5);
            std::size_t unpaired = 0;
            std::size_t pairs = 0;
            for (std::size_t a = 0; a < planes.size(); ++a)
            {
                bool paired = false;
                for (std::size_t b = 0; b < planes.size(); ++b)
                {
                    paired = paired || (a != b && glm::dot(Vec3(planes[a]), Vec3(planes[b])) < -0.999f);
                }
                if (paired)
                {
                    ++pairs;
                }
                else
                {
                    unpaired = a;
                }
            }
            REQUIRE(pairs == 4);
            const Vec3 sun = glm::normalize(Vec3(0.3f, -1.0f, -0.4f));
            REQUIRE(glm::dot(Vec3(planes[unpaired]), sun) < -0.999f);
            REQUIRE(view.volume.sphere.radius == 0.0f);
            break;
        }
        case ShadowViewKind::Spot: {
            INFO("the cone's five planes (an infinite perspective), keeping a point down the light and not one above");
            REQUIRE(planes.size() == 5);
            REQUIRE(view.volume.frustum.Intersects(Fenrir::Math::Sphere{{0.0f, 1.0f, 0.0f}, 0.1f}));
            REQUIRE_FALSE(view.volume.frustum.Intersects(Fenrir::Math::Sphere{{0.0f, 5.0f, 0.0f}, 0.1f}));
            REQUIRE_FALSE(view.volume.frustum.Intersects(Fenrir::Math::Sphere{{6.0f, 2.5f, 0.0f}, 0.1f}));
            REQUIRE(view.volume.sphere.radius == 0.0f);
            break;
        }
        case ShadowViewKind::Point: {
            INFO("no plane, and the light's position and range as the sphere");
            REQUIRE(planes.empty());
            REQUIRE(Near4(sphere, Vec4(-2.0f, 3.0f, 1.0f, 7.0f)));
            break;
        }
        }
    }
}

namespace
{
    /** @brief Two cameras, a sun, a cube, a spot light and a point light: every kind of shadow view, twice over. */
    void AddTwoCameraScene(RendererTestApp& test)
    {
        test.AddCamera();
        test.AddCamera({0.0f, 0.0f, 10.0f}).GetComponent<Fenrir::Camera>().viewport = {0.5f, 0.0f, 0.5f, 1.0f};
        AddSun(test);
        test.AddMeshRenderer(test.Mesh(), test.Material());
        AddSpotLight(test, {0.0f, 3.0f, 0.0f});
        AddPointLight(test, {-2.0f, 3.0f, 1.0f});
    }
} // namespace

TEST_CASE("RendererProbe on null: every shadow view chooses levels by its camera, a spot or point light by the first "
          "camera drawn",
          "[renderer][culling][shadow-views][lod]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    AddTwoCameraScene(test);

    test.Frame();
    const std::vector<RecordedCull> culls = test.Culls();
    const std::vector<RendererProbe::CullBuffers> cameras = RendererProbe::Culls(test.renderer);
    INFO("both cameras were culled");
    REQUIRE(cameras.size() == 2);
    const Vec4 firstCamera = JobVec4(test, CullOf(test, culls, cameras[0].commands), 112);
    const Vec4 secondCamera = JobVec4(test, CullOf(test, culls, cameras[1].commands), 112);
    const Vec4 firstLod = JobVec4(test, CullOf(test, culls, cameras[0].commands), 128);
    INFO("the two cameras are told apart by their positions in their jobs");
    REQUIRE_FALSE(Near4(firstCamera, secondCamera));
    REQUIRE(firstCamera.z == 5.0f);
    REQUIRE(secondCamera.z == 10.0f);
    for (const DrawnShadowView& view : RendererProbe::ShadowCulls(test.renderer))
    {
        CAPTURE(static_cast<int>(view.kind), view.view, view.index);
        const RecordedCull& cull = CullOf(test, culls, view.cullCommands);
        INFO("a cascade names its camera; a spot or point light names none, being every camera's");
        REQUIRE(view.view.has_value() == (view.kind == ShadowViewKind::Cascade));
        INFO("a cascade's job has its camera's position and projection; a spot or point light's the first camera's");
        const Vec4 expected = view.view == 1 ? secondCamera : firstCamera;
        REQUIRE(Near4(JobVec4(test, cull, 112), expected));
        INFO("with the error pixels, near distance and perspective flag the camera's cull has");
        REQUIRE(Near4(JobVec4(test, cull, 128), firstLod));
        INFO("and the probe's level view is that camera's");
        REQUIRE(view.lod.position == Vec3(expected));
    }
}

TEST_CASE("RendererProbe on null: with GPU culling off a shadow view's job carries no planes, no sphere and no error "
          "allowed, so every caster draws at level 0",
          "[renderer][culling][shadow-views][lod]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    AddTwoCameraScene(test);
    test.renderer.SetGpuCulling(false);

    test.Frame();
    const std::vector<RecordedCull> culls = test.Culls();
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    INFO("the shadow views are culled all the same, through the same passes");
    REQUIRE(views.size() == 10);
    for (const DrawnShadowView& view : views)
    {
        CAPTURE(static_cast<int>(view.kind), view.view, view.index);
        const RecordedCull& cull = CullOf(test, culls, view.cullCommands);
        INFO("no planes");
        REQUIRE(JobPlanes(test, cull).empty());
        INFO("a sphere of radius 0");
        REQUIRE(JobVec4(test, cull, 240).w == 0.0f);
        INFO("and no error allowed, so level 0");
        REQUIRE(JobVec4(test, cull, 128).x == 0.0f);
    }
}

TEST_CASE("RendererProbe on null: a renderer with castShadows off is a candidate of the camera's cull and of no shadow "
          "view's",
          "[renderer][culling][shadow-views]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    AddCasterAndHidden(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {-2.0f, 3.0f, 0.0f});

    test.Frame();
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    INFO("Opaque draws both cubes as one merged run of two");
    REQUIRE(opaque.size() == 1);
    REQUIRE(opaque.front().draw.instanceCount == 2);
    const std::vector<RecordedCull> culls = test.Culls();
    const RecordedCull& cameraCull = CullOf(test, culls, RendererProbe::Culls(test.renderer).front().commands);
    const std::set<std::uint32_t> cameraRecords = RecordsOf(test, cameraCull);
    INFO("the camera culls both cubes, one merged run of two");
    REQUIRE(cameraRecords.size() == 2);
    REQUIRE(RendererProbe::Culls(test.renderer).front().candidates == 2);
    // Which record is the hidden cube's: the one whose world matrix's position is x = 1.
    const std::vector<std::byte> records = test.DrawBuffer(opaque.front());
    std::uint32_t hiddenRecord = 0;
    bool found = false;
    for (const std::uint32_t record : cameraRecords)
    {
        if (RendererTest::FloatAt(records, std::size_t{record} * RendererTest::RecordSize +
                                               RendererTest::RecordPositionXOffset) == 1.0f)
        {
            hiddenRecord = record;
            found = true;
        }
    }
    REQUIRE(found);
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    REQUIRE(views.size() == 6);
    for (const DrawnShadowView& view : views)
    {
        CAPTURE(static_cast<int>(view.kind), view.index);
        INFO("each shadow view culls the one caster, and its group table never names the hidden cube's record");
        REQUIRE(view.cullCandidates == 1);
        const std::set<std::uint32_t> shadowRecords = RecordsOf(test, CullOf(test, culls, view.cullCommands));
        REQUIRE(shadowRecords.size() == 1);
        REQUIRE_FALSE(shadowRecords.contains(hiddenRecord));
        REQUIRE(cameraRecords.contains(*shadowRecords.begin()));
    }
}

TEST_CASE("RendererProbe on null: the shadow passes draw each run indirectly from their view's command buffer through "
          "its indirection, declaring both",
          "[renderer][culling][shadow-views]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    const auto cube = test.Mesh();
    test.AddMeshRenderer(cube, test.Material());
    test.AddMeshRenderer(cube, test.Material("builtin:unlit")).GetComponent<Fenrir::Transform>().pos = {2.0f, 0.0f,
                                                                                                        0.0f};
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {-2.0f, 3.0f, 0.0f});

    test.Frame();
    const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
    REQUIRE(views.size() == 6);
    const auto check = [&](std::string_view pass, const DrawnShadowView& view, std::size_t runs) {
        CAPTURE(pass, static_cast<int>(view.kind), view.index);
        std::vector<RecordedIndirectDraw> draws;
        for (const RecordedIndirectDraw& draw : test.IndirectDraws(pass))
        {
            if (draw.draw.buffer == view.cullCommands)
            {
                draws.push_back(draw);
            }
        }
        INFO("one indirect draw per run from the view's buffer, at the run's command, through its indirection");
        REQUIRE(draws.size() == runs);
        for (std::size_t r = 0; r < runs; ++r)
        {
            REQUIRE(draws[r].draw.offset == r * CommandSize);
            REQUIRE(draws[r].draw.drawCount == 1);
            REQUIRE(DrawSetIndirection(test, draws[r].groups) == view.cullIndirection);
            REQUIRE(Declares(draws[r].uses, view.cullCommands, ResourceUsage::IndirectRead));
            REQUIRE(Declares(draws[r].uses, view.cullIndirection, ResourceUsage::StorageRead));
        }
        INFO("and no direct draw");
        for (const RecordedDraw& draw : RendererTest::DrawsIn(RendererProbe::Submitted(test.renderer), pass))
        {
            REQUIRE(DrawSetIndirection(test, draw.groups) != RendererProbe::Culls(test.renderer).front().indirection);
        }
    };
    check("SpotShadows", views[0], 2);
    check("PointShadows", views[1], 2);
    for (std::size_t c = 0; c < 4; ++c)
    {
        check("ShadowCascades", views[2 + c], 2);
    }
    INFO("resolved through the group table, the shadow draws read as they did drawing directly");
    REQUIRE(test.Draws("ShadowCascades").size() == 8);
    REQUIRE(test.Draws("SpotShadows").size() == 2);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("RendererProbe on null: a point light's six faces draw from one command buffer, six instances a kept "
          "candidate when instanced and one a face otherwise",
          "[renderer][culling][shadow-views][point-shadows]")
{
    SECTION("instanced faces: one indirect draw of six instances a candidate")
    {
        RendererTestApp test(RendererTest::OcclusionOff());
        test.AddCamera();
        test.AddMeshRenderer(test.Mesh(), test.Material());
        AddPointLight(test, {0.0f, 3.0f, 0.0f});

        test.Frame();
        const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
        INFO("one shadow view, the point light's");
        REQUIRE(views.size() == 1);
        const std::vector<RecordedIndirectDraw> draws = test.IndirectDraws("PointShadows");
        INFO("one indirect draw from its command buffer");
        REQUIRE(draws.size() == 1);
        REQUIRE(draws.front().draw.buffer == views.front().cullCommands);
        INFO("the level table says each kept candidate counts six, which the resolved draw shows");
        REQUIRE(RendererTest::InstancesPerCandidate(test.Device(), test.Culls().at(0), 0) == CubeFaceCount);
        const std::vector<RecordedDraw> resolved = test.Draws("PointShadows");
        REQUIRE(resolved.size() == 1);
        REQUIRE(resolved.front().draw.instanceCount == CubeFaceCount);
        REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    }
    SECTION("a face at a time: six passes, each one indirect draw of the same command")
    {
        RendererTestApp test(RendererTest::OcclusionOff());
        RendererProbe::SetFeatures(test.Device(), {.vertexLayer = false});
        test.AddCamera();
        test.AddMeshRenderer(test.Mesh(), test.Material());
        AddPointLight(test, {0.0f, 3.0f, 0.0f});

        test.Frame();
        const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
        REQUIRE(views.size() == 1);
        REQUIRE(std::ranges::count(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)), "Cull") == 2);
        const std::vector<RecordedIndirectDraw> draws = test.IndirectDraws("PointShadowFaces");
        REQUIRE(draws.size() == CubeFaceCount);
        for (const RecordedIndirectDraw& draw : draws)
        {
            REQUIRE(draw.draw.buffer == views.front().cullCommands);
            REQUIRE(draw.draw.offset == 0);
            REQUIRE(DrawSetIndirection(test, draw.groups) == views.front().cullIndirection);
        }
        INFO("one instance a candidate, as the faces are drawn one at a time");
        REQUIRE(RendererTest::InstancesPerCandidate(test.Device(), test.Culls().at(0), 0) == 1);
        REQUIRE(test.Draws("PointShadowFaces").size() == CubeFaceCount);
        REQUIRE(test.Draws("PointShadowFaces").front().draw.instanceCount == 1);
    }
    SECTION("both at once: the instanced runs' commands first, the faced runs' after them, one cull of two runs")
    {
        RendererTestApp test(RendererTest::OcclusionOff());
        test.AddCamera();
        const auto cube = test.Mesh();
        test.AddMeshRenderer(cube, test.Material());
        const auto user = test.Store().CreateMaterial(UserShader(test), Fenrir::AssetOwner::App());
        test.AddMeshRenderer(cube, user).GetComponent<Fenrir::Transform>().pos = {2.0f, 0.0f, 0.0f};
        AddPointLight(test, {0.0f, 3.0f, 0.0f});

        test.Frame();
        const std::vector<DrawnShadowView>& views = RendererProbe::ShadowCulls(test.renderer);
        INFO("one shadow view over both runs, two candidates");
        REQUIRE(views.size() == 1);
        REQUIRE(views.front().cullRuns == 2);
        REQUIRE(views.front().cullCandidates == 2);
        const std::vector<RecordedIndirectDraw> instanced = test.IndirectDraws("PointShadows");
        const std::vector<RecordedIndirectDraw> faced = test.IndirectDraws("PointShadowFaces");
        INFO("one instanced draw, and a face at a time for the other run");
        REQUIRE(instanced.size() == 1);
        REQUIRE(faced.size() == CubeFaceCount);
        INFO("the lit cube's command is the first, drawn six a candidate; the user shader's the second, one a face");
        REQUIRE(instanced.front().draw.offset == 0);
        for (const RecordedIndirectDraw& draw : faced)
        {
            REQUIRE(draw.draw.buffer == views.front().cullCommands);
            REQUIRE(draw.draw.offset == CommandSize);
        }
        const std::vector<RecordedCull> culls = test.Culls();
        const RecordedCull& cull = CullOf(test, culls, views.front().cullCommands);
        REQUIRE(RendererTest::InstancesPerCandidate(test.Device(), cull, 0) == CubeFaceCount);
        REQUIRE(RendererTest::InstancesPerCandidate(test.Device(), cull, 1) == 1);
        REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    }
}

TEST_CASE("RendererProbe on null: the shadow views' templates and groups follow the cameras' in the frame's rings, "
          "and a frame with no caster adds no shadow view",
          "[renderer][culling][shadow-views]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    Fenrir::Entity cube = test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<RecordedCull> culls = test.Culls();
    REQUIRE(culls.size() == 5);
    INFO("each cascade's templates lie after the camera's in the template ring, one command each");
    std::set<std::size_t> offsets;
    for (const RecordedCull& cull : culls)
    {
        REQUIRE(cull.copies.front().size == CommandSize);
        REQUIRE(offsets.insert(cull.copies.front().sourceOffset).second);
    }
    REQUIRE(offsets == std::set<std::size_t>{0, CommandSize, 2 * CommandSize, 3 * CommandSize, 4 * CommandSize});

    cube.GetComponent<Fenrir::MeshRenderer>().castShadows = false;
    test.Frame();
    INFO("with nothing casting there is no shadow pass, and no shadow view");
    REQUIRE(test.Culls().size() == 1);
    REQUIRE(RendererProbe::ShadowCulls(test.renderer).empty());
    REQUIRE(RendererTest::Labelled(test.Passes(), "ShadowCascades").empty());
}
