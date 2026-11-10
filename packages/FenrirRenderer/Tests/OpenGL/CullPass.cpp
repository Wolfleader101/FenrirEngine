// The cull pass on OpenGL (ADR-0057, culling ticket 03), read back from the camera's command buffer and indirection:
// with every candidate kept, each run's command counts its candidates and its region of the indirection holds each
// candidate's (record, copy) once, in whatever order the atomics gave them; and the draws through the indirection land
// where direct draws did, a user vertex shader's copies through CurrentDraw() among them.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirShaders/GlUserTint.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using namespace Fenrir::Graphics;

namespace
{
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);
    using RendererTest::NoCopy;

    /** @brief GlScene with a camera entity at the origin looking along -Z, its bloom off, and a white unlit square. */
    class GlCullScene : public GlTest::GlScene
    {
      public:
        GlCullScene()
        {
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
} // namespace

TEST_CASE("The cull pass keeps every candidate: each command counts its run's candidates, and its region of the "
          "indirection holds each (record, copy) once",
          "[renderer][gl][culling][cull-pass]")
{
    GlCullScene test;
    test.RequireOpenGL();
    // One instance list of five copies, three squares that merge into one run, and one lone square in a material of
    // its own: nine candidates in three runs.
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
