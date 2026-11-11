// Occlusion culling (ADR-0057, culling ticket 11): the CPU mirror of the cull pass's second phase (a box's screen
// rectangle, the pyramid level whose texels cover it, and the test against the farthest depth there), and the frame
// on the null backend: each camera's cull pass runs twice, its first phase first in the frame and its second after the
// pyramid is built from the prepass's first draws, each phase's job block saying which it is and whether the view's
// visibility words are stale; the prepass draws the second phase's commands after the first's and Opaque draws both;
// a reflecting camera builds its pyramid again after GTAO's passes; with occlusion culling off, with GPU culling off
// or while frozen there is one phase; and drawnByPhase sums the readback by phase. The null backend runs no compute,
// so what it shows is the passes, the job blocks and the draws, never which copies were hidden.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Occlusion.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/SceneHistory.hpp"
#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirMath/Math.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using Fenrir::Render::HiZTexel;
using Fenrir::Render::IsHidden;
using Fenrir::Render::PyramidLevelOf;
using Fenrir::Render::ScreenRect;
using Fenrir::Render::ScreenRectOf;
using Fenrir::Render::TexelsOf;
using namespace Fenrir::Graphics;
using RendererTest::Labelled;
using RendererTest::PassRecord;
using RendererTest::RecordedCull;
using RendererTest::RecordedIndirectDraw;
using RendererTest::RendererTestApp;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);
    /** @brief The 1280 x 720 null window's pyramid has eleven levels. */
    constexpr std::size_t Mips = 11;

    /** @brief A camera at the origin looking along -Z with a 90 degree field of view, square, near plane 0.1. */
    Fenrir::Math::Mat4 SquareProjection()
    {
        return Fenrir::Render::ReversedZPerspective(std::numbers::pi_v<float> / 2.0f, 1.0f, 0.1f);
    }

    /** @brief The unit cube centred at `centre`. */
    Fenrir::Math::AABB UnitCubeAt(Vec3 centre)
    {
        return {centre - Vec3(0.5f), centre + Vec3(0.5f)};
    }

    bool Close(float a, float b, float tolerance = 1e-5f)
    {
        return std::abs(a - b) <= tolerance;
    }

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

    /** @brief The scene CullPass.cpp uses: one list of ten cubes, a merged run of three, and a lone quad: three runs.
     */
    void AddThreeRuns(RendererTestApp& test)
    {
        const auto cube = test.Mesh();
        const auto lit = test.Material();
        const auto list = test.Store().Add<Fenrir::InstanceList>(Row(10), Fenrir::AssetOwner::App());
        test.ActiveScene().GetEntityList().CreateEntity().AddComponent<Fenrir::InstancedMeshRenderer>(
            Fenrir::InstancedMeshRenderer{cube, lit, list, true, true});
        for (int i = 0; i < 3; ++i)
        {
            test.AddMeshRenderer(cube, lit).GetComponent<Fenrir::Transform>().pos = {static_cast<float>(i), 2.0f, 0.0f};
        }
        test.AddMeshRenderer(test.Mesh(RendererTest::Quad()), test.Material("builtin:unlit"));
    }

    /** @brief The labels of `passes`, in order. */
    std::vector<std::string> Labels(const std::vector<PassRecord>& passes)
    {
        std::vector<std::string> labels;
        for (const PassRecord& pass : passes)
        {
            labels.push_back(pass.pass.label);
        }
        return labels;
    }

    /** @brief A Cull pass's job block's occlusion vector: the phase, whether the words are stale, the command offset
     * of the second phase and the pyramid's levels. */
    std::array<std::uint32_t, 4> JobOcclusion(const RendererTestApp& test, const RecordedCull& cull)
    {
        const BindingGroupEntry job = RendererTest::EntryAt(test.Device(), cull.job, 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), job.buffer);
        std::array<std::uint32_t, 4> words{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            words.at(i) = RendererTest::UintAt(bytes, job.offset + 144 + i * 4);
        }
        return words;
    }

    /** @brief Whether `uses` declares `usage` on `buffer`. */
    bool Declares(const std::vector<PassResourceUse>& uses, BufferHandle buffer, ResourceUsage usage)
    {
        return std::ranges::any_of(uses, [&](const PassResourceUse& use) {
            return use.texture.IsEmpty() && use.buffer == buffer && use.usage == usage;
        });
    }

    /** @brief Whether `uses` declares sampling `texture`. */
    bool SamplesTexture(const std::vector<PassResourceUse>& uses, TextureHandle texture)
    {
        return std::ranges::any_of(uses, [&](const PassResourceUse& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief The commands buffer a Cull pass binds at set 1 binding 2, and its visibility words at binding 6. */
    BufferHandle CommandsOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        return RendererTest::EntryAt(test.Device(), cull.target, 2).buffer;
    }
    BufferHandle VisibilityOf(const RendererTestApp& test, const RecordedCull& cull)
    {
        return RendererTest::EntryAt(test.Device(), cull.target, 6).buffer;
    }
} // namespace

TEST_CASE("A box's screen rectangle under a perspective view is its corners' extent, y down from the top, with its "
          "nearest depth",
          "[renderer][culling][occlusion]")
{
    // A 90 degree view from the origin along -Z: a point at distance d projects to x / d. The unit cube five units
    // out has its near face at 4.5, so its corners reach 0.5 / 4.5 = 0.1111 either side of the centre, a ninth of
    // the half-screen, and its nearest depth is the near plane over that distance.
    const std::optional<ScreenRect> rect =
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, -5.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection());
    REQUIRE(rect.has_value());
    INFO("a ninth of the half-screen each side of the centre, both ways");
    REQUIRE(Close(rect->min.x, 0.5f - 0.5f / 9.0f));
    REQUIRE(Close(rect->max.x, 0.5f + 0.5f / 9.0f));
    REQUIRE(Close(rect->min.y, 0.5f - 0.5f / 9.0f));
    REQUIRE(Close(rect->max.y, 0.5f + 0.5f / 9.0f));
    INFO("the nearest depth is the near face's: 0.1 over 4.5");
    REQUIRE(Close(rect->nearest, 0.1f / 4.5f));

    INFO("a cube above the centre has its rectangle in the top half, texture rows running down");
    const std::optional<ScreenRect> above =
        ScreenRectOf(UnitCubeAt({0.0f, 2.0f, -5.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection());
    REQUIRE(above.has_value());
    REQUIRE(above->max.y < 0.5f);
    INFO("the transform moves the box: the same cube translated there gives the same rectangle");
    const std::optional<ScreenRect> moved =
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, 0.0f}),
                     Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {0.0f, 2.0f, -5.0f}), SquareProjection());
    REQUIRE(moved.has_value());
    REQUIRE(Close(moved->min.y, above->min.y));
    REQUIRE(Close(moved->nearest, above->nearest));
    INFO("and a cube past the screen's edge is clamped to it");
    const std::optional<ScreenRect> edge =
        ScreenRectOf(UnitCubeAt({-4.8f, 0.0f, -5.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection());
    REQUIRE(edge.has_value());
    REQUIRE(edge->min.x == 0.0f);
    REQUIRE(edge->max.x > 0.0f);
}

TEST_CASE("The pyramid level for a rectangle is the one whose texels cover it in at most 2 x 2",
          "[renderer][culling][occlusion]")
{
    const std::optional<ScreenRect> rect =
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, -5.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection());
    REQUIRE(rect.has_value());
    INFO("a ninth of a 128-pixel view is 14.2 pixels: level 4, whose texels are 16 pixels, halves it under one");
    REQUIRE(PyramidLevelOf(*rect, {128, 128}, 8) == 4);
    INFO("at level 4 the view is 8 texels, and the rectangle (3.56 to 4.44 texels) touches texels 3 and 4 each way");
    const Fenrir::Render::TexelRange texels = TexelsOf(*rect, {8, 8});
    REQUIRE(texels.lo == glm::uvec2(3, 3));
    REQUIRE(texels.hi == glm::uvec2(4, 4));
    INFO("over a 1280 x 720 view its 142 pixels want level 8");
    REQUIRE(PyramidLevelOf(*rect, {1280, 720}, Mips) == 8);
    INFO("a pyramid of fewer levels gives its last");
    REQUIRE(PyramidLevelOf(*rect, {1280, 720}, 3) == 2);
    INFO("a rectangle under a pixel wants level 0");
    const ScreenRect tiny{{0.5f, 0.5f}, {0.501f, 0.501f}, 0.5f};
    REQUIRE(PyramidLevelOf(tiny, {128, 128}, 8) == 0);
    INFO("a rectangle ending on a texel's edge touches that texel and not the next");
    const ScreenRect aligned{{0.25f, 0.25f}, {0.5f, 0.5f}, 0.5f};
    REQUIRE(TexelsOf(aligned, {4, 4}).hi == glm::uvec2(1, 1));
    INFO("and one on the far edge of the view stays inside it");
    const ScreenRect far{{0.9f, 0.9f}, {1.0f, 1.0f}, 0.5f};
    REQUIRE(TexelsOf(far, {4, 4}).hi == glm::uvec2(3, 3));
}

TEST_CASE("A box straddling the near plane, or behind the camera at a corner, has no rectangle and is kept",
          "[renderer][culling][occlusion]")
{
    INFO("a cube around the camera reaches behind it");
    REQUIRE_FALSE(
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, 0.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection()).has_value());
    INFO("a cube in front of the camera but crossing the near plane, a tenth out, has no rectangle either");
    REQUIRE_FALSE(
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, -0.5f}), Fenrir::Math::Mat4(1.0f), SquareProjection()).has_value());
    INFO("one wholly past the near plane has one");
    REQUIRE(ScreenRectOf(UnitCubeAt({0.0f, 0.0f, -0.7f}), Fenrir::Math::Mat4(1.0f), SquareProjection()).has_value());
}

TEST_CASE("A rectangle is hidden when its nearest depth is behind the farthest depth of every texel it touches",
          "[renderer][culling][occlusion]")
{
    const std::optional<ScreenRect> rect =
        ScreenRectOf(UnitCubeAt({0.0f, 0.0f, -5.0f}), Fenrir::Math::Mat4(1.0f), SquareProjection());
    REQUIRE(rect.has_value());
    // Level 4 of a 128-pixel view: 8 x 8 texels; the cube touches texels 3 and 4 each way.
    const PixelSize size{8, 8};
    std::vector<HiZTexel> wall(64, HiZTexel{0.5f, 0.5f});
    INFO("a wall nearer than the cube everywhere hides it");
    REQUIRE(IsHidden(*rect, wall, size));
    INFO("a wall farther than the cube does not");
    REQUIRE_FALSE(IsHidden(*rect, std::vector<HiZTexel>(64, HiZTexel{0.01f, 0.01f}), size));
    INFO("nor one with a gap in any of the four texels, where nothing was drawn (a farthest depth of 0)");
    for (const auto [x, y] : {std::pair{3u, 3u}, std::pair{4u, 3u}, std::pair{3u, 4u}, std::pair{4u, 4u}})
    {
        std::vector<HiZTexel> gapped = wall;
        gapped[std::size_t{y} * 8 + x] = HiZTexel{0.5f, 0.0f};
        CAPTURE(x, y);
        REQUIRE_FALSE(IsHidden(*rect, gapped, size));
    }
    INFO("a gap in a texel it does not touch changes nothing");
    std::vector<HiZTexel> elsewhere = wall;
    elsewhere[0] = HiZTexel{0.5f, 0.0f};
    REQUIRE(IsHidden(*rect, elsewhere, size));
    INFO("and a texel whose nearest is in front but whose farthest is behind the cube shows it");
    std::vector<HiZTexel> mixed = wall;
    mixed[3 * 8 + 3] = HiZTexel{0.9f, 0.01f};
    REQUIRE_FALSE(IsHidden(*rect, mixed, size));
}

TEST_CASE("Occlusion culling is on by default, and a camera's cull pass then runs twice round one build of its "
          "pyramid, the second phase after the prepass's first draws",
          "[renderer][culling][occlusion]")
{
    INFO("on by default");
    REQUIRE(Fenrir::RendererConfig{}.occlusionCulling);
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
    AddThreeRuns(test);

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    INFO("the first Cull first in the frame; the prepass; eleven HiZ passes; the second Cull; the prepass again");
    REQUIRE(labels == RendererTest::WithOcclusion(
                          {"Cull", "BrdfLut", "DepthPrepass", "Opaque", "Background", "Output", "CullReadback"}));
    const std::vector<RecordedCull> culls = test.Culls();
    REQUIRE(culls.size() == 2);
    const RecordedCull& first = culls[0];
    const RecordedCull& second = culls[1];
    INFO("the first phase copies the templates of both phases, six commands, and dispatches; the second only "
         "dispatches, over the same groups");
    REQUIRE(first.copies.size() == 1);
    REQUIRE(first.copies.front().size == 6 * CommandSize);
    REQUIRE(first.dispatches.size() == 1);
    REQUIRE(second.copies.empty());
    REQUIRE(second.dispatches.size() == 1);
    REQUIRE(second.dispatches.front().groupsX == first.dispatches.front().groupsX);
    INFO("both count into the same command buffer");
    REQUIRE(CommandsOf(test, second) == CommandsOf(test, first));
    INFO("the jobs say which phase each is, that the words are stale on the first frame, that the second phase's "
         "commands are three on, and that the pyramid has eleven levels");
    REQUIRE(JobOcclusion(test, first) == std::array<std::uint32_t, 4>{1, 1, 3, Mips});
    REQUIRE(JobOcclusion(test, second) == std::array<std::uint32_t, 4>{2, 1, 3, Mips});
    INFO("both read the view's visibility words; the second writes them and samples the pyramid");
    const BufferHandle visibility = VisibilityOf(test, first);
    REQUIRE(visibility == VisibilityOf(test, second));
    REQUIRE(RendererProbe::Label(test.Device(), visibility) == "Cull visibility");
    REQUIRE(Declares(first.pass.uses, visibility, ResourceUsage::StorageRead));
    REQUIRE_FALSE(Declares(first.pass.uses, visibility, ResourceUsage::StorageWrite));
    REQUIRE(Declares(second.pass.uses, visibility, ResourceUsage::StorageRead));
    REQUIRE(Declares(second.pass.uses, visibility, ResourceUsage::StorageWrite));
    const TextureHandle pyramid = RendererTest::EntryAt(test.Device(), second.target, 7).texture;
    REQUIRE(pyramid == RendererProbe::Reflections(test.renderer).at(0).pyramid);
    REQUIRE(SamplesTexture(second.pass.uses, pyramid));
    REQUIRE_FALSE(SamplesTexture(first.pass.uses, pyramid));
    INFO("the probe reports the two phases and the three commands a phase");
    const std::vector<RendererProbe::CullBuffers> probed = RendererProbe::Culls(test.renderer);
    REQUIRE(probed.size() == 1);
    REQUIRE(probed.front().phases == 2);
    REQUIRE(probed.front().commandsPerPhase == 3);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());

    test.Frame();
    INFO("the next frame's words are not stale: the candidates kept their order");
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 0);
    REQUIRE(JobOcclusion(test, test.Culls().at(1))[1] == 0);
}

TEST_CASE("The prepass draws the second phase's commands after the first's, and Opaque draws both",
          "[renderer][culling][occlusion]")
{
    RendererTestApp test;
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    const BufferHandle commands = CommandsOf(test, test.Culls().at(0));
    const std::vector<RecordedIndirectDraw> prepass = test.IndirectDraws("DepthPrepass");
    const std::vector<RecordedIndirectDraw> opaque = test.IndirectDraws("Opaque");
    INFO("three runs, drawn twice in each pass: the first phase's three commands, then the second's three");
    REQUIRE(prepass.size() == 6);
    REQUIRE(opaque.size() == 6);
    for (std::size_t d = 0; d < 6; ++d)
    {
        CAPTURE(d);
        REQUIRE(prepass[d].draw.buffer == commands);
        REQUIRE(prepass[d].draw.offset == d * CommandSize);
        REQUIRE(prepass[d].draw.drawCount == 1);
        REQUIRE(opaque[d].draw == prepass[d].draw);
        INFO("the second phase's draw of a run has the run's mesh");
        REQUIRE(prepass[d].indexBuffer == prepass[d % 3].indexBuffer);
    }
    INFO("the readback slot holds all six commands");
    REQUIRE(RendererProbe::CullReadbacks(test.renderer).front().commands == 6);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A reflecting camera builds its pyramid twice: after the first phase's draws, and from the complete depth "
          "after GTAO's passes",
          "[renderer][culling][occlusion][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
    camera.reflections.enabled = true;
    camera.ambientOcclusion.enabled = true;
    AddThreeRuns(test);

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    INFO("two cull dispatches and twenty-two HiZ passes");
    REQUIRE(test.Culls().size() == 2);
    REQUIRE(std::ranges::count(labels, "HiZ") == 2 * Mips);
    const auto at = [&](const std::string& label, bool last) {
        const auto found = last ? std::find(labels.rbegin(), labels.rend(), label).base() - 1
                                : std::find(labels.begin(), labels.end(), label);
        return static_cast<std::size_t>(found - labels.begin());
    };
    INFO("the first build between the prepass's phases, the second after GTAO and before the trace");
    REQUIRE(at("DepthPrepass", false) < at("HiZ", false));
    REQUIRE(at("HiZ", false) < at("DepthPrepass", true));
    REQUIRE(at("GtaoDenoise", true) < at("HiZ", true));
    REQUIRE(at("HiZ", true) < at("ReflectionTrace", false));

    test.renderer.SetOcclusionCulling(false);
    test.Frame();
    INFO("occlusion off, one cull of phase 0 and the reflections' one build");
    REQUIRE(test.Culls().size() == 1);
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[0] == 0);
    REQUIRE(std::ranges::count(Labels(test.Passes()), "HiZ") == Mips);
    REQUIRE(std::ranges::count(Labels(test.Passes()), "DepthPrepass") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("With occlusion culling off, GPU culling off or culling frozen a camera has one phase and no pyramid",
          "[renderer][culling][occlusion][frozen]")
{
    RendererTestApp test(RendererTest::OcclusionOff());
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    INFO("off in the config: one cull of phase 0, no HiZ pass, nothing kept");
    REQUIRE(test.Culls().size() == 1);
    REQUIRE(JobOcclusion(test, test.Culls().at(0)) == std::array<std::uint32_t, 4>{0, 0, 3, 0});
    REQUIRE(Labelled(test.Passes(), "HiZ").empty());
    REQUIRE(Labelled(test.Passes(), "DepthPrepass").size() == 1);
    REQUIRE(RendererProbe::Reflections(test.renderer).empty());
    REQUIRE(RendererProbe::Culls(test.renderer).front().phases == 1);
    INFO("the one phase binds the word that says there is no visibility, which it never reads");
    REQUIRE(RendererProbe::Label(test.Device(), VisibilityOf(test, test.Culls().at(0))) == "No visibility");
    REQUIRE(test.IndirectDraws("DepthPrepass").size() == 3);
    REQUIRE(test.IndirectDraws("Opaque").size() == 3);

    test.renderer.SetOcclusionCulling(true);
    test.Frame();
    INFO("on from the next frame");
    REQUIRE(test.Culls().size() == 2);

    test.renderer.SetGpuCulling(false);
    test.Frame();
    INFO("with GPU culling off there is one path, accepting everything");
    REQUIRE(test.Culls().size() == 1);
    REQUIRE(Labelled(test.Passes(), "HiZ").empty());

    test.renderer.SetGpuCulling(true);
    test.renderer.SetCullingFrozen(true);
    test.Frame();
    INFO("and frozen, the planes are not the view's, so nothing is tested against its depth");
    REQUIRE(test.Culls().size() == 1);
    test.renderer.SetCullingFrozen(false);
    test.Frame();
    REQUIRE(test.Culls().size() == 2);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The visibility words are stale when the camera's candidates change, and kept while they only move",
          "[renderer][culling][occlusion]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto cube = test.Mesh();
    const auto lit = test.Material();
    Fenrir::Entity moving = test.AddMeshRenderer(cube, lit);
    test.AddMeshRenderer(cube, lit).GetComponent<Fenrir::Transform>().pos = {1.0f, 0.0f, 0.0f};

    test.Frame();
    INFO("the first frame's words are stale");
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 1);
    moving.GetComponent<Fenrir::Transform>().pos = {0.0f, 1.0f, 0.0f};
    test.Frame();
    INFO("a candidate moving keeps them");
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 0);
    test.AddMeshRenderer(cube, lit).GetComponent<Fenrir::Transform>().pos = {-1.0f, 0.0f, 0.0f};
    test.Frame();
    INFO("another candidate in the run makes them stale, and the next frame keeps them again");
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 1);
    test.Frame();
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 0);
    INFO("a frame without the second phase forgets them");
    test.renderer.SetOcclusionCulling(false);
    test.Frame();
    test.renderer.SetOcclusionCulling(true);
    test.Frame();
    REQUIRE(JobOcclusion(test, test.Culls().at(0))[1] == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("drawnByPhase sums the counts read back by each command's phase, and with instancesDrawn",
          "[renderer][culling][occlusion][readback]")
{
    RendererTestApp test;
    test.AddCamera();
    AddThreeRuns(test);

    test.Frame();
    // The null backend runs no compute, so the counts are written here as the passes would have left them: seven of
    // the list's copies drawn in the first phase, three more and two of the merged run's cubes in the second.
    const BufferHandle slot = RendererProbe::CullReadbacks(test.renderer).front().buffer;
    for (const auto [command, count] : {std::pair{0, 7u}, std::pair{3, 3u}, std::pair{4, 2u}})
    {
        const std::uint32_t kept = count;
        REQUIRE(test.Device().WriteBuffer(slot, static_cast<std::size_t>(command) * CommandSize + 4,
                                          std::as_bytes(std::span(&kept, 1))));
    }
    test.Frame();
    test.Frame();
    INFO("two frames later: twelve drawn, seven in the first phase and five in the second, all at level 0");
    const Fenrir::RendererStats stats = test.renderer.Stats();
    REQUIRE(stats.instancesDrawn == 12);
    REQUIRE(std::ranges::equal(stats.drawnByPhase, std::array<std::uint32_t, 2>{7, 5}));
    REQUIRE(stats.drawnByLod[0] == 12);
    REQUIRE(test.messages->errors.empty());
}
