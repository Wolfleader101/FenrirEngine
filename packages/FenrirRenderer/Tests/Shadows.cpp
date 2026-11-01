// The sun's shadow on the null backend through App::RunFrame (the spec's seam 1, ADR-0050): which items each view's
// ShadowCascades pass draws, into which tiles of which atlas, with which pipelines and set 2, merged as Opaque merges
// them, and what the views that read it bind. The cascades' own maths is Cascades.cpp's.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/InstanceListData.hpp"
#include "RendererTestApp.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::CompareOp;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::TextureFormat;
using RendererTest::PassRecord;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A sun shining down and along -Z, as most scenes light. */
    Fenrir::Entity AddSun(RendererTestApp& test, bool castShadows = true, float intensity = 1.0f)
    {
        Fenrir::Entity sun = test.ActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot =
            glm::quatLookAt(glm::normalize(Fenrir::Math::Vec3(0.3f, -1.0f, -0.4f)), Fenrir::Math::Vec3(0, 1, 0));
        Fenrir::DirectionalLight light;
        light.intensity = intensity;
        light.castShadows = castShadows;
        sun.AddComponent<Fenrir::DirectionalLight>(light);
        return sun;
    }

    /** @brief A material on `shader` with `mode`. */
    Fenrir::AssetHandle<Fenrir::Material> Blended(RendererTestApp& test, Fenrir::BlendMode mode,
                                                  std::string_view shader = "builtin:lit")
    {
        const auto material = test.Material(shader);
        test.Store().SetBlendMode(material, mode);
        return material;
    }

    /** @brief A cube at `x` along the camera's view. */
    Fenrir::Entity AddCubeAt(RendererTestApp& test, float x, Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                             Fenrir::AssetHandle<Fenrir::Material> material)
    {
        Fenrir::Entity entity = test.AddMeshRenderer(mesh, material);
        entity.GetComponent<Fenrir::Transform>().pos = {x, 0.0f, 0.0f};
        return entity;
    }

    /** @brief Each draw's record of the draw buffer: which item it drew. */
    std::vector<std::uint32_t> Items(const std::vector<RecordedDraw>& draws)
    {
        std::vector<std::uint32_t> items;
        for (const RecordedDraw& draw : draws)
        {
            items.push_back(draw.draw.firstInstance);
        }
        return items;
    }

    /** @brief What a group binds at `binding`; empty if it binds nothing there. */
    BindingGroupEntry EntryAt(const RendererTestApp& test, Fenrir::Graphics::BindingGroupHandle group,
                              std::uint32_t binding)
    {
        for (const BindingGroupEntry& entry : RendererProbe::Entries(test.Device(), group))
        {
            if (entry.binding == binding)
            {
                return entry;
            }
        }
        return {};
    }

    /** @brief The bytes of the view block a draw's set 1 binds, from its slot's offset. */
    std::vector<std::byte> ViewBlock(const RendererTestApp& test, const RecordedDraw& draw)
    {
        const BindingGroupEntry block = EntryAt(test, draw.groups.at(1), 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), block.buffer);
        REQUIRE(block.offset + 608 <= bytes.size());
        return {bytes.begin() + static_cast<std::ptrdiff_t>(block.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(block.offset + 608)};
    }

    /** @brief A view block's shadowInfo.x: how many cascades it reads (`Fenrir/View.glsl`, byte 592). */
    std::uint32_t CascadesIn(const std::vector<std::byte>& block)
    {
        return RendererTest::UintAt(block, 592);
    }

    /** @brief The ShadowCascades passes of the last frame. */
    std::vector<PassRecord> ShadowPasses(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "ShadowCascades");
    }
} // namespace

TEST_CASE("The sun's ShadowCascades pass draws every opaque and masked item into each cascade's tile of one atlas",
          "[renderer][shadows]")
{
    RendererTestApp test;
    test.AddCamera();
    AddSun(test);
    const auto cube = test.Mesh();
    AddCubeAt(test, -2.0f, cube, Blended(test, Fenrir::BlendMode::Opaque));
    AddCubeAt(test, -1.0f, test.Mesh(RendererTest::Sphere()), Blended(test, Fenrir::BlendMode::Masked));
    AddCubeAt(test, 0.0f, cube, Blended(test, Fenrir::BlendMode::AlphaBlend));
    AddCubeAt(test, 1.0f, cube, Blended(test, Fenrir::BlendMode::Additive));
    AddCubeAt(test, 2.0f, test.Mesh(RendererTest::Sphere()), Blended(test, Fenrir::BlendMode::Opaque, "builtin:unlit"));

    test.Frame();
    const std::vector<PassRecord> passes = ShadowPasses(test);
    REQUIRE(passes.size() == 1);
    INFO("it runs first in the frame's views' passes, before the DepthPrepass");
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    const auto shadow = std::ranges::find(labels, "ShadowCascades");
    const auto prepass = std::ranges::find(labels, "DepthPrepass");
    REQUIRE(shadow < prepass);

    INFO("into a depth atlas of the four 2048-texel cascades side by side, cleared to 0 (far, reversed-Z), no colour");
    const Fenrir::Graphics::PassDesc& pass = passes.front().pass;
    REQUIRE(pass.colour.empty());
    REQUIRE(pass.depth.has_value());
    REQUIRE(pass.depth->load == LoadOp::Clear);
    REQUIRE(pass.depth->clear == 0.0f);
    const Fenrir::Graphics::TextureDesc atlas = RendererProbe::Desc(test.Device(), pass.depth->texture);
    REQUIRE(atlas.format == TextureFormat::Depth32Float);
    REQUIRE(atlas.width == 4 * 2048);
    REQUIRE(atlas.height == 2048);
    REQUIRE(atlas.samples == 1);

    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    INFO("the three opaque and masked items, once per cascade, and neither blended one");
    REQUIRE(opaque.size() == 3);
    REQUIRE(draws.size() == 4 * opaque.size());
    for (std::size_t c = 0; c < 4; ++c)
    {
        CAPTURE(c);
        const std::vector<RecordedDraw> cascade(draws.begin() + static_cast<std::ptrdiff_t>(c * 3),
                                                draws.begin() + static_cast<std::ptrdiff_t>((c + 1) * 3));
        INFO("each cascade draws Opaque's items in Opaque's order");
        REQUIRE(Items(cascade) == Items(opaque));
        for (const RecordedDraw& draw : cascade)
        {
            INFO("into its own tile of the atlas");
            REQUIRE(draw.viewport.x == static_cast<std::int32_t>(c * 2048));
            REQUIRE(draw.viewport.y == 0);
            REQUIRE(draw.viewport.width == 2048);
            REQUIRE(draw.viewport.height == 2048);
            INFO("through a view block of its own, not the camera's");
            REQUIRE(draw.groups.at(1) != opaque.front().groups.at(1));
            INFO("which reads no shadow itself");
            REQUIRE(CascadesIn(ViewBlock(test, draw)) == 0);
        }
    }
    REQUIRE(test.renderer.Stats().shadowDraws == 12);
    INFO("nothing was logged as an error, nor a warning about shadows");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "shadow") == 0);
}

TEST_CASE("Shadow pipelines are depth only, uncull both faces, and cut Masked items out by their prepass mask",
          "[renderer][shadows]")
{
    RendererTestApp test;
    test.AddCamera();
    AddSun(test);
    AddCubeAt(test, -1.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
    AddCubeAt(test, 1.0f, test.Mesh(RendererTest::Sphere()), Blended(test, Fenrir::BlendMode::Masked));

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    REQUIRE(draws.size() == 8);
    std::size_t masked = 0;
    for (const RecordedDraw& draw : draws)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        REQUIRE(RendererProbe::Label(test.Device(), draw.pipeline) == "ShadowCascades");
        INFO("both faces cast; depth is tested and written greater-or-equal, reversed-Z");
        REQUIRE(state.cull == Fenrir::Graphics::CullMode::None);
        REQUIRE(state.depth.test);
        REQUIRE(state.depth.write);
        REQUIRE(state.depth.compare == CompareOp::GreaterEqual);
        REQUIRE(RendererProbe::Samples(test.Device(), draw.pipeline) == 1);
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        if (std::ranges::find(keys, "ALPHA_MASK") != keys.end())
        {
            ++masked;
            INFO("a Masked item binds its prepass mask group at set 2");
            REQUIRE(test.GroupLabel(draw, 2) == "Prepass mask");
        }
        else
        {
            REQUIRE(test.GroupLabel(draw, 2) != "Prepass mask");
        }
    }
    INFO("the masked item in each of four cascades");
    REQUIRE(masked == 4);
}

TEST_CASE("Opaque and Transparent read the atlas at set 1 binding 2 by comparison, and the cascades in the view block",
          "[renderer][shadows]")
{
    RendererTestApp test;
    test.AddCamera();
    AddSun(test);
    AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
    AddCubeAt(test, 1.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::AlphaBlend));

    test.Frame();
    const std::vector<PassRecord> passes = ShadowPasses(test);
    REQUIRE(passes.size() == 1);
    const Fenrir::Graphics::TextureHandle atlas = passes.front().pass.depth->texture;
    for (const char* name : {"Opaque", "Transparent"})
    {
        CAPTURE(name);
        const std::vector<RecordedDraw> draws = test.Draws(name);
        REQUIRE(draws.size() == 1);
        const BindingGroupEntry shadowMap = EntryAt(test, draws.front().groups.at(1), 2);
        REQUIRE(shadowMap.texture == atlas);
        INFO("with a sampler of its own, the comparison one, not the occlusion's");
        REQUIRE_FALSE(shadowMap.sampler.IsEmpty());
        REQUIRE(shadowMap.sampler != EntryAt(test, draws.front().groups.at(1), 1).sampler);
        const std::vector<std::byte> block = ViewBlock(test, draws.front());
        INFO("four cascades, their splits rising to the light's 100-unit shadow distance");
        REQUIRE(CascadesIn(block) == 4);
        float previous = 0.0f;
        for (std::size_t c = 0; c < 4; ++c)
        {
            const float split = RendererTest::FloatAt(block, 544 + c * 4);
            REQUIRE(split > previous);
            previous = split;
        }
        REQUIRE(previous == 100.0f);
        INFO("the light's biases in texels, the filter's radius and the blend share");
        REQUIRE(RendererTest::FloatAt(block, 576) == 1.0f);
        REQUIRE(RendererTest::FloatAt(block, 580) == 2.0f);
    }
    INFO("the prepass, which shades nothing, binds the map that lights everything");
    const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");
    REQUIRE(prepass.size() == 1);
    REQUIRE(EntryAt(test, prepass.front().groups.at(1), 2).texture != atlas);
    REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, prepass.front().groups.at(1), 2).texture) == "No shadow");
}

TEST_CASE("No sun, a sun with castShadows off or no light, or nothing that casts, adds no shadow pass",
          "[renderer][shadows]")
{
    const auto expectNoShadow = [](RendererTestApp& test) {
        test.Frame();
        REQUIRE(ShadowPasses(test).empty());
        const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
        REQUIRE_FALSE(opaque.empty());
        INFO("Opaque binds the map that lights everything, and its view block has no cascades");
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, opaque.front().groups.at(1), 2).texture) ==
                "No shadow");
        REQUIRE(CascadesIn(ViewBlock(test, opaque.front())) == 0);
        REQUIRE(test.renderer.Stats().shadowDraws == 0);
    };

    SECTION("no DirectionalLight")
    {
        RendererTestApp test;
        test.AddCamera();
        AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
        expectNoShadow(test);
    }
    SECTION("castShadows off")
    {
        RendererTestApp test;
        test.AddCamera();
        AddSun(test, false);
        AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
        expectNoShadow(test);
    }
    SECTION("a sun of intensity 0")
    {
        RendererTestApp test;
        test.AddCamera();
        AddSun(test, true, 0.0f);
        AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
        expectNoShadow(test);
    }
    SECTION("every MeshRenderer has castShadows off, or is blended")
    {
        RendererTestApp test;
        test.AddCamera();
        AddSun(test);
        Fenrir::Entity cube = AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));
        cube.GetComponent<Fenrir::MeshRenderer>().castShadows = false;
        AddCubeAt(test, 1.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::AlphaBlend));
        expectNoShadow(test);
    }
}

TEST_CASE("A MeshRenderer or InstancedMeshRenderer with castShadows off is drawn but casts nothing",
          "[renderer][shadows]")
{
    RendererTestApp test;
    test.AddCamera();
    AddSun(test);
    const auto material = Blended(test, Fenrir::BlendMode::Opaque);
    AddCubeAt(test, -1.0f, test.Mesh(), material);
    Fenrir::Entity hidden = AddCubeAt(test, 1.0f, test.Mesh(RendererTest::Sphere()), material);
    hidden.GetComponent<Fenrir::MeshRenderer>().castShadows = false;

    Fenrir::InstanceListData data;
    data.transforms = {Fenrir::Math::Mat4(1.0f), Fenrir::Math::Mat4(1.0f), Fenrir::Math::Mat4(1.0f)};
    const auto list = test.Store().Add<Fenrir::InstanceList>(data, Fenrir::AssetOwner::App());
    const auto listMesh = test.Mesh();
    Fenrir::Entity casting = test.ActiveScene().GetEntityList().CreateEntity();
    casting.AddComponent<Fenrir::InstancedMeshRenderer>(Fenrir::InstancedMeshRenderer{listMesh, material, list});
    Fenrir::Entity shy = test.ActiveScene().GetEntityList().CreateEntity();
    shy.AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{listMesh, material, list, false, true});

    test.Frame();
    INFO("four items in Opaque");
    REQUIRE(test.Draws("Opaque").size() == 4);
    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    INFO("the casting cube and the casting list, each once a cascade");
    REQUIRE(draws.size() == 8);
    const auto sphereIndices = static_cast<std::uint32_t>(RendererTest::Sphere().indices.size());
    std::size_t listDraws = 0;
    for (const RecordedDraw& draw : draws)
    {
        INFO("the hidden sphere casts in no cascade: every shadow draw is of a cube");
        REQUIRE(draw.draw.indexCount != sphereIndices);
        REQUIRE(draw.draw.indexCount == 36);
        listDraws += draw.draw.instanceCount == 3 ? 1 : 0;
    }
    INFO("the list draws all three copies in each cascade, as one draw");
    REQUIRE(listDraws == 4);
}

TEST_CASE("Matching casting items merge into one instanced shadow draw a cascade, as in Opaque",
          "[renderer][shadows][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    AddSun(test);
    const auto cube = test.Mesh();
    const auto material = Blended(test, Fenrir::BlendMode::Opaque);
    for (int i = 0; i < 5; ++i)
    {
        AddCubeAt(test, static_cast<float>(i) - 2.0f, cube, material);
    }

    test.Frame();
    REQUIRE(test.Draws("Opaque").size() == 1);
    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    REQUIRE(draws.size() == 4);
    for (const RecordedDraw& draw : draws)
    {
        REQUIRE(draw.draw.instanceCount == 5);
        REQUIRE(draw.draw.firstInstance == test.Draws("Opaque").front().draw.firstInstance);
    }
    REQUIRE(test.renderer.Stats().shadowDraws == 4);
}

TEST_CASE("RendererConfig sets the cascades per view and their maps' size, and each view has its own atlas",
          "[renderer][shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.shadowCascades = 2;
    config.shadowMapSize = 512;
    RendererTestApp test(config);
    test.AddCamera();
    test.AddCamera({4.0f, 0.0f, 5.0f});
    AddSun(test);
    AddCubeAt(test, 0.0f, test.Mesh(), Blended(test, Fenrir::BlendMode::Opaque));

    test.Frame();
    const std::vector<PassRecord> passes = ShadowPasses(test);
    INFO("one pass and one atlas per view");
    REQUIRE(passes.size() == 2);
    REQUIRE(passes[0].pass.depth->texture != passes[1].pass.depth->texture);
    for (const PassRecord& pass : passes)
    {
        const Fenrir::Graphics::TextureDesc atlas = RendererProbe::Desc(test.Device(), pass.pass.depth->texture);
        REQUIRE(atlas.width == 2 * 512);
        REQUIRE(atlas.height == 512);
    }
    REQUIRE(test.Draws("ShadowCascades").size() == 4);
    for (const RecordedDraw& draw : test.Draws("Opaque"))
    {
        REQUIRE(CascadesIn(ViewBlock(test, draw)) == 2);
    }
}

TEST_CASE("Compute-skinned items cast from the skinned vertex buffer, which the shadow pass declares reading",
          "[renderer][shadows][compute-skinning]")
{
    RendererTest::SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    AddSun(test);
    const Fenrir::Entity skeleton = test.AddSkeleton();
    test.AddSkinned(skeleton, test.StripMesh(), test.Material());

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    REQUIRE(draws.size() == 4);
    for (const RecordedDraw& draw : draws)
    {
        INFO("positions, normals and tangents from the Skinning pass's buffer, drawn rigidly");
        REQUIRE(RendererProbe::Label(test.Device(), draw.vertexBuffers.at(0)) == "Skinned vertices");
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        REQUIRE(std::ranges::find(keys, "SKINNED") == keys.end());
    }
    const std::vector<PassRecord> passes = ShadowPasses(test);
    REQUIRE(passes.size() == 1);
    const auto& uses = passes.front().pass.uses;
    REQUIRE(std::ranges::any_of(uses, [](const Fenrir::Graphics::PassResourceUse& use) {
        return use.usage == Fenrir::Graphics::ResourceUsage::VertexInput;
    }));
    REQUIRE(test.messages->errors.empty());
}
