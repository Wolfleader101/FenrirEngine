// The cull pass on the null backend through App::RunFrame (ADR-0057, culling tickets 03 and 04): each camera's Cull
// pass, first in the frame, copies its runs' command templates over its command buffer and dispatches builtin:cull
// once over its group table, its job block carrying the camera's planes; DepthPrepass and Opaque then draw each run
// with one indirect draw of its commands, one per level of detail of its mesh, reading the camera's indirection
// through the draw set, while the transparent passes draw directly (the shadow passes draw from their own shadow
// views' buffers, ticket 10, ShadowCulling.cpp). An entity outside the camera is no candidate at all. The null backend
// runs no compute, so what it shows is the templates, the group table, the planes and the draws, never the survivors.
// Ticket 05: a CullReadback pass copies each camera's commands into the frame's slot of the readback ring, read
// FramesInFlight frames later into instancesDrawn, and frozen culling keeps each camera's planes while its matrices
// follow it. Ticket 07: a mesh with levels of detail uploads them into one index buffer, each run gets a template per
// level, the job block carries the camera's position and projection for choosing one, and drawnByLod sums the readback
// by level. Every test here runs with occlusion culling off (ticket 11): the one-phase layout is its subject, and
// OcclusionCulling.cpp has the two.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LevelOfDetail.hpp"
#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirMath/Frustum.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using namespace Fenrir::Graphics;
using RendererTest::CullGroup;
using RendererTest::RecordedCull;
using RendererTest::RecordedDraw;
using RendererTest::RecordedIndirectDraw;
using RendererTest::RendererTestApp;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);

    /** @brief `count` copies along +X, a unit apart. */
    Fenrir::InstanceListData Row(std::size_t count)
    {
        Fenrir::InstanceListData list;
        for (std::size_t i = 0; i < count; ++i)
        {
            list.transforms.push_back(
                Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {static_cast<float>(i), 0.0f, 0.0f}));
        }
        return list;
    }

    /** @brief An entity drawing a list of `copies` cubes in `material`. */
    Fenrir::Entity AddList(RendererTestApp& test, std::size_t copies, Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                           Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto list = test.Store().Add<Fenrir::InstanceList>(Row(copies), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::InstancedMeshRenderer>(
            Fenrir::InstancedMeshRenderer{mesh, material, list, true, true});
        return entity;
    }

    /** @brief A sun casting shadows, so the frame has a ShadowCascades pass. */
    void AddSun(RendererTestApp& test)
    {
        Fenrir::Entity sun = test.ActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot =
            glm::quatLookAt(glm::normalize(Fenrir::Math::Vec3(0.3f, -1.0f, -0.4f)), Fenrir::Math::Vec3(0, 1, 0));
        sun.AddComponent<Fenrir::DirectionalLight>();
    }

    /**
     * @brief The scene the ticket names: one instance list of ten cubes, one merged run of three cubes, and one lone
     * quad in a material of its own, fourteen candidates in three runs.
     */
    void AddThreeRuns(RendererTestApp& test)
    {
        const auto cube = test.Mesh();
        const auto lit = test.Material();
        AddList(test, 10, cube, lit);
        for (int i = 0; i < 3; ++i)
        {
            test.AddMeshRenderer(cube, lit).GetComponent<Fenrir::Transform>().pos = {static_cast<float>(i), 2.0f, 0.0f};
        }
        test.AddMeshRenderer(test.Mesh(RendererTest::Quad()), test.Material("builtin:unlit"));
    }

    /** @brief Whether `uses` declares `usage` on `buffer`. */
    bool Declares(const std::vector<PassResourceUse>& uses, BufferHandle buffer, ResourceUsage usage)
    {
        return std::ranges::any_of(uses, [&](const PassResourceUse& use) {
            return use.texture.IsEmpty() && use.buffer == buffer && use.usage == usage;
        });
    }

    /** @brief The `n`th word of command `command` of the command buffer bytes. */
    std::uint32_t Word(const std::vector<std::byte>& commands, std::size_t command, std::size_t n)
    {
        return RendererTest::CommandWord(commands, command * CommandSize, n);
    }

    /** @brief The buffer the camera's Cull pass counts into, its set 1 binding 2, and its indirection, binding 3. */
    BufferHandle CommandsOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        return RendererTest::EntryAt(test.Device(), cull.target, 2).buffer;
    }
    BufferHandle IndirectionOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        return RendererTest::EntryAt(test.Device(), cull.target, 3).buffer;
    }

    /** @brief The planes a Cull pass's job block carries, as many as its count says (0 with GPU culling off). */
    std::vector<Fenrir::Math::Vec4> JobPlanes(const RendererTestApp& test, const RecordedCull& cull)
    {
        const BindingGroupEntry job = RendererTest::EntryAt(test.Device(), cull.job, 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), job.buffer);
        const std::uint32_t count = RendererTest::UintAt(bytes, job.offset + 8);
        INFO("a job names at most the six planes it has room for");
        REQUIRE(count <= 6);
        std::vector<Fenrir::Math::Vec4> planes(count);
        REQUIRE(job.offset + 16 + count * sizeof(Fenrir::Math::Vec4) <= bytes.size());
        std::memcpy(planes.data(), bytes.data() + job.offset + 16, count * sizeof(Fenrir::Math::Vec4));
        return planes;
    }

    /** @brief Whether two planes are the same to a millionth. */
    bool Near(const Fenrir::Math::Vec4& a, const Fenrir::Math::Vec4& b)
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

    /** @brief The buffer a draw's set 3 binds at binding 3: a view's indirection, or the one that says there is none.
     */
    BufferHandle DrawSetIndirection(const RendererTestApp& test, const std::array<BindingGroupHandle, 4>& groups)
    {
        return RendererTest::EntryAt(test.Device(), groups.at(3), 3).buffer;
    }
} // namespace

TEST_CASE("Each camera's Cull pass copies its templates and dispatches once, first in the frame and before the shadow "
          "passes",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    AddThreeRuns(test);

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("the Cull passes run first, before ShadowCascades, DepthPrepass and Opaque: the camera's, then one per "
         "cascade of its sun shadow (ticket 10)");
    REQUIRE(passes.front() == "Cull");
    REQUIRE(std::ranges::count(passes, "Cull") == 5);
    REQUIRE(std::ranges::find(passes, "ShadowCascades") != passes.end());
    const std::vector<RecordedCull> culls = test.Culls();
    INFO("the camera's Cull pass comes first of the five");
    REQUIRE(culls.size() == 5);
    const RecordedCull& cull = culls.front();
    INFO("a compute pass of one copy and one dispatch, running builtin:cull");
    REQUIRE(cull.pass.queue == QueueKind::Compute);
    REQUIRE(cull.copies.size() == 1);
    REQUIRE(cull.dispatches.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), cull.pipeline) == "Cull");
    INFO("the copy lays the three runs' templates over the camera's command buffer, from the frame's template ring");
    const BufferHandle commands = CommandsOf(test, cull);
    REQUIRE(cull.copies.front().destination == commands);
    REQUIRE(cull.copies.front().destinationOffset == 0);
    REQUIRE(cull.copies.front().size == 3 * CommandSize);
    REQUIRE(RendererProbe::Label(test.Device(), cull.copies.front().source) == "Cull templates");
    INFO("the pass declares the copy, its reads, and its writes of the commands and the indirection");
    REQUIRE(Declares(cull.pass.uses, cull.copies.front().source, ResourceUsage::CopySource));
    REQUIRE(Declares(cull.pass.uses, commands, ResourceUsage::CopyDestination));
    REQUIRE(Declares(cull.pass.uses, commands, ResourceUsage::StorageWrite));
    REQUIRE(Declares(cull.pass.uses, IndirectionOf(test, cull), ResourceUsage::StorageWrite));
    REQUIRE(Declares(cull.pass.uses, RendererTest::EntryAt(test.Device(), cull.target, 1).buffer,
                     ResourceUsage::StorageRead));
    REQUIRE(Declares(cull.pass.uses, RendererTest::EntryAt(test.Device(), cull.target, 0).buffer,
                     ResourceUsage::StorageRead));
    INFO("the probe sees the camera's buffers, three runs and fourteen candidates");
    const std::vector<RendererProbe::CullBuffers> probed = RendererProbe::Culls(test.renderer);
    REQUIRE(probed.size() == 1);
    REQUIRE(probed.front().commands == commands);
    REQUIRE(probed.front().indirection == IndirectionOf(test, cull));
    REQUIRE(probed.front().runs == 3);
    REQUIRE(probed.front().candidates == 14);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The templates and the group table describe a frame of one instance list, one merged run and one lone item",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    const RecordedCull cull = test.Culls().at(0);
    const std::vector<std::byte> commands = RendererProbe::Bytes(test.Device(), CommandsOf(test, cull));
    const std::vector<CullGroup> groups = RendererTest::GroupsOf(test.Device(), cull);
    INFO("one group per run, each run under 64 candidates, and the dispatch runs that many");
    REQUIRE(groups.size() == 3);
    REQUIRE(cull.dispatches.front().groupsX == 3);
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    REQUIRE(opaque.size() == 3);
    std::set<std::uint32_t> commandIndices;
    std::set<std::uint32_t> counts;
    std::uint32_t total = 0;
    for (const CullGroup& group : groups)
    {
        CAPTURE(group.command, group.record, group.count, group.first);
        INFO("each group names its run's command, culls it from its first candidate, and gives its count");
        REQUIRE(group.command < 3);
        REQUIRE(group.first == 0);
        commandIndices.insert(group.command);
        counts.insert(group.count);
        INFO("the template has the run's mesh's index count, no instances, first index 0 and base vertex 0");
        const std::uint32_t indexCount = Word(commands, group.command, 0);
        REQUIRE(indexCount == (group.count == 1 ? 6u : 36u));
        REQUIRE(Word(commands, group.command, 1) == 0);
        REQUIRE(Word(commands, group.command, 2) == 0);
        REQUIRE(Word(commands, group.command, 3) == 0);
        INFO("and its base instance is where the run's region of the indirection starts: the candidates before it");
        REQUIRE(Word(commands, group.command, 4) == total);
        total += group.count;
        INFO("the run's first record is the draw's record, as the pass draws it");
        const auto draw =
            std::ranges::find_if(opaque, [&](const RecordedDraw& d) { return d.draw.firstInstance == group.record; });
        REQUIRE(draw != opaque.end());
        REQUIRE(draw->draw.instanceCount == group.count);
        INFO("an instance list's record says its copies; an item's says none");
        const std::vector<std::byte> records = test.DrawBuffer(*draw);
        const std::uint32_t listCopies =
            RendererTest::UintAt(records, std::size_t{group.record} * RendererTest::RecordSize + 140);
        REQUIRE(listCopies == (group.count == 10 ? 10u : 0u));
    }
    REQUIRE(commandIndices == std::set<std::uint32_t>{0, 1, 2});
    REQUIRE(counts == std::set<std::uint32_t>{10, 3, 1});
    REQUIRE(total == 14);
    INFO("the indirection holds the fourteen entries, eight bytes each");
    REQUIRE(RendererProbe::Bytes(test.Device(), IndirectionOf(test, cull)).size() >= 14 * 8);
}

TEST_CASE("A run of more than 64 candidates is several groups of its own, none straddling another run",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const auto cube = test.Mesh();
    AddList(test, 130, cube, test.Material());
    test.AddMeshRenderer(cube, test.Material());

    test.Frame();
    const RecordedCull cull = test.Culls().at(0);
    std::vector<CullGroup> groups = RendererTest::GroupsOf(test.Device(), cull);
    INFO("three groups for the list's 130 copies, one for the cube, and the dispatch runs four");
    REQUIRE(groups.size() == 4);
    REQUIRE(cull.dispatches.front().groupsX == 4);
    std::ranges::sort(groups, [](const CullGroup& a, const CullGroup& b) {
        return a.count != b.count ? a.count > b.count : a.first < b.first;
    });
    for (std::size_t g = 0; g < 3; ++g)
    {
        REQUIRE(groups[g].count == 130);
        REQUIRE(groups[g].first == g * 64);
        REQUIRE(groups[g].command == groups[0].command);
        REQUIRE(groups[g].record == groups[0].record);
    }
    REQUIRE(groups[3].count == 1);
    REQUIRE(groups[3].first == 0);
    REQUIRE(groups[3].command != groups[0].command);
}

TEST_CASE("DepthPrepass and Opaque draw the same runs, each one indirect draw of its command from the camera's buffer",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    const RecordedCull cull = test.Culls().at(0);
    const BufferHandle commands = CommandsOf(test, cull);
    const BufferHandle indirection = IndirectionOf(test, cull);
    const std::vector<RecordedIndirectDraw> prepass = test.IndirectDraws("DepthPrepass");
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    INFO("one indirect draw per run in each pass, and no direct draw");
    REQUIRE(prepass.size() == 3);
    REQUIRE(opaque.size() == 3);
    REQUIRE(RendererTest::DrawsIn(RendererProbe::Submitted(test.renderer), "Opaque").empty());
    for (std::size_t r = 0; r < 3; ++r)
    {
        CAPTURE(r);
        INFO("the prepass's draw and Opaque's name the same command, one each at the run's index");
        REQUIRE(prepass[r].draw == opaque[r].draw);
        REQUIRE(opaque[r].draw.buffer == commands);
        REQUIRE(opaque[r].draw.offset == r * CommandSize);
        REQUIRE(opaque[r].draw.drawCount == 1);
        REQUIRE(opaque[r].draw.stride == CommandSize);
        INFO("with the same mesh, through the camera's own draw set, whose binding 3 is the camera's indirection");
        REQUIRE(prepass[r].indexBuffer == opaque[r].indexBuffer);
        REQUIRE(DrawSetIndirection(test, opaque[r].groups) == indirection);
        REQUIRE(DrawSetIndirection(test, prepass[r].groups) == indirection);
        INFO("both passes declare IndirectRead on the commands and StorageRead on the indirection");
        for (const RecordedIndirectDraw* draw : {&prepass[r], &opaque[r]})
        {
            REQUIRE(Declares(draw->uses, commands, ResourceUsage::IndirectRead));
            REQUIRE(Declares(draw->uses, indirection, ResourceUsage::StorageRead));
        }
    }
    INFO("the resolved draws read as direct ones did: three runs covering the fourteen items");
    std::uint32_t instances = 0;
    for (const RecordedDraw& draw : test.Draws("Opaque"))
    {
        instances += draw.draw.instanceCount;
    }
    REQUIRE(instances == 14);
    REQUIRE(test.renderer.Stats().draws == 3);
    REQUIRE(test.renderer.Stats().instances == 14);
    INFO("the null backend's checks of the indirect draws all pass");
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An item with no prepass draw keeps its command in the camera's buffer, which only Opaque draws",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    // A Masked material on a shader with no base colour (the user shader fixture, UserTint) cannot be cut out in the
    // prepass, so Opaque writes its depth.
    std::ifstream file(FENRIR_TEST_USER_SHADER, std::ios::binary);
    Fenrir::ShaderData data;
    for (const char byte : std::vector<char>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()})
    {
        data.bytes.push_back(static_cast<std::byte>(byte));
    }
    const auto masked = test.Store().CreateMaterial(
        test.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App()), Fenrir::AssetOwner::App());
    test.Store().SetBlendMode(masked, Fenrir::BlendMode::Masked);
    const auto cube = test.Mesh();
    test.AddMeshRenderer(cube, test.Material());
    test.AddMeshRenderer(cube, masked);

    test.Frame();
    const std::vector<RecordedIndirectDraw> prepass = test.IndirectDraws("DepthPrepass");
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    INFO("Opaque draws both runs' commands, the prepass one of them, at its own index");
    REQUIRE(opaque.size() == 2);
    REQUIRE(prepass.size() == 1);
    REQUIRE((prepass.front().draw == opaque[0].draw || prepass.front().draw == opaque[1].draw));
    REQUIRE(RendererProbe::Culls(test.renderer).front().runs == 2);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("Two cameras each get their own Cull pass, command buffer and indirection", "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    test.AddCamera({0.0f, 0.0f, 10.0f}).GetComponent<Fenrir::Camera>().viewport = {0.5f, 0.0f, 0.5f, 1.0f};
    AddThreeRuns(test);

    test.Frame();
    const std::vector<RecordedCull> culls = test.Culls();
    INFO("one Cull pass per camera, and the probe sees both views");
    REQUIRE(culls.size() == 2);
    const std::vector<RendererProbe::CullBuffers> probed = RendererProbe::Culls(test.renderer);
    REQUIRE(probed.size() == 2);
    INFO("two command buffers and two indirections, with the same runs and candidates");
    REQUIRE(probed[0].commands != probed[1].commands);
    REQUIRE(probed[0].indirection != probed[1].indirection);
    REQUIRE(CommandsOf(test, culls[0]) == probed[0].commands);
    REQUIRE(CommandsOf(test, culls[1]) == probed[1].commands);
    for (const RendererProbe::CullBuffers& view : probed)
    {
        REQUIRE(view.runs == 3);
        REQUIRE(view.candidates == 14);
    }
    INFO("each camera's Opaque pass draws from its own buffer through its own draw set");
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    REQUIRE(opaque.size() == 6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        CAPTURE(i);
        const RendererProbe::CullBuffers& view = probed[i / 3];
        REQUIRE(opaque[i].draw.buffer == view.commands);
        REQUIRE(DrawSetIndirection(test, opaque[i].groups) == view.indirection);
    }
    INFO("the group table holds each camera's groups, three apiece, from its job block's first group on");
    REQUIRE(RendererTest::GroupsOf(test.Device(), culls[0]).size() == 3);
    REQUIRE(RendererTest::GroupsOf(test.Device(), culls[1]).size() == 3);
    REQUIRE(culls[1].copies.front().sourceOffset == 3 * CommandSize);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("A hundred thousand copies are still one draw per pass, culled by 1563 groups",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddList(test, 100000, test.Mesh(), test.Material());

    test.Frame();
    for (const char* pass : {"DepthPrepass", "Opaque"})
    {
        INFO(pass << " draws the list once, by one command");
        REQUIRE(test.IndirectDraws(pass).size() == 1);
        REQUIRE(test.Draws(pass).front().draw.instanceCount == 100000);
    }
    const RecordedCull cull = test.Culls().at(0);
    REQUIRE(cull.dispatches.front().groupsX == 1563);
    const RendererProbe::CullBuffers probed = RendererProbe::Culls(test.renderer).front();
    REQUIRE(probed.runs == 1);
    REQUIRE(probed.candidates == 100000);
    INFO("the indirection has room for every copy");
    REQUIRE(RendererProbe::Bytes(test.Device(), probed.indirection).size() >= 100000 * 8);
    REQUIRE(test.renderer.Stats().draws == 1);
    REQUIRE(test.renderer.Stats().instances == 100000);
}

TEST_CASE("Transparent items draw directly, through the draw set with no indirection, while the shadow passes draw "
          "through their own view's (ticket 10)",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddSun(test);
    const auto cube = test.Mesh();
    test.AddMeshRenderer(cube, test.Material());
    const auto blended = test.Material();
    test.Store().SetBlendMode(blended, Fenrir::BlendMode::AlphaBlend);
    test.AddMeshRenderer(cube, blended).GetComponent<Fenrir::Transform>().pos = {-2.0f, 0.0f, 0.0f};

    test.Frame();
    const BufferHandle indirection = RendererProbe::Culls(test.renderer).front().indirection;
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");
    const std::vector<RecordedDraw> shadows = test.Draws("ShadowCascades");
    REQUIRE(transparent.size() == 1);
    REQUIRE_FALSE(shadows.empty());
    const BufferHandle visible = DrawSetIndirection(test, transparent.front().groups);
    INFO("a direct draw's set 3 binds the one entry that says there is no indirection");
    REQUIRE(visible != indirection);
    REQUIRE(RendererProbe::Label(test.Device(), visible) == "No indirection");
    REQUIRE(RendererTest::UintAt(RendererProbe::Bytes(test.Device(), visible), 0) == RendererTest::NoIndirection);
    INFO("while Opaque's draws bind the camera's indirection");
    REQUIRE(DrawSetIndirection(test, test.IndirectDraws("Opaque").front().groups) == indirection);
    INFO("and a shadow draw binds its cascade's own indirection, not the camera's and not the empty one");
    const BufferHandle shadowVisible = DrawSetIndirection(test, shadows.front().groups);
    REQUIRE(shadowVisible != indirection);
    REQUIRE(shadowVisible != visible);
    REQUIRE(std::ranges::any_of(RendererProbe::ShadowCulls(test.renderer),
                                [&](const auto& view) { return view.cullIndirection == shadowVisible; }));
}

TEST_CASE("A frame with no opaque run has no Cull pass and no indirect draw", "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const auto blended = test.Material();
    test.Store().SetBlendMode(blended, Fenrir::BlendMode::AlphaBlend);
    test.AddMeshRenderer(test.Mesh(), blended);

    test.Frame();
    INFO("nothing to cull: no Cull pass, no indirect draw, and the view's cull buffers stay empty");
    REQUIRE(test.Culls().empty());
    REQUIRE(test.IndirectDraws("Opaque").empty());
    REQUIRE(RendererProbe::Culls(test.renderer).front().commands.IsEmpty());
    INFO("the transparent item still draws directly, and nothing was logged as an error");
    REQUIRE(test.Draws("Transparent").size() == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A camera's cull buffers are kept between frames, ringed per frame in flight, and grown when the frame needs",
          "[renderer][culling][cull-pass]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const auto cube = test.Mesh();
    const auto material = test.Material();
    AddList(test, 10, cube, material);

    test.Frame();
    const RendererProbe::CullBuffers first = RendererProbe::Culls(test.renderer).front();
    test.Frame();
    const RendererProbe::CullBuffers second = RendererProbe::Culls(test.renderer).front();
    test.Frame();
    const RendererProbe::CullBuffers third = RendererProbe::Culls(test.renderer).front();
    INFO("two frames in flight: the third frame's buffers are the first's, the second's its own");
    REQUIRE(first.commands != second.commands);
    REQUIRE(first.indirection != second.indirection);
    REQUIRE(third.commands == first.commands);
    REQUIRE(third.indirection == first.indirection);

    AddList(test, 1000, cube, material);
    test.Frame();
    const RendererProbe::CullBuffers grown = RendererProbe::Culls(test.renderer).front();
    INFO("more candidates and runs than the buffers hold make new ones");
    REQUIRE(grown.commands != second.commands);
    REQUIRE(grown.indirection != second.indirection);
    REQUIRE(grown.runs == 2);
    REQUIRE(grown.candidates == 1010);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The job block carries the camera's planes each frame, which change as the camera moves, and none with GPU "
          "culling off",
          "[renderer][culling][cull-pass][frustum]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    Fenrir::Entity camera = test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    const std::vector<Fenrir::Math::Vec4> first = JobPlanes(test, test.Culls().at(0));
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(camera);
    INFO("the camera has a frustum once a frame has resolved it");
    REQUIRE(frustum.has_value());
    INFO("five planes for the camera, each as Math::Frustum gives it");
    REQUIRE(first.size() == 5);
    REQUIRE(first.size() == frustum->Planes().size());
    for (std::size_t p = 0; p < first.size(); ++p)
    {
        CAPTURE(p);
        REQUIRE(Near(first[p], frustum->Planes()[p]));
    }
    camera.GetComponent<Fenrir::Transform>().pos = {3.0f, 1.0f, 5.0f};
    camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.5f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Frame();
    const std::vector<Fenrir::Math::Vec4> moved = JobPlanes(test, test.Culls().at(0));
    const std::optional<Fenrir::Math::Frustum> movedFrustum = test.renderer.GetFrustum(camera);
    REQUIRE(movedFrustum.has_value());
    INFO("the moved camera's planes are uploaded, and differ from the first frame's");
    REQUIRE(moved.size() == 5);
    for (std::size_t p = 0; p < moved.size(); ++p)
    {
        CAPTURE(p);
        REQUIRE(Near(moved[p], movedFrustum->Planes()[p]));
        REQUIRE_FALSE(Near(moved[p], first[p]));
    }

    test.renderer.SetGpuCulling(false);
    test.Frame();
    INFO("with GPU culling off the job has no planes, so the pass keeps every candidate");
    REQUIRE(JobPlanes(test, test.Culls().at(0)).empty());
    test.renderer.SetGpuCulling(true);
    test.Frame();
    REQUIRE(JobPlanes(test, test.Culls().at(0)).size() == 5);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The Cull pass binds the instance lists at set 1 binding 4 and declares reading them",
          "[renderer][culling][cull-pass][frustum]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    const RecordedCull cull = test.Culls().at(0);
    const BufferHandle copies = RendererTest::EntryAt(test.Device(), cull.target, 4).buffer;
    INFO("binding 4 is the packed instance lists, which a list's copies are tested from");
    REQUIRE(RendererProbe::Label(test.Device(), copies) == "Instance lists");
    INFO("and the pass declares reading them");
    REQUIRE(Declares(cull.pass.uses, copies, ResourceUsage::StorageRead));
}

TEST_CASE("An entity outside the camera is left out of the view's candidates on the CPU, with GPU culling on or off",
          "[renderer][culling][cull-pass][frustum]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const auto cube = test.Mesh();
    const auto material = test.Material();
    // In view: a cube at the origin and a row of ten whose first copies are in view; out of it: a cube far along +X
    // and a row wholly behind the camera.
    test.AddMeshRenderer(cube, material);
    AddList(test, 10, cube, material);
    test.AddMeshRenderer(cube, material).GetComponent<Fenrir::Transform>().pos = {100.0f, 0.0f, 0.0f};
    AddList(test, 4, cube, material).GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, 20.0f};

    for (const bool gpu : {true, false})
    {
        CAPTURE(gpu);
        test.renderer.SetGpuCulling(gpu);
        test.Frame();
        const RendererProbe::CullBuffers probed = RendererProbe::Culls(test.renderer).front();
        INFO("the cube in view and the row reaching it are the candidates: two runs, eleven candidates");
        REQUIRE(probed.runs == 2);
        REQUIRE(probed.candidates == 11);
        REQUIRE(test.IndirectDraws("Opaque").size() == 2);
        REQUIRE(test.renderer.Stats().draws == 2);
        REQUIRE(test.renderer.Stats().instances == 11);
    }
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

namespace
{
    /** @brief The last frame's CullReadback pass: its declared uses and its copies. */
    struct RecordedReadback
    {
        PassDesc pass;
        std::vector<CmdCopyBuffer> copies;
    };

    /** @brief The CullReadback passes the device received last frame, in order. */
    std::vector<RecordedReadback> ReadbacksOf(const RendererTestApp& test)
    {
        std::vector<RecordedReadback> readbacks;
        bool inPass = false;
        for (const CommandList& list : RendererProbe::Submitted(test.renderer))
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inPass = begin->pass.label == "CullReadback";
                    if (inPass)
                    {
                        readbacks.push_back({begin->pass, {}});
                    }
                }
                else if (const auto* copy = std::get_if<CmdCopyBuffer>(&command); inPass && copy != nullptr)
                {
                    readbacks.back().copies.push_back(*copy);
                }
            }
        }
        return readbacks;
    }

    /** @brief The view block bytes a draw's set 1 binds: its matrices first (view, projection, viewProjection). */
    std::vector<std::byte> ViewMatrices(const RendererTestApp& test, const RecordedIndirectDraw& draw)
    {
        const BindingGroupEntry block = RendererTest::EntryAt(test.Device(), draw.groups.at(1), 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), block.buffer);
        constexpr std::size_t matrices = 3 * sizeof(Fenrir::Math::Mat4);
        REQUIRE(block.offset + matrices <= bytes.size());
        return {bytes.begin() + static_cast<std::ptrdiff_t>(block.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(block.offset + matrices)};
    }

    /** @brief Whether every plane of `a` is Near its counterpart in `b`. */
    bool SamePlanes(const std::vector<Fenrir::Math::Vec4>& a, std::span<const Fenrir::Math::Vec4> b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t p = 0; p < a.size(); ++p)
        {
            if (!Near(a[p], b[p]))
            {
                return false;
            }
        }
        return true;
    }

    /** @brief The last frame's Debug passes' non-indexed draws, each with the pipeline it drew with. */
    std::vector<std::pair<PipelineHandle, CmdDraw>> DebugDraws(const RendererTestApp& test)
    {
        std::vector<std::pair<PipelineHandle, CmdDraw>> draws;
        bool inDebug = false;
        PipelineHandle pipeline;
        for (const CommandList& list : RendererProbe::Submitted(test.renderer))
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inDebug = begin->pass.label == "Debug";
                }
                else if (const auto* set = std::get_if<CmdSetPipeline>(&command))
                {
                    pipeline = set->pipeline;
                }
                else if (const auto* draw = std::get_if<CmdDraw>(&command); inDebug && draw != nullptr)
                {
                    draws.emplace_back(pipeline, *draw);
                }
            }
        }
        return draws;
    }
} // namespace

TEST_CASE("Each camera's command buffer is copied into the frame's slot of the readback ring once a frame, last in "
          "the frame",
          "[renderer][culling][readback]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    test.AddCamera({0.0f, 0.0f, 10.0f}).GetComponent<Fenrir::Camera>().viewport = {0.5f, 0.0f, 0.5f, 1.0f};
    AddThreeRuns(test);

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("one CullReadback pass, after both cameras' Output passes");
    REQUIRE(std::ranges::count(passes, "CullReadback") == 1);
    const auto readbackAt = std::ranges::find(passes, "CullReadback") - passes.begin();
    const auto lastOutputAt = passes.rend() - std::ranges::find(passes.rbegin(), passes.rend(), "Output") - 1;
    REQUIRE(readbackAt > lastOutputAt);
    const std::vector<RecordedReadback> readbacks = ReadbacksOf(test);
    REQUIRE(readbacks.size() == 1);
    const RecordedReadback& readback = readbacks.front();
    const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(test.renderer);
    REQUIRE(culls.size() == 2);
    INFO("one copy per camera: its three commands, from its command buffer, at its templates' offset in the slot");
    REQUIRE(readback.copies.size() == 2);
    const BufferHandle slot = readback.copies.front().destination;
    REQUIRE(RendererProbe::Label(test.Device(), slot) == "Cull readback");
    for (std::size_t c = 0; c < 2; ++c)
    {
        CAPTURE(c);
        REQUIRE(readback.copies[c].source == culls[c].commands);
        REQUIRE(readback.copies[c].sourceOffset == 0);
        REQUIRE(readback.copies[c].destination == slot);
        REQUIRE(readback.copies[c].destinationOffset == c * 3 * CommandSize);
        REQUIRE(readback.copies[c].size == 3 * CommandSize);
        INFO("the pass declares reading each command buffer as a copy's source");
        REQUIRE(Declares(readback.pass.uses, culls[c].commands, ResourceUsage::CopySource));
    }
    REQUIRE(Declares(readback.pass.uses, slot, ResourceUsage::CopyDestination));
    INFO("the ring has a slot per frame in flight, this frame's holding the six commands");
    const std::vector<RendererProbe::CullReadbackSlot> slots = RendererProbe::CullReadbacks(test.renderer);
    REQUIRE(slots.size() == test.Device().FramesInFlight());
    REQUIRE(slots.front().buffer == slot);
    REQUIRE(slots.front().frame == 0);
    REQUIRE(slots.front().commands == 6);
    INFO("the slot holds the templates the null backend copied: six commands with no instances yet");
    const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), slot);
    REQUIRE(bytes.size() >= 6 * CommandSize);
    for (std::size_t command = 0; command < 6; ++command)
    {
        CAPTURE(command);
        REQUIRE(Word(bytes, command, 0) > 0);
        REQUIRE(Word(bytes, command, 1) == 0);
    }

    test.Frame();
    INFO("the next frame fills the next slot, and the one after comes back to the first");
    REQUIRE(ReadbacksOf(test).front().copies.front().destination != slot);
    test.Frame();
    REQUIRE(ReadbacksOf(test).front().copies.front().destination == slot);
    REQUIRE(RendererProbe::CullReadbacks(test.renderer).front().frame == 2);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("instancesDrawn is the sum of the instance counts read back from the frame FramesInFlight frames ago",
          "[renderer][culling][readback]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    INFO("no frame has left flight: nothing drawn yet, fourteen candidates submitted");
    REQUIRE(test.renderer.Stats().instancesDrawn == 0);
    REQUIRE(test.renderer.Stats().instances == 14);
    // The null backend runs no compute, so the counts the slot holds are written here as the pass would have left
    // them: 7 of the list's 10, 2 of the merged 3, and the lone quad.
    const BufferHandle slot = RendererProbe::CullReadbacks(test.renderer).front().buffer;
    for (const auto [command, count] : {std::pair{0, 7u}, std::pair{1, 2u}, std::pair{2, 1u}})
    {
        const std::uint32_t kept = count;
        REQUIRE(test.Device().WriteBuffer(slot, static_cast<std::size_t>(command) * CommandSize + 4,
                                          std::as_bytes(std::span(&kept, 1))));
    }

    test.Frame();
    INFO("one frame later the first frame may still be in flight: still nothing");
    REQUIRE(test.renderer.Stats().instancesDrawn == 0);
    test.Frame();
    INFO("two frames later the first frame's counts are read: ten drawn of the fourteen submitted");
    REQUIRE(test.renderer.Stats().instancesDrawn == 10);
    REQUIRE(test.renderer.Stats().instances == 14);
    test.Frame();
    INFO("the frame after reads the second frame's slot, whose counts the null backend left at zero");
    REQUIRE(test.renderer.Stats().instancesDrawn == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("While culling is frozen the job's planes stay the frozen camera's as it moves, its view's matrices do not, "
          "and unfreezing restores them",
          "[renderer][culling][frozen]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    Fenrir::Entity camera = test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    test.renderer.SetCullingFrozen(true);
    test.Frame();
    const std::vector<Fenrir::Math::Vec4> frozen = JobPlanes(test, test.Culls().at(0));
    const std::optional<Fenrir::Math::Frustum> atFreeze = test.renderer.GetFrustum(camera);
    REQUIRE(atFreeze.has_value());
    INFO("the frame the freeze took hold in culls by the camera's own planes");
    REQUIRE(SamePlanes(frozen, atFreeze->Planes()));
    const std::vector<std::byte> matricesAtFreeze = ViewMatrices(test, test.IndirectDraws("Opaque").front());
    const std::vector<RendererProbe::CullBuffers> before = RendererProbe::Culls(test.renderer);

    // The camera moves and turns to face +X, away from every item.
    camera.GetComponent<Fenrir::Transform>().pos = {3.0f, 1.0f, 5.0f};
    camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(1.5f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Frame();
    const std::optional<Fenrir::Math::Frustum> moved = test.renderer.GetFrustum(camera);
    REQUIRE(moved.has_value());
    REQUIRE_FALSE(SamePlanes(frozen, moved->Planes()));
    INFO("moved, the job still carries the frozen planes, not the camera's now");
    REQUIRE(SamePlanes(JobPlanes(test, test.Culls().at(0)), atFreeze->Planes()));
    INFO("while the view's matrices follow the camera");
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    REQUIRE_FALSE(opaque.empty());
    REQUIRE(ViewMatrices(test, opaque.front()) != matricesAtFreeze);
    INFO("and the coarse cull keeps every entity the frozen frustum keeps, though the camera looks away from them");
    REQUIRE(RendererProbe::Culls(test.renderer).front().candidates == before.front().candidates);
    REQUIRE(RendererProbe::Culls(test.renderer).front().runs == 3);
    INFO("the probe sees the one camera's frozen cull, with its frustum and the position it froze at");
    const std::vector<Fenrir::Render::FrozenCull>& culls = RendererProbe::FrozenCulls(test.renderer);
    REQUIRE(culls.size() == 1);
    REQUIRE(culls.front().camera == camera.GetId());
    REQUIRE(SamePlanes(frozen, culls.front().frustum.Planes()));
    REQUIRE(culls.front().position == Fenrir::Math::Vec3(0.0f, 0.0f, 5.0f));

    test.renderer.SetCullingFrozen(false);
    test.Frame();
    INFO("unfrozen, the coarse cull drops every entity behind the camera: no candidates, so no Cull pass");
    REQUIRE(RendererProbe::Culls(test.renderer).front().candidates == 0);
    REQUIRE(test.Culls().empty());
    REQUIRE(RendererProbe::FrozenCulls(test.renderer).empty());
    camera.GetComponent<Fenrir::Transform>().pos = {1.0f, 0.0f, 5.0f};
    camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.0f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Frame();
    const std::optional<Fenrir::Math::Frustum> back = test.renderer.GetFrustum(camera);
    REQUIRE(back.has_value());
    INFO("turned back, the job carries the camera's planes where it is now, not the frozen ones");
    REQUIRE(SamePlanes(JobPlanes(test, test.Culls().at(0)), back->Planes()));
    REQUIRE_FALSE(SamePlanes(frozen, back->Planes()));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("While culling is frozen each view's Debug pass draws the frozen frustum's twelve edges on top",
          "[renderer][culling][frozen]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    INFO("with nothing to draw, there is no Debug pass");
    REQUIRE(std::ranges::count(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)), "Debug") == 0);
    test.renderer.SetCullingFrozen(true);
    test.Frame();
    test.Frame();
    INFO("frozen, the Debug pass draws one batch of lines: twelve edges, six vertices each, not depth tested");
    const std::vector<std::pair<PipelineHandle, CmdDraw>> draws = DebugDraws(test);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().second.vertexCount == 12 * 6);
    REQUIRE(RendererProbe::Label(test.Device(), draws.front().first) == "BuiltinDebugLines");
    REQUIRE_FALSE(RendererProbe::State(test.Device(), draws.front().first).depth.test);
    INFO("the frozen corners are on the camera's near plane, a tenth in front of it, and a hundred units out along "
         "the same rays, since a perspective view has no far plane");
    const Fenrir::Render::FrozenCull& frozen = RendererProbe::FrozenCulls(test.renderer).front();
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        CAPTURE(corner);
        const Fenrir::Math::Vec3 near = frozen.corners.at(corner);
        const Fenrir::Math::Vec3 far = frozen.corners.at(corner + 4);
        REQUIRE(std::abs(near.z - (5.0f - 0.1f)) < 1e-3f);
        REQUIRE(std::abs(glm::length(far - frozen.position) - 100.0f) < 1e-2f);
        REQUIRE(glm::length(glm::cross(far - frozen.position, near - frozen.position)) < 1e-2f);
    }
    test.renderer.SetCullingFrozen(false);
    test.Frame();
    INFO("unfrozen, the edges go with the Debug pass");
    REQUIRE(DebugDraws(test).empty());
    REQUIRE(test.messages->errors.empty());
}

namespace
{
    /** @brief The cube with two hand-made coarser levels: its first four triangles at error 0.1, its first two at 0.25.
     */
    Fenrir::MeshData ThreeLevelCube()
    {
        Fenrir::MeshData mesh = RendererTest::Cube();
        mesh.lods.push_back({{mesh.indices.begin(), mesh.indices.begin() + 12}, 0.1f});
        mesh.lods.push_back({{mesh.indices.begin(), mesh.indices.begin() + 6}, 0.25f});
        return mesh;
    }

    /** @brief The float at byte `offset` of `bytes`. */
    float FloatAt(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        float value = 0.0f;
        REQUIRE(offset + sizeof(value) <= bytes.size());
        std::memcpy(&value, std::span(bytes).subspan(offset).data(), sizeof(value));
        return value;
    }

    /** @brief A Cull pass's job block's camera (xyz, pixels per unit in w) and lod vectors, at 112 and 128 bytes. */
    std::array<Fenrir::Math::Vec4, 2> JobLod(const RendererTestApp& test, const RecordedCull& cull)
    {
        const BindingGroupEntry job = RendererTest::EntryAt(test.Device(), cull.job, 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), job.buffer);
        std::array<Fenrir::Math::Vec4, 2> vectors{};
        for (std::size_t v = 0; v < 2; ++v)
        {
            for (int i = 0; i < 4; ++i)
            {
                vectors.at(v)[i] = FloatAt(bytes, job.offset + 112 + v * 16 + static_cast<std::size_t>(i) * 4);
            }
        }
        return vectors;
    }
} // namespace

TEST_CASE("A mesh of three levels uploads one index buffer of all three, writes three templates per run with their "
          "first indices, and both passes draw each run as one multi-draw of three commands",
          "[renderer][culling][lod]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const Fenrir::MeshData data = ThreeLevelCube();
    const auto mesh = test.Mesh(data);
    const auto material = test.Material();
    AddList(test, 10, mesh, material);
    test.AddMeshRenderer(mesh, material);

    test.Frame();
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    const std::vector<RecordedIndirectDraw> prepass = test.IndirectDraws("DepthPrepass");
    INFO("one index buffer holds level 0's 36 indices, then level 1's 12, then level 2's 6");
    REQUIRE(opaque.size() == 2);
    const std::vector<std::byte> indices = RendererProbe::Bytes(test.Device(), opaque.front().indexBuffer);
    REQUIRE(indices.size() == 54 * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < 36; ++i)
    {
        REQUIRE(RendererTest::UintAt(indices, i * 4) == data.indices[i]);
    }
    for (std::size_t i = 0; i < 12; ++i)
    {
        REQUIRE(RendererTest::UintAt(indices, (36 + i) * 4) == data.lods[0].indices[i]);
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
        REQUIRE(RendererTest::UintAt(indices, (48 + i) * 4) == data.lods[1].indices[i]);
    }

    const RecordedCull cull = test.Culls().at(0);
    const std::vector<std::byte> commands = RendererProbe::Bytes(test.Device(), CommandsOf(test, cull));
    std::vector<CullGroup> groups = RendererTest::GroupsOf(test.Device(), cull);
    INFO("two runs, each group naming its run's first command: 0 for one run, 3 for the other");
    REQUIRE(groups.size() == 2);
    std::ranges::sort(groups, [](const CullGroup& a, const CullGroup& b) { return a.command < b.command; });
    REQUIRE(groups[0].command == 0);
    REQUIRE(groups[1].command == 3);
    std::uint32_t regionStart = 0;
    for (const CullGroup& group : groups)
    {
        CAPTURE(group.command, group.count);
        INFO("the run's three templates draw level 0, 1 and 2: their index counts and first indices, no instances");
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> levels{{{36, 0}, {12, 36}, {6, 48}}};
        for (std::uint32_t level = 0; level < 3; ++level)
        {
            CAPTURE(level);
            const std::size_t command = group.command + level;
            REQUIRE(Word(commands, command, 0) == levels.at(level).first);
            REQUIRE(Word(commands, command, 1) == 0);
            REQUIRE(Word(commands, command, 2) == levels.at(level).second);
            REQUIRE(Word(commands, command, 3) == 0);
            INFO("and each level's region of the indirection is the whole run's candidates long");
            REQUIRE(Word(commands, command, 4) == regionStart + level * group.count);
        }
        regionStart += 3 * group.count;
    }
    INFO("the indirection holds a region per level: 33 entries of eight bytes");
    REQUIRE(RendererProbe::Bytes(test.Device(), IndirectionOf(test, cull)).size() >= 33 * 8);
    INFO("each pass draws each run as one indirect draw of three commands from its first, the same in both");
    REQUIRE(prepass.size() == 2);
    for (std::size_t r = 0; r < 2; ++r)
    {
        CAPTURE(r);
        REQUIRE(prepass[r].draw == opaque[r].draw);
        REQUIRE(opaque[r].draw.drawCount == 3);
        REQUIRE(opaque[r].draw.stride == CommandSize);
        REQUIRE(opaque[r].draw.offset % (3 * CommandSize) == 0);
    }
    REQUIRE(opaque[0].draw.offset != opaque[1].draw.offset);
    INFO("read through the group table, the draws still cover the eleven candidates");
    std::uint32_t instances = 0;
    for (const RecordedDraw& draw : test.Draws("Opaque"))
    {
        instances += draw.draw.instanceCount;
    }
    REQUIRE(instances == 11);
    INFO("the readback slot holds the six commands, and the null backend's checks of the multi-draws pass");
    REQUIRE(RendererProbe::CullReadbacks(test.renderer).front().commands == 6);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The job block carries the camera's position and pixels per unit, the level selection's pixels with GPU "
          "culling on and zeros with it off, and the frozen position while frozen",
          "[renderer][culling][lod][frozen]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    Fenrir::Entity camera = test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    // The null camera: at (0, 0, 5), 720 pixels high with a 60 degree field of view, so a unit at distance 1 covers
    // 360 / tan(30 deg) pixels, and its near plane a tenth away.
    const float pixelsPerUnit = 360.0f / std::tan(std::numbers::pi_v<float> / 6.0f);
    std::array<Fenrir::Math::Vec4, 2> job = JobLod(test, test.Culls().at(0));
    INFO("the camera's position, and pixels per unit in w");
    REQUIRE(Near(job[0], Fenrir::Math::Vec4(0.0f, 0.0f, 5.0f, pixelsPerUnit)));
    INFO("a pixel of error, no least size, the near plane, and a perspective view");
    REQUIRE(Near(job[1], Fenrir::Math::Vec4(1.0f, 0.0f, 0.1f, 1.0f)));

    test.renderer.SetGpuCulling(false);
    test.Frame();
    job = JobLod(test, test.Culls().at(0));
    INFO("with GPU culling off, no error is allowed under the threshold, so every copy draws level 0, and nothing "
         "is dropped for its size");
    REQUIRE(Near(job[0], Fenrir::Math::Vec4(0.0f, 0.0f, 5.0f, pixelsPerUnit)));
    REQUIRE(Near(job[1], Fenrir::Math::Vec4(0.0f, 0.0f, 0.1f, 1.0f)));

    test.renderer.SetGpuCulling(true);
    test.renderer.SetCullingFrozen(true);
    test.Frame();
    camera.GetComponent<Fenrir::Transform>().pos = {3.0f, 0.0f, 5.0f};
    test.Frame();
    INFO("frozen, the job keeps the position the camera froze at as it moves");
    REQUIRE(Near(JobLod(test, test.Culls().at(0))[0], Fenrir::Math::Vec4(0.0f, 0.0f, 5.0f, pixelsPerUnit)));
    test.renderer.SetCullingFrozen(false);
    test.Frame();
    INFO("unfrozen, it follows the camera again");
    REQUIRE(Near(JobLod(test, test.Culls().at(0))[0], Fenrir::Math::Vec4(3.0f, 0.0f, 5.0f, pixelsPerUnit)));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("SetLodSelection changes the job block's two thresholds from the next frame", "[renderer][culling][lod]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    INFO("installed with the config's defaults: a pixel of error and no least size");
    REQUIRE(Near(JobLod(test, test.Culls().at(0))[1], Fenrir::Math::Vec4(1.0f, 0.0f, 0.1f, 1.0f)));
    test.renderer.SetLodSelection(2.5f, 3.0f);
    test.Frame();
    INFO("the next frame's job carries the new thresholds, with the near plane and the projection as they were");
    REQUIRE(Near(JobLod(test, test.Culls().at(0))[1], Fenrir::Math::Vec4(2.5f, 3.0f, 0.1f, 1.0f)));
    INFO("the App& static answers as the owner");
    Fenrir::Renderer::SetLodSelection(test.app, 0.5f, 0.0f);
    test.Frame();
    REQUIRE(Near(JobLod(test, test.Culls().at(0))[1], Fenrir::Math::Vec4(0.5f, 0.0f, 0.1f, 1.0f)));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("drawnByLod sums the counts read back by each command's level of detail, and with instancesDrawn",
          "[renderer][culling][lod][readback]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    const auto mesh = test.Mesh(ThreeLevelCube());
    const auto material = test.Material();
    AddList(test, 10, mesh, material);
    test.AddMeshRenderer(mesh, material);

    test.Frame();
    // The null backend runs no compute, so the counts are written here as the pass would have left them: of the
    // list's ten copies, four at level 0, three at level 1 and two at level 2, and the lone cube at level 1.
    const BufferHandle slot = RendererProbe::CullReadbacks(test.renderer).front().buffer;
    for (const auto [command, count] : {std::pair{0, 4u}, std::pair{1, 3u}, std::pair{2, 2u}, std::pair{4, 1u}})
    {
        const std::uint32_t kept = count;
        REQUIRE(test.Device().WriteBuffer(slot, static_cast<std::size_t>(command) * CommandSize + 4,
                                          std::as_bytes(std::span(&kept, 1))));
    }
    test.Frame();
    test.Frame();
    INFO("two frames later: ten drawn, four at level 0, four at level 1, two at level 2 and none coarser");
    const Fenrir::RendererStats stats = test.renderer.Stats();
    REQUIRE(stats.instancesDrawn == 10);
    REQUIRE(std::ranges::equal(stats.drawnByLod, std::array<std::uint32_t, 8>{4, 4, 2, 0, 0, 0, 0, 0}));
    REQUIRE(test.messages->errors.empty());
}
