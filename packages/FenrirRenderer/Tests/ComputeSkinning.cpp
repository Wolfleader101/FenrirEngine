// Compute skinning on the null backend (the spec's seam 1; ADR-0050): the Skinning pass's dispatches, one per skinned
// mesh and palette range a frame however many items, views and passes draw it, their job blocks, and the draws that
// read what they write, rigidly, through their shader's variant without SKINNED. Turned off, skinned draws skin in
// their vertex stages as ADR-0035 has them (Skinning.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirShaders/UserSkinned.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RecordedDraw;
using RendererTest::SkinningTestApp;

namespace
{
    /** @brief builtin:skinning's job block (set 0), as `skinning.comp` declares it: stream offsets in words. */
    struct SkinJob
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t paletteOffset = 0;
        std::uint32_t skinnedFirst = 0;
        std::uint32_t flags = 0;
        std::uint32_t positions = 0;
        std::uint32_t normals = 0;
        std::uint32_t tangents = 0;
        std::uint32_t joints = 0;
        std::uint32_t weights = 0;
    };

    /** @brief One dispatch of a Skinning pass: its job block, the groups bound for it and its work groups. */
    struct RecordedDispatch
    {
        SkinJob job;
        std::array<Fenrir::Graphics::BindingGroupHandle, 4> groups{};
        Fenrir::Graphics::CmdDispatch dispatch;
    };

    /** @brief The strip's vertices: three rows of two. */
    constexpr std::uint32_t StripVertices = 6;
    /** @brief A skinned vertex's position, normal and tangent, in bytes. */
    constexpr std::size_t SkinnedVertexBytes = 40;

    /**
     * @brief Every dispatch of the last frame's Skinning passes, in order, each with its job block as the device holds
     * it; the Cull passes' dispatches (ADR-0057) are not among them.
     */
    std::vector<RecordedDispatch> Dispatches(const SkinningTestApp& test)
    {
        using namespace Fenrir::Graphics;
        std::vector<RecordedDispatch> dispatches;
        for (const CommandList& list : RendererProbe::Submitted(test.renderer))
        {
            std::array<BindingGroupHandle, 4> groups{};
            bool skinning = false;
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    skinning = begin->pass.label == "Skinning";
                }
                else if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    groups.at(group->set) = group->group;
                }
                else if (const auto* dispatch = std::get_if<CmdDispatch>(&command); dispatch != nullptr && skinning)
                {
                    RecordedDispatch recorded{{}, groups, *dispatch};
                    const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), groups[0]);
                    REQUIRE(entries.size() == 1);
                    const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
                    REQUIRE(entries[0].offset + sizeof(SkinJob) <= bytes.size());
                    std::memcpy(&recorded.job, std::span(bytes).subspan(entries[0].offset).data(), sizeof(SkinJob));
                    dispatches.push_back(recorded);
                }
            }
        }
        return dispatches;
    }

    /** @brief How many indexed draws of skinned vertices the last frame's passes recorded, in every pass. */
    std::size_t SkinnedVertexDraws(const SkinningTestApp& test)
    {
        std::size_t count = 0;
        for (const char* pass : {"DepthPrepass", "Opaque", "Transparent"})
        {
            for (const RecordedDraw& draw : test.Draws(pass))
            {
                count += RendererProbe::Label(test.Device(), draw.vertexBuffers.at(0)) == "Skinned vertices" ? 1 : 0;
            }
        }
        return count;
    }
} // namespace

TEST_CASE("Each skinned mesh is skinned by one dispatch a frame however many items, views and passes draw it",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddCamera({3.0f, 1.0f, 10.0f});
    // On the first skeleton, the strip twice: one mesh and one palette range, one dispatch. On the second, the strip
    // again in a masked and a blended material, and a second strip mesh: two more.
    const Fenrir::Entity first = test.AddSkeleton({-2.0f, 0.0f, 0.0f});
    test.AddSkinned(first, test.StripMesh(), test.Material());
    test.AddSkinned(first, test.StripMesh(), test.Material());
    const Fenrir::Entity second = test.AddSkeleton({2.0f, 0.0f, 0.0f});
    const auto masked = test.Material();
    REQUIRE(test.Store().SetBlendMode(masked, Fenrir::BlendMode::Masked));
    const auto glass = test.Material();
    REQUIRE(test.Store().SetBlendMode(glass, Fenrir::BlendMode::AlphaBlend));
    test.AddSkinned(second, test.StripMesh(), masked);
    test.AddSkinned(second, test.StripMesh(), glass);
    test.AddSkinned(second, test.Mesh(RendererTest::Strip()), test.Material());

    for (int frame = 0; frame < 2; ++frame)
    {
        test.Frame();
        CAPTURE(frame);

        const std::vector<RecordedDispatch> dispatches = Dispatches(test);
        INFO("three dispatches, for three skinned meshes and palette ranges");
        REQUIRE(dispatches.size() == 3);
        INFO("while the two views' DepthPrepass, Opaque and Transparent passes draw them far more often");
        REQUIRE(SkinnedVertexDraws(test) > 3 * dispatches.size());
        INFO("five skinned items in each of two views");
        REQUIRE(test.renderer.Stats().skinnedDraws == 10);

        const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
        INFO("in one Skinning pass, before either view's passes");
        REQUIRE(std::count(passes.begin(), passes.end(), "Skinning") == 1);
        const auto skinning = std::find(passes.begin(), passes.end(), "Skinning");
        REQUIRE(skinning < std::find(passes.begin(), passes.end(), "DepthPrepass"));
        for (const RecordedDispatch& dispatch : dispatches)
        {
            INFO("each runs builtin:skinning over the strip's six vertices, one work group of 64");
            REQUIRE(dispatch.job.vertexCount == StripVertices);
            REQUIRE(dispatch.dispatch.groupsX == 1);
            REQUIRE(dispatch.dispatch.groupsY == 1);
            REQUIRE(dispatch.dispatch.groupsZ == 1);
        }
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A compute-skinned draw reads its positions, normals and tangents from its mesh's skinned vertices, through "
          "its shader's variant without SKINNED",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddRig({-2.0f, 0.0f, 0.0f});
    test.AddRig({2.0f, 0.0f, 0.0f});
    test.AddMeshRenderer(test.Mesh(), test.Material()).GetComponent<Fenrir::Transform>().pos = {0.0f, 3.0f, 0.0f};

    test.Frame();

    const std::vector<RecordedDispatch> dispatches = Dispatches(test);
    REQUIRE(dispatches.size() == 2);
    std::vector<std::uint32_t> skinnedFirsts;
    for (const RecordedDraw& draw : test.Draws())
    {
        const std::string slot0 = RendererProbe::Label(test.Device(), draw.vertexBuffers.at(0));
        if (slot0 != "Skinned vertices")
        {
            INFO("the rigid cube reads its own vertex buffer");
            REQUIRE(slot0 == "Mesh vertices");
            continue;
        }
        INFO("a skinned strip draws builtin:lit's base variant, not SKINNED, with its skeleton's world matrix");
        REQUIRE(RendererProbe::Keys(test.Device(), draw.pipeline).empty());
        const RendererTest::DrawRecord record = test.Record(draw);
        REQUIRE(std::abs(record.world[3].x) == 2.0f);
        // builtin:lit reads locations 0 to 5 in slots 0 to 5: the skinned streams, then the strip's own (UVs and
        // colours it lacks, so the defaults).
        const std::size_t first = draw.vertexOffsets.at(0);
        INFO("its normals and tangents follow its positions in the skinned vertex buffer");
        REQUIRE(draw.vertexBuffers.at(1) == draw.vertexBuffers.at(0));
        REQUIRE(draw.vertexBuffers.at(2) == draw.vertexBuffers.at(0));
        REQUIRE(draw.vertexOffsets.at(1) == first + StripVertices * 12);
        REQUIRE(draw.vertexOffsets.at(2) == first + StripVertices * 24);
        REQUIRE(RendererProbe::Label(test.Device(), draw.vertexBuffers.at(3)) != "Skinned vertices");
        INFO("where one dispatch's job writes them");
        const auto job = std::find_if(dispatches.begin(), dispatches.end(), [&](const RecordedDispatch& d) {
            return std::size_t{d.job.skinnedFirst} * 4 == first;
        });
        REQUIRE(job != dispatches.end());
        INFO("from the palette range its record names");
        REQUIRE(job->job.paletteOffset == record.paletteOffset);
        skinnedFirsts.push_back(job->job.skinnedFirst);
    }
    INFO("the two skeletons' strips are skinned apart");
    REQUIRE(skinnedFirsts.size() == 2);
    REQUIRE(skinnedFirsts[0] != skinnedFirsts[1]);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A dispatch's job reads its mesh's streams where they are, and every job writes inside the skinned vertex "
          "buffer",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 30.0f});
    for (int i = 0; i < 9; ++i)
    {
        test.AddRig({static_cast<float>(i) - 4.0f, 0.0f, 0.0f});
    }

    test.Frame();
    test.Frame();

    const std::vector<RecordedDispatch> dispatches = Dispatches(test);
    REQUIRE(dispatches.size() == 9);
    const std::vector<RecordedDraw> draws = test.Draws();
    REQUIRE(!draws.empty());
    const std::size_t capacity = RendererProbe::Bytes(test.Device(), draws.front().vertexBuffers.at(0)).size();
    std::vector<std::uint32_t> palettes;
    for (const RecordedDispatch& dispatch : dispatches)
    {
        const SkinJob& job = dispatch.job;
        // The strip's streams, back to back: positions and normals (12 bytes a vertex), then joints and weights (8).
        INFO("its streams' first words");
        REQUIRE(job.positions == 0);
        REQUIRE(job.normals == StripVertices * 3);
        REQUIRE(job.joints == StripVertices * 6);
        REQUIRE(job.weights == StripVertices * 8);
        INFO("it has normals and no tangents");
        REQUIRE(job.flags == 1u);
        INFO("its skinned vertices lie inside the buffer, 16-byte aligned");
        REQUIRE(job.skinnedFirst % 4 == 0);
        REQUIRE(std::size_t{job.skinnedFirst} * 4 + StripVertices * SkinnedVertexBytes <= capacity);
        palettes.push_back(job.paletteOffset);
        INFO("and its source is its mesh's vertex buffer");
        const std::vector<Fenrir::Graphics::BindingGroupEntry> source =
            RendererProbe::Entries(test.Device(), dispatch.groups[1]);
        REQUIRE(source.size() == 1);
        REQUIRE(RendererProbe::Label(test.Device(), source[0].buffer) == "Mesh vertices");
    }
    std::sort(palettes.begin(), palettes.end());
    INFO("each skeleton's own palette range, two bones apart");
    REQUIRE(std::adjacent_find(palettes.begin(), palettes.end()) == palettes.end());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE(
    "Items of one mesh on several skeletons draw apart under compute skinning, and merge with it off, while items "
    "of one mesh on one skeleton merge either way",
    "[renderer][skinning][compute-skinning][instancing]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 30.0f});
    const auto material = test.Material();
    for (int i = 0; i < 3; ++i)
    {
        test.AddSkinned(test.AddSkeleton({static_cast<float>(i) * 2.0f - 2.0f, 0.0f, 0.0f}), test.StripMesh(),
                        material);
    }
    const Fenrir::Entity shared = test.AddSkeleton({0.0f, 4.0f, 0.0f});
    const auto other = test.Mesh(RendererTest::Strip());
    test.AddSkinned(shared, other, material);
    test.AddSkinned(shared, other, material);

    test.Frame();
    std::vector<std::uint32_t> computed;
    for (const RecordedDraw& draw : test.Draws())
    {
        computed.push_back(draw.draw.instanceCount);
    }
    std::sort(computed.begin(), computed.end());
    INFO("compute skinning on: the three skeletons' strips one draw each, the shared skeleton's two as one");
    REQUIRE(computed == std::vector<std::uint32_t>{1, 1, 1, 2});

    test.renderer.SetComputeSkinning(false);
    test.Frame();
    std::vector<std::uint32_t> vertex;
    for (const RecordedDraw& draw : test.Draws())
    {
        vertex.push_back(draw.draw.instanceCount);
    }
    std::sort(vertex.begin(), vertex.end());
    INFO("compute skinning off: each mesh's items one draw, each instance skinning from its own palette range");
    REQUIRE(vertex == std::vector<std::uint32_t>{2, 3});
}

TEST_CASE("Compute skinning turned off adds no Skinning pass and draws SKINNED variants, and turned on again skins in "
          "compute",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddRig();
    const auto keysOfTheDraw = [&] {
        const std::vector<RecordedDraw> draws = test.Draws();
        REQUIRE(draws.size() == 1);
        return RendererProbe::Keys(test.Device(), draws[0].pipeline);
    };

    test.Frame();
    REQUIRE(Dispatches(test).size() == 1);
    REQUIRE(keysOfTheDraw().empty());

    test.renderer.SetComputeSkinning(false);
    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("off: no Skinning pass, no dispatch, and the SKINNED variant");
    REQUIRE(std::find(passes.begin(), passes.end(), "Skinning") == passes.end());
    REQUIRE(Dispatches(test).empty());
    REQUIRE(keysOfTheDraw() == std::vector<std::string>{"SKINNED"});
    REQUIRE(SkinnedVertexDraws(test) == 0);

    test.renderer.SetComputeSkinning(true);
    test.Frame();
    INFO("on again: one dispatch, and the base variant");
    REQUIRE(Dispatches(test).size() == 1);
    REQUIRE(keysOfTheDraw().empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("RendererConfig::computeSkinning off skins every skinned draw in its vertex stage from the first frame",
          "[renderer][skinning][compute-skinning]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.computeSkinning = false;
    SkinningTestApp test(true, config);
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddRig();

    test.Frame();

    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    REQUIRE(std::find(passes.begin(), passes.end(), "Skinning") == passes.end());
    REQUIRE(Dispatches(test).empty());
    const std::vector<RecordedDraw> draws = test.Draws();
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline) == std::vector<std::string>{"SKINNED"});
}

TEST_CASE("A frame with no skinned mesh drawn, or only skipped ones, has no Skinning pass",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddMeshRenderer(test.Mesh(), test.Material());
    // Skin data and no SkinnedMesh: skipped (ADR-0035), so not skinned either.
    test.AddMeshRenderer(test.StripMesh(), test.Material());
    // Hidden.
    test.AddRig().mesh.GetComponent<Fenrir::MeshRenderer>().visible = false;

    test.Frame();

    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    REQUIRE(std::find(passes.begin(), passes.end(), "Skinning") == passes.end());
    REQUIRE(Dispatches(test).empty());
    REQUIRE(test.Draws().size() == 1);
}

TEST_CASE("With compute skinning, a shader without SKINNED still draws a skinned mesh as the fallback material, from "
          "its skinned vertices, logged once",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const Fenrir::Entity owner = test.AddSkeleton();
    // SampledPicture is built with no variant keys at all.
    test.AddSkinned(owner, test.StripMesh(), test.SampledMaterial({}));
    test.AddSkinned(owner, test.StripMesh(), test.SampledMaterial({}));

    test.Frame();
    test.Frame();

    const std::vector<RecordedDraw> draws = test.Draws();
    INFO("both are one draw of the fallback program's base variant, from one dispatch's vertices");
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().draw.instanceCount == 2);
    REQUIRE(RendererProbe::Label(test.Device(), draws.front().pipeline) == "BuiltinFallback");
    REQUIRE(RendererProbe::Keys(test.Device(), draws.front().pipeline).empty());
    REQUIRE(RendererProbe::Label(test.Device(), draws.front().vertexBuffers.at(0)) == "Skinned vertices");
    REQUIRE(Dispatches(test).size() == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "has no SKINNED variant") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A user shader with SKINNED draws its skinned meshes with its own SKINNED variant, with compute skinning on",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    Fenrir::ShaderData data;
    const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::UserSkinned));
    data.bytes.assign(bytes.begin(), bytes.end());
    const auto shader = test.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
    const Fenrir::Entity owner = test.AddSkeleton({-2.0f, 0.0f, 0.0f});
    test.AddSkinned(owner, test.StripMesh(), test.Store().CreateMaterial(shader, Fenrir::AssetOwner::App()));
    // The same mesh on another skeleton in builtin:lit, which is compute-skinned.
    test.AddRig({2.0f, 0.0f, 0.0f});

    test.Frame();

    const std::vector<RecordedDraw> draws = test.Draws();
    REQUIRE(draws.size() == 2);
    for (const RecordedDraw& draw : draws)
    {
        const bool user = RendererProbe::Label(test.Device(), draw.pipeline) == "UserSkinned";
        CAPTURE(user);
        INFO("the user shader's draw skins in its SKINNED vertex stage from the mesh; builtin:lit's reads the compute "
             "pass's vertices");
        REQUIRE(RendererProbe::Keys(test.Device(), draw.pipeline).empty() != user);
        REQUIRE((RendererProbe::Label(test.Device(), draw.vertexBuffers.at(0)) == "Skinned vertices") != user);
    }
    INFO("one dispatch, for builtin:lit's strip only");
    REQUIRE(Dispatches(test).size() == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("builtin:skinning is a compute program reading a job block, the mesh's vertices and the palette, and writing "
          "the skinned vertices",
          "[renderer][skinning][compute-skinning]")
{
    SkinningTestApp test;
    const Fenrir::ShaderData* data = test.Store().TryGet(test.Builtin("builtin:skinning"));
    REQUIRE(data != nullptr);
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(data->bytes);
    REQUIRE(read.Ok());
    REQUIRE(read.container->variants.size() == 1);
    const Fenrir::ShaderVariant& variant = read.container->variants.front();
    REQUIRE(variant.stages.size() == 1);
    REQUIRE(variant.stages.front().stage == Fenrir::ShaderStage::Compute);
    REQUIRE(variant.vertexInputs.empty());
    const auto binding = [&](std::uint32_t set, std::uint32_t at) {
        const auto found =
            std::find_if(variant.bindings.begin(), variant.bindings.end(),
                         [&](const Fenrir::ShaderBinding& b) { return b.set == set && b.binding == at; });
        REQUIRE(found != variant.bindings.end());
        return *found;
    };
    INFO("the job block, as the C++ writes it");
    REQUIRE(binding(0, 0).kind == Fenrir::ShaderBindingKind::UniformBuffer);
    REQUIRE(binding(0, 0).size == 48);
    REQUIRE(binding(1, 0).kind == Fenrir::ShaderBindingKind::StorageBuffer);
    INFO("the palette, in GpuBone's 48-byte layout");
    REQUIRE(binding(2, 0).kind == Fenrir::ShaderBindingKind::StorageBuffer);
    REQUIRE(binding(2, 0).size == 48);
    REQUIRE(binding(2, 1).kind == Fenrir::ShaderBindingKind::StorageBuffer);
    REQUIRE(variant.bindings.size() == 4);
}
