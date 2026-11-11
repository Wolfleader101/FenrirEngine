// The cull pass on OpenGL (ADR-0057, culling tickets 03 and 04), read back from the camera's command buffer and
// indirection: each run's command counts the candidates the pass kept and its region of the indirection holds each
// kept candidate's (record, copy) once, in whatever order the atomics gave them, the kept ones exactly those
// Math::Frustum keeps on the CPU over the same scene; and the draws through the indirection land where direct draws
// did, a user vertex shader's copies through CurrentDraw() among them, the same with GPU culling off; and the counts
// read back FramesInFlight frames later into instancesDrawn are the oracle's. Ticket 07: copies of a mesh with levels
// of detail land in the levels SelectLod gives, and a level of a different shape shows in the pixels.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LevelOfDetail.hpp"
#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirMath/Frustum.hpp"
#include "FenrirShaders/GlUserTint.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using Fenrir::Render::SelectLod;
using namespace Fenrir::Graphics;

namespace
{
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);
    using RendererTest::NoCopy;

    /**
     * @brief GlScene with a camera entity at the origin looking along -Z, its bloom off, and a white unlit square,
     * culling by the frustum alone: occlusion culling (ticket 11, OcclusionCulling.cpp) would hide the copies behind
     * others from the second frame on, which these tests' oracles do not model.
     */
    class GlCullScene : public GlTest::GlScene
    {
      public:
        GlCullScene()
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
        }

        Fenrir::Entity camera;
        Fenrir::AssetHandle<Fenrir::Material> white;
        Fenrir::AssetHandle<Fenrir::Mesh> square;

        /** @brief An entity at `position` drawing the square `offsets` times along its +X in `material`. */
        Fenrir::Entity AddList(std::span<const float> offsets, Vec3 position,
                               Fenrir::AssetHandle<Fenrir::Material> material)
        {
            Fenrir::InstanceListData list;
            for (const float x : offsets)
            {
                list.transforms.push_back(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {x, 0.0f, 0.0f}));
            }
            const auto handle = assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = position;
            entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                Fenrir::InstancedMeshRenderer{square, material, handle, true, true});
            return entity;
        }

        /** @brief A MeshRenderer of the square at `position` in `material`. */
        void AddSquare(Vec3 position, Fenrir::AssetHandle<Fenrir::Material> material)
        {
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = position;
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{square, material, true});
        }

        /** @brief The user shader fixture UserTint, built for these tests and embedded, as a game's own would load. */
        Fenrir::AssetHandle<Fenrir::Material> UserMaterial()
        {
            Fenrir::ShaderData data;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::GlUserTint));
            data.bytes.assign(bytes.begin(), bytes.end());
            const auto shader = assets.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
            REQUIRE_FALSE(shader.IsEmpty());
            const auto material = assets.Store().CreateMaterial(shader, Fenrir::AssetOwner::App());
            assets.Store().SetDoubleSided(material, true);
            return material;
        }

        /** @brief The pixel of `image` that `point` (world space) appears at. */
        [[nodiscard]] GlProbe::Pixel At(const GlProbe::Image& image, Vec3 point) const
        {
            const std::optional<Fenrir::Math::Vec2> screen = renderer.WorldToScreenPoint(camera, point);
            REQUIRE(screen.has_value());
            return image.At(static_cast<std::uint32_t>(screen->x), static_cast<std::uint32_t>(screen->y));
        }
    };

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

    /** @brief A bar `length` along X and `height` along Y, facing +Z, about the origin: a long thin box. */
    Fenrir::MeshData Bar(float length, float height)
    {
        Fenrir::MeshData mesh;
        const float w = length * 0.5f;
        const float h = height * 0.5f;
        mesh.positions = {Vec3(-w, -h, 0.0f), Vec3(w, -h, 0.0f), Vec3(w, h, 0.0f), Vec3(-w, h, 0.0f)};
        mesh.normals = {Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1)};
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }

    /** @brief The seeded scene: the list's copies (relative to its entity), the lone squares' positions and the entity.
     */
    struct SeededScene
    {
        std::vector<Fenrir::Math::Mat4> copies;
        std::vector<Vec3> lone;
        Fenrir::Entity entity;
    };

    /**
     * @brief One list of 150 squares scattered around and behind the camera, each turned and scaled, under an entity
     * that is itself moved and turned; eight lone white squares that merge into one run, some of them off screen; and
     * a red one in front. Seeded, so every run of it is the same scene.
     */
    SeededScene AddSeededScene(GlCullScene& test)
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
            Fenrir::InstancedMeshRenderer{test.square, test.white, handle, true, true});
        for (int i = 0; i < 8; ++i)
        {
            scene.lone.push_back({across(random), across(random), along(random)});
            test.AddSquare(scene.lone.back(), test.white);
        }
        const auto red = test.assets.Store().CreateMaterial(
            test.assets.Store().Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
            Fenrir::AssetOwner::App());
        test.assets.Store().SetColour(red, "baseColour", Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f});
        test.AddSquare({0.0f, 0.0f, -3.0f}, red);
        return scene;
    }
} // namespace

TEST_CASE("The cull pass keeps every candidate in view: each command counts its run's candidates, and its region of "
          "the indirection holds each (record, copy) once",
          "[renderer][gl][culling][cull-pass]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // One instance list of five copies, three squares that merge into one run, and one lone square in a material of
    // its own, all in view: nine candidates in three runs, every one kept.
    const std::array<float, 5> offsets{-1.6f, -0.8f, 0.0f, 0.8f, 1.6f};
    test.AddList(offsets, {0.0f, 0.8f, -4.0f}, test.white);
    for (const float x : {-0.8f, 0.0f, 0.8f})
    {
        test.AddSquare({x, 0.0f, -4.0f}, test.white);
    }
    const auto red = test.assets.Store().CreateMaterial(
        test.assets.Store().Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
        Fenrir::AssetOwner::App());
    test.assets.Store().SetColour(red, "baseColour", Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f});
    test.AddSquare({0.0f, -0.8f, -4.0f}, red);

    const GlProbe::Image image = test.Frame();
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    INFO("the one camera's cull pass counted three runs and nine candidates");
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().runs == 3);
    REQUIRE(culls.front().candidates == 9);
    const std::vector<std::byte> commands = GlProbe::ReadBuffer(device, culls.front().commands);
    const std::vector<std::byte> entries = GlProbe::ReadBuffer(device, culls.front().indirection);
    INFO("its command buffer and indirection read back with room for them");
    REQUIRE(commands.size() >= 3 * CommandSize);
    REQUIRE(entries.size() >= 9 * 8);

    std::set<Entry> all;
    std::multiset<std::uint32_t> counts;
    std::set<std::uint32_t> itemRecords;
    for (std::size_t command = 0; command < 3; ++command)
    {
        const std::uint32_t count = Word(commands, command, 1);
        const std::uint32_t base = Word(commands, command, 4);
        CAPTURE(command, count, base);
        INFO("the command's instance count is its run's candidates, every one kept");
        counts.insert(count);
        REQUIRE(base + count <= 9);
        std::set<std::uint32_t> records;
        std::set<std::uint32_t> copies;
        for (std::uint32_t i = base; i < base + count; ++i)
        {
            const Entry entry = EntryOf(entries, i);
            INFO("entry " << i << " is (" << entry.first << ", " << entry.second << ")");
            REQUIRE(all.insert(entry).second);
            records.insert(entry.first);
            copies.insert(entry.second);
        }
        if (count == 5)
        {
            INFO("the list's region names its one record with each of its five copies, consecutive in the copy buffer");
            REQUIRE(records.size() == 1);
            REQUIRE(copies.size() == 5);
            REQUIRE_FALSE(copies.contains(NoCopy));
            REQUIRE(*copies.rbegin() - *copies.begin() == 4);
        }
        else
        {
            INFO("an item run's region names each item's own record with no copy");
            REQUIRE(records.size() == count);
            REQUIRE(copies == std::set<std::uint32_t>{NoCopy});
            itemRecords.insert(records.begin(), records.end());
        }
    }
    REQUIRE(counts == std::multiset<std::uint32_t>{1, 3, 5});
    REQUIRE(all.size() == 9);
    REQUIRE(itemRecords.size() == 4);

    INFO("and the frame shows every candidate where a direct draw put it");
    for (const float x : offsets)
    {
        REQUIRE(GlProbe::Near(test.At(image, {x, 0.8f, -4.0f}), GlProbe::Pixel{1.0f, 1.0f, 1.0f, 1.0f}));
    }
    for (const float x : {-0.8f, 0.0f, 0.8f})
    {
        REQUIRE(GlProbe::Near(test.At(image, {x, 0.0f, -4.0f}), GlProbe::Pixel{1.0f, 1.0f, 1.0f, 1.0f}));
    }
    REQUIRE(GlProbe::Near(test.At(image, {0.0f, -0.8f, -4.0f}), GlProbe::Pixel{1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(image, {-0.4f, -0.8f, -4.0f}), Background));
    INFO("and nothing was logged as an error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A user vertex shader through CurrentDraw() draws an instance list's copies and a lone item alike",
          "[renderer][gl][culling][cull-pass]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // UserTint's colour is tint x strength plus its black pattern, the same for every copy, placed by CurrentDraw().
    const auto user = test.UserMaterial();
    INFO("the user material takes its tint and strength");
    REQUIRE(test.assets.Store().SetColour(user, "tint", Fenrir::Colour{0.2f, 0.6f, 0.4f, 1.0f}));
    REQUIRE(test.assets.Store().SetFloat(user, "strength", 1.0f));
    const std::array<float, 3> offsets{-0.8f, 0.0f, 0.8f};
    test.AddList(offsets, {0.0f, 0.6f, -4.0f}, user);
    test.AddSquare({0.0f, -0.6f, -4.0f}, user);

    const GlProbe::Image image = test.Frame();

    const GlProbe::Pixel lone = test.At(image, {0.0f, -0.6f, -4.0f});
    INFO("the lone item shows the tint, not the background");
    REQUIRE_FALSE(GlProbe::Near(lone, Background));
    for (const float x : offsets)
    {
        INFO("the copy at " << x << " shows the same colour as the lone item");
        REQUIRE(GlProbe::Near(test.At(image, {x, 0.6f, -4.0f}), lone));
        INFO("and nothing between the copies");
        REQUIRE(GlProbe::Near(test.At(image, {x + 0.4f, 0.6f, -4.0f}), Background));
    }
    REQUIRE(test.renderer.Stats().instances == 4);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE(
    "The cull pass keeps exactly the copies Math::Frustum keeps: each run's count and the indirection's set match "
    "the CPU oracle over a seeded random scene",
    "[renderer][gl][culling][cull-pass][frustum]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // The CPU keeps the list whole (its union reaches the view) and drops each lone square outside the view by its own
    // box, so the GPU's work shows in the list's count and set, and the lone squares in the totals.
    const auto [copies, lone, entity] = AddSeededScene(test);

    test.Frame();
    // The oracle: the same test on the CPU, over the same box, world matrix and copies.
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(test.camera);
    INFO("the camera has a frustum once a frame has resolved it");
    REQUIRE(frustum.has_value());
    const Fenrir::Math::AABB box = test.assets.Store().TryGet(test.square)->bounds;
    const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(entity);
    std::set<std::uint32_t> expected;
    for (std::uint32_t i = 0; i < copies.size(); ++i)
    {
        if (frustum->Intersects(box, world * copies[i]))
        {
            expected.insert(i);
        }
    }
    std::uint32_t lonesKept = 0;
    for (const Vec3& position : lone)
    {
        if (frustum->Intersects(Fenrir::Math::Moved(box, Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), position))))
        {
            ++lonesKept;
        }
    }
    INFO("the scene is a real test: some copies and some lone squares are outside the view, and some inside");
    REQUIRE(expected.size() > 20);
    REQUIRE(expected.size() < 130);
    REQUIRE(lonesKept > 0);
    REQUIRE(lonesKept < 8);

    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    REQUIRE(culls.size() == 1);
    INFO("the CPU dropped the lone squares outside the view before the pass: the list, the kept lone squares and the "
         "red one are the candidates, in three runs");
    REQUIRE(culls.front().runs == 3);
    REQUIRE(culls.front().candidates == 150 + lonesKept + 1);
    REQUIRE(test.renderer.Stats().instances == 150 + lonesKept + 1);
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const std::vector<std::byte> commands = GlProbe::ReadBuffer(device, culls.front().commands);
    const std::vector<std::byte> entries = GlProbe::ReadBuffer(device, culls.front().indirection);
    std::set<std::uint32_t> listCopies;
    std::uint32_t itemsKept = 0;
    std::multiset<std::uint32_t> itemCounts;
    std::set<Entry> all;
    for (std::size_t command = 0; command < 3; ++command)
    {
        const std::uint32_t count = Word(commands, command, 1);
        const std::uint32_t base = Word(commands, command, 4);
        CAPTURE(command, count, base);
        if (count == 0 || EntryOf(entries, base).second == NoCopy)
        {
            itemCounts.insert(count);
        }
        for (std::uint32_t i = base; i < base + count; ++i)
        {
            const Entry entry = EntryOf(entries, i);
            INFO("entry " << i << " is (" << entry.first << ", " << entry.second << "), kept once");
            REQUIRE(all.insert(entry).second);
            if (entry.second == NoCopy)
            {
                ++itemsKept;
            }
            else
            {
                INFO("the list's run counts what it kept");
                REQUIRE(count == expected.size());
                listCopies.insert(entry.second);
            }
        }
    }
    INFO("the list's region holds exactly the copies the CPU keeps (the one list's copies start at 0 in the buffer)");
    REQUIRE(listCopies == expected);
    INFO("and every lone square the CPU kept, its box under the identity, the GPU kept too: the merged run counts "
         "them and the red square's run counts one");
    REQUIRE(itemsKept == lonesKept + 1);
    REQUIRE(itemCounts == std::multiset<std::uint32_t>{lonesKept, 1});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Long thin copies tangent to the frustum are dropped by their corners where their spheres alone would keep "
          "them",
          "[renderer][gl][culling][cull-pass][frustum]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // Bars twenty units long and a tenth tall: three behind the camera a few units, whose spheres of radius ten reach
    // past it into the view, and two in front, which keep the list a candidate.
    const auto bar = test.assets.Store().Add<Fenrir::Mesh>(Bar(20.0f, 0.1f), Fenrir::AssetOwner::App());
    Fenrir::InstanceListData list;
    for (const float z : {1.0f, 2.0f, 3.0f, -4.0f, -6.0f})
    {
        list.transforms.push_back(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {0.0f, 0.0f, z}));
    }
    const std::vector<Fenrir::Math::Mat4> copies = list.transforms;
    const auto handle = test.assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
    Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
    entity.AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{bar, test.white, handle, true, true});

    test.Frame();
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(test.camera);
    REQUIRE(frustum.has_value());
    const Fenrir::Math::AABB box = test.assets.Store().TryGet(bar)->bounds;
    std::uint32_t bySphere = 0;
    std::uint32_t byBox = 0;
    for (const Fenrir::Math::Mat4& copy : copies)
    {
        bySphere += frustum->Intersects(Fenrir::Math::Frustum::SphereOf(box, copy)) ? 1 : 0;
        byBox += frustum->Intersects(box, copy) ? 1 : 0;
    }
    INFO("the spheres keep every bar, the corners only the two in front");
    REQUIRE(bySphere == 5);
    REQUIRE(byBox == 2);
    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    INFO("the one camera's pass culls the list's five bars as one run");
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().runs == 1);
    REQUIRE(culls.front().candidates == 5);
    const std::vector<std::byte> commands =
        GlProbe::ReadBuffer(RendererProbe::Device(test.renderer), culls.front().commands);
    INFO("the pass kept the two by their corners");
    REQUIRE(Word(commands, 0, 1) == byBox);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Copies inside and straddling the frustum's edges draw at their transforms and colours under a moved entity, "
          "and the frame is the same with GPU culling off",
          "[renderer][gl][culling][cull-pass][frustum]")
{
    GlCullScene test;
    test.RequireOpenGL();
    test.RunFrame();
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(test.camera);
    REQUIRE(frustum.has_value());
    // Where the right plane crosses y = 0 at z = -4: the view's right edge there.
    const Fenrir::Math::Vec4 right = frustum->Planes()[1];
    const float edge = (4.0f * right.z - right.w) / right.x;
    INFO("the right edge at four units is a little over two units out: " << edge);
    REQUIRE(edge > 2.0f);
    REQUIRE(edge < 3.0f);
    // Five copies, placed in world space then expressed under the entity, which is moved and turned about Z so each
    // square draws turned at its place: red and green inside, blue straddling the right edge (its centre a tenth
    // inside it, its turned corners a quarter past it), one wholly past the edge and one behind the camera.
    Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
    entity.GetComponent<Fenrir::Transform>().pos = {0.5f, -0.3f, 0.0f};
    entity.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.3f, Vec3(0.0f, 0.0f, 1.0f));
    const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(entity);
    const std::array<Vec3, 5> places{Vec3(-1.0f, 0.5f, -4.0f), Vec3(0.0f, -0.5f, -4.0f), Vec3(edge - 0.1f, 0.0f, -4.0f),
                                     Vec3(edge + 1.0f, 0.0f, -4.0f), Vec3(0.0f, 0.0f, 2.0f)};
    Fenrir::InstanceListData list;
    for (const Vec3& place : places)
    {
        list.transforms.push_back(glm::inverse(world) * Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), place) *
                                  glm::mat4_cast(entity.GetComponent<Fenrir::Transform>().rot));
    }
    list.colours = {Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f},
                    Fenrir::Colour{0.0f, 0.0f, 1.0f, 1.0f}, Fenrir::Colour::White, Fenrir::Colour::White};
    const auto handle = test.assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
    entity.AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{test.square, test.white, handle, true, true});

    const GlProbe::Image culled = test.Frame();
    INFO("the pass kept the three copies that reach the view");
    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().candidates == 5);
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    REQUIRE(Word(GlProbe::ReadBuffer(device, culls.front().commands), 0, 1) == 3);
    INFO("each kept copy shows its colour at its place, and nothing beside it");
    REQUIRE(GlProbe::Near(test.At(culled, places[0]), GlProbe::Pixel{1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(culled, places[1]), GlProbe::Pixel{0.0f, 1.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(culled, places[2]), GlProbe::Pixel{0.0f, 0.0f, 1.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(culled, places[0] + Vec3(0.0f, 0.5f, 0.0f)), Background));
    REQUIRE(GlProbe::Near(test.At(culled, places[1] + Vec3(0.5f, 0.0f, 0.0f)), Background));

    test.renderer.SetGpuCulling(false);
    const GlProbe::Image direct = test.Frame();
    INFO("with GPU culling off the pass keeps every copy");
    REQUIRE(Word(GlProbe::ReadBuffer(device, RendererProbe::Culls(test.renderer).front().commands), 0, 1) == 5);
    INFO("and the frame is the same, pixel for pixel");
    REQUIRE(direct.width == culled.width);
    REQUIRE(direct.height == culled.height);
    for (std::uint32_t y = 0; y < culled.height; ++y)
    {
        for (std::uint32_t x = 0; x < culled.width; ++x)
        {
            CAPTURE(x, y);
            REQUIRE(GlProbe::Near(direct.At(x, y), culled.At(x, y)));
        }
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("instancesDrawn equals the CPU oracle's count for the seeded scene, FramesInFlight frames after it was drawn",
          "[renderer][gl][culling][cull-pass][readback]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // Every frame of the still scene draws the same count.
    const auto [copies, lone, entity] = AddSeededScene(test);

    test.RunFrame();
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(test.camera);
    REQUIRE(frustum.has_value());
    const Fenrir::Math::AABB box = test.assets.Store().TryGet(test.square)->bounds;
    const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(entity);
    std::uint32_t expected = 1;
    for (const Fenrir::Math::Mat4& copy : copies)
    {
        expected += frustum->Intersects(box, world * copy) ? 1 : 0;
    }
    for (const Vec3& position : lone)
    {
        expected +=
            frustum->Intersects(Fenrir::Math::Moved(box, Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), position)))
                ? 1
                : 0;
    }
    INFO("the oracle keeps some of the scene and drops some: " << expected << " of "
                                                               << copies.size() + lone.size() + 1);
    REQUIRE(expected > 20);
    REQUIRE(expected < copies.size() + lone.size());
    const std::uint32_t candidates = test.renderer.Stats().instances;
    REQUIRE(candidates > expected);

    const std::uint32_t frames = RendererProbe::Device(test.renderer).FramesInFlight();
    INFO("until FramesInFlight frames have followed the first, no frame has left flight: nothing read back");
    REQUIRE(test.renderer.Stats().instancesDrawn == 0);
    for (std::uint32_t frame = 1; frame < frames; ++frame)
    {
        test.RunFrame();
        REQUIRE(test.renderer.Stats().instancesDrawn == 0);
    }
    test.RunFrame();
    INFO("then the first frame's commands are read back: the GPU drew what the oracle keeps, of the candidates "
         "submitted");
    REQUIRE(test.renderer.Stats().instancesDrawn == expected);
    REQUIRE(test.renderer.Stats().instances == candidates);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Frozen, the GPU keeps drawing what the frozen camera saw after the camera turns away, and the frustum's "
          "edges draw with no error",
          "[renderer][gl][culling][frozen]")
{
    GlCullScene test;
    test.RequireOpenGL();
    const std::array<float, 3> offsets{-0.8f, 0.0f, 0.8f};
    test.AddList(offsets, {0.0f, 0.0f, -4.0f}, test.white);
    const std::uint32_t frames = RendererProbe::Device(test.renderer).FramesInFlight();
    for (std::uint32_t frame = 0; frame <= frames; ++frame)
    {
        test.RunFrame();
    }
    INFO("the three copies in view are drawn");
    REQUIRE(test.renderer.Stats().instancesDrawn == 3);

    test.renderer.SetCullingFrozen(true);
    test.RunFrame();
    test.camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(3.0f, Vec3(0.0f, 1.0f, 0.0f));
    for (std::uint32_t frame = 0; frame <= frames; ++frame)
    {
        test.RunFrame();
    }
    INFO("turned away, the camera's pass still keeps the three, drawn off screen, and the edges drew without error");
    REQUIRE(test.renderer.Stats().instances == 3);
    REQUIRE(test.renderer.Stats().instancesDrawn == 3);
    REQUIRE(test.messages->errors.empty());
    test.renderer.SetCullingFrozen(false);
    for (std::uint32_t frame = 0; frame <= frames; ++frame)
    {
        test.RunFrame();
    }
    INFO("unfrozen, the list behind the camera is no candidate and nothing is drawn");
    REQUIRE(test.renderer.Stats().instances == 0);
    REQUIRE(test.renderer.Stats().instancesDrawn == 0);
}

namespace
{
    /** @brief The white square with two hand-made coarser levels, the same triangles at errors of 0.02 and 0.08. */
    Fenrir::MeshData ThreeLevelSquare()
    {
        Fenrir::MeshData mesh = RendererTest::Quad(0.4f, 0.0f);
        mesh.lods.push_back({mesh.indices, 0.02f});
        mesh.lods.push_back({mesh.indices, 0.08f});
        return mesh;
    }

    /**
     * @brief A square of side 0.4 whose level 1 is a square of side 0.16 about the same centre, at error 0.03: the
     * coarse level is a different shape, so which level drew shows in the pixels around the small square.
     */
    Fenrir::MeshData TwoShapeSquare()
    {
        Fenrir::MeshData mesh = RendererTest::Quad(0.4f, 0.0f);
        const Fenrir::MeshData inner = RendererTest::Quad(0.16f, 0.0f);
        mesh.positions.insert(mesh.positions.end(), inner.positions.begin(), inner.positions.end());
        mesh.normals.insert(mesh.normals.end(), inner.normals.begin(), inner.normals.end());
        mesh.lods.push_back({{4, 5, 6, 4, 6, 7}, 0.03f});
        return mesh;
    }

    /** @brief What SelectLod sees of the test camera: at the origin, `GlScene::Size` pixels high, perspective. */
    Fenrir::Render::LodView LodViewOf(const GlCullScene& test)
    {
        const Fenrir::Perspective& perspective =
            test.camera.GetComponent<Fenrir::Camera>().projection.As<Fenrir::Perspective>();
        return {test.camera.GetComponent<Fenrir::Transform>().pos,
                0.5f * static_cast<float>(GlTest::GlScene::Size) / std::tan(perspective.verticalFov * 0.5f),
                perspective.near, true};
    }
} // namespace

TEST_CASE("Copies of a three-level mesh in a line away from the camera land in the levels SelectLod gives, exactly, "
          "and drawnByLod reads them back",
          "[renderer][gl][culling][lod][readback]")
{
    GlCullScene test;
    test.RequireOpenGL();
    const Fenrir::MeshData data = ThreeLevelSquare();
    const auto mesh = test.assets.Store().Add<Fenrir::Mesh>(data, Fenrir::AssetOwner::App());
    // Twelve copies a unit apart along -Z, from one unit out to twelve, under an entity at the origin.
    Fenrir::InstanceListData list;
    for (int i = 1; i <= 12; ++i)
    {
        list.transforms.push_back(
            Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {0.0f, 0.0f, -static_cast<float>(i)}));
    }
    const std::vector<Fenrir::Math::Mat4> copies = list.transforms;
    const auto handle = test.assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().GetEntityList().CreateEntity().AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{mesh, test.white, handle, true, true});

    test.RunFrame();
    // The oracle: the levels' errors, the mesh's box and each copy's transform, under the camera as the test knows it.
    const std::vector<float> errors{0.0f, data.lods[0].error, data.lods[1].error};
    const Fenrir::Math::AABB box = test.assets.Store().TryGet(mesh)->bounds;
    const Fenrir::Render::LodView view = LodViewOf(test);
    std::array<std::set<std::uint32_t>, 3> expected;
    for (std::uint32_t copy = 0; copy < copies.size(); ++copy)
    {
        const std::optional<std::uint32_t> level = SelectLod(errors, box, copies[copy], view, 1.0f);
        REQUIRE(level.has_value());
        expected.at(*level).insert(copy);
    }
    INFO("the line crosses both switches: " << expected[0].size() << ", " << expected[1].size() << " and "
                                            << expected[2].size() << " copies at levels 0, 1 and 2");
    REQUIRE_FALSE(expected[0].empty());
    REQUIRE_FALSE(expected[1].empty());
    REQUIRE_FALSE(expected[2].empty());

    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().candidates == 12);
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const std::vector<std::byte> commands = GlProbe::ReadBuffer(device, culls.front().commands);
    const std::vector<std::byte> entries = GlProbe::ReadBuffer(device, culls.front().indirection);
    REQUIRE(commands.size() >= 3 * CommandSize);
    for (std::uint32_t level = 0; level < 3; ++level)
    {
        CAPTURE(level);
        INFO("the level's command counts exactly the copies the oracle puts at it");
        REQUIRE(Word(commands, level, 1) == expected.at(level).size());
        INFO("and its region of the indirection holds exactly those copies, each once");
        std::set<std::uint32_t> kept;
        const std::uint32_t base = Word(commands, level, 4);
        REQUIRE(base == level * 12);
        for (std::uint32_t i = 0; i < Word(commands, level, 1); ++i)
        {
            const Entry entry = EntryOf(entries, base + i);
            REQUIRE(entry.second != NoCopy);
            REQUIRE(kept.insert(entry.second).second);
        }
        REQUIRE(kept == expected.at(level));
    }

    const std::uint32_t frames = device.FramesInFlight();
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        test.RunFrame();
    }
    INFO("read back FramesInFlight frames later, drawnByLod counts the same per level, summing to instancesDrawn");
    const Fenrir::RendererStats stats = test.renderer.Stats();
    REQUIRE(stats.instancesDrawn == 12);
    REQUIRE(std::ranges::equal(
        stats.drawnByLod, std::array<std::uint32_t, 8>{static_cast<std::uint32_t>(expected[0].size()),
                                                       static_cast<std::uint32_t>(expected[1].size()),
                                                       static_cast<std::uint32_t>(expected[2].size()), 0, 0, 0, 0, 0}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A mesh whose level 1 is a smaller square draws its level 1 when far and level 0 when near, and level 0 at "
          "both with GPU culling off",
          "[renderer][gl][culling][lod]")
{
    GlCullScene test;
    test.RequireOpenGL();
    const auto mesh = test.assets.Store().Add<Fenrir::Mesh>(TwoShapeSquare(), Fenrir::AssetOwner::App());
    // Two of it in one run: level 1's error of 0.03 units projects under a pixel from 1.66 units out, so the near one,
    // 1.25 from the camera to its sphere, draws at full detail and the far one, 2.78 away, at level 1.
    const Vec3 near(-0.3f, 0.0f, -1.5f);
    const Vec3 far(0.6f, 0.0f, -3.0f);
    for (const Vec3& place : {near, far})
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = place;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, test.white, true});
    }
    // A point 0.14 units from each centre: inside the large square, outside the small one.
    const Vec3 beside(0.14f, 0.0f, 0.0f);
    constexpr GlProbe::Pixel White{1.0f, 1.0f, 1.0f, 1.0f};

    const GlProbe::Image culled = test.Frame();
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().runs == 1);
    INFO("the run's level 0 command counts the near square and its level 1 command the far one");
    std::vector<std::byte> commands = GlProbe::ReadBuffer(device, culls.front().commands);
    REQUIRE(Word(commands, 0, 1) == 1);
    REQUIRE(Word(commands, 1, 1) == 1);
    INFO("both centres are white; beside the near centre the large square shows, beside the far one nothing does");
    REQUIRE(GlProbe::Near(test.At(culled, near), White));
    REQUIRE(GlProbe::Near(test.At(culled, far), White));
    REQUIRE(GlProbe::Near(test.At(culled, near + beside), White));
    REQUIRE(GlProbe::Near(test.At(culled, far + beside), Background));

    test.renderer.SetGpuCulling(false);
    const GlProbe::Image direct = test.Frame();
    INFO("with GPU culling off both squares draw at level 0, so the large square shows beside both centres");
    commands = GlProbe::ReadBuffer(device, RendererProbe::Culls(test.renderer).front().commands);
    REQUIRE(Word(commands, 0, 1) == 2);
    REQUIRE(Word(commands, 1, 1) == 0);
    REQUIRE(GlProbe::Near(test.At(direct, near + beside), White));
    REQUIRE(GlProbe::Near(test.At(direct, far + beside), White));
    REQUIRE(test.messages->errors.empty());
}
