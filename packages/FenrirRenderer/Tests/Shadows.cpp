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

    /** @name The view block's shadow members, as `Fenrir/View.glsl` lays them out (std140): byte offsets.
     * @{
     */
    constexpr std::size_t ViewBlockBytes = 608;
    constexpr std::size_t ShadowSplitsAt = 544;
    constexpr std::size_t ShadowBiasAt = 576;
    constexpr std::size_t ShadowInfoAt = 592;
    /** @} */

    /** @brief The bytes of the view block a draw's set 1 binds, from its slot's offset. */
    std::vector<std::byte> ViewBlock(const RendererTestApp& test, const RecordedDraw& draw)
    {
        const BindingGroupEntry block = EntryAt(test, draw.groups.at(1), 0);
        const std::vector<std::byte> bytes = RendererProbe::Bytes(test.Device(), block.buffer);
        INFO("the view block's slot lies inside its buffer");
        REQUIRE(block.offset + ViewBlockBytes <= bytes.size());
        return {bytes.begin() + static_cast<std::ptrdiff_t>(block.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(block.offset + ViewBlockBytes)};
    }

    /** @brief A view block's shadowInfo.x: how many cascades it reads. */
    std::uint32_t CascadesIn(const std::vector<std::byte>& block)
    {
        return RendererTest::UintAt(block, ShadowInfoAt);
    }

    /** @brief The ShadowCascades passes of the last frame. */
    std::vector<PassRecord> ShadowPasses(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "ShadowCascades");
    }

    /** @brief A camera and a sun over an opaque cube, a masked sphere, two blended cubes and an unlit sphere. */
    void AddMixedScene(RendererTestApp& test)
    {
        test.AddCamera();
        AddSun(test);
        const auto cube = test.Mesh();
        AddCubeAt(test, -2.0f, cube, Blended(test, Fenrir::BlendMode::Opaque));
        AddCubeAt(test, -1.0f, test.Mesh(RendererTest::Sphere()), Blended(test, Fenrir::BlendMode::Masked));
        AddCubeAt(test, 0.0f, cube, Blended(test, Fenrir::BlendMode::AlphaBlend));
        AddCubeAt(test, 1.0f, cube, Blended(test, Fenrir::BlendMode::Additive));
        AddCubeAt(test, 2.0f, test.Mesh(RendererTest::Sphere()),
                  Blended(test, Fenrir::BlendMode::Opaque, "builtin:unlit"));
    }
} // namespace

TEST_CASE("The sun's ShadowCascades pass runs before the DepthPrepass", "[renderer][shadows]")
{
    RendererTestApp test;
    AddMixedScene(test);

    test.Frame();
    INFO("one pass for the one view");
    REQUIRE(ShadowPasses(test).size() == 1);
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("in the Shadow stage, ahead of the view's DepthPrepass");
    REQUIRE(std::ranges::find(labels, "ShadowCascades") < std::ranges::find(labels, "DepthPrepass"));
    INFO("nothing was logged as an error, nor a warning about shadows");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "shadow") == 0);
}

TEST_CASE("The ShadowCascades pass draws into a depth atlas of the cascades side by side, cleared to far",
          "[renderer][shadows]")
{
    RendererTestApp test;
    AddMixedScene(test);

    test.Frame();
    const std::vector<PassRecord> passes = ShadowPasses(test);
    INFO("one pass");
    REQUIRE(passes.size() == 1);
    const Fenrir::Graphics::PassDesc& pass = passes.front().pass;
    INFO("no colour, and depth cleared to 0, the far plane under reversed-Z");
    REQUIRE(pass.colour.empty());
    REQUIRE(pass.depth.has_value());
    REQUIRE(pass.depth->load == LoadOp::Clear);
    REQUIRE(pass.depth->clear == 0.0f);
    const Fenrir::Graphics::TextureDesc atlas = RendererProbe::Desc(test.Device(), pass.depth->texture);
    INFO("Depth32Float, the four 2048-texel cascades side by side, single-sampled");
    REQUIRE(atlas.format == TextureFormat::Depth32Float);
    REQUIRE(atlas.width == 4 * 2048);
    REQUIRE(atlas.height == 2048);
    REQUIRE(atlas.samples == 1);
}

TEST_CASE("Each cascade draws Opaque's opaque and masked items, and no blended one, into its own tile",
          "[renderer][shadows]")
{
    RendererTestApp test;
    AddMixedScene(test);

    test.Frame();
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
            INFO("through a view block of its own, not the camera's, which reads no shadow itself");
            REQUIRE(draw.groups.at(1) != opaque.front().groups.at(1));
            REQUIRE(CascadesIn(ViewBlock(test, draw)) == 0);
        }
    }
    INFO("RendererStats counts the twelve shadow draws");
    REQUIRE(test.renderer.Stats().shadowDraws == 12);
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
    INFO("two items in four cascades");
    REQUIRE(draws.size() == 8);
    std::size_t masked = 0;
    for (const RecordedDraw& draw : draws)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        INFO("a ShadowCascades pipeline, single-sampled; both faces cast; depth tested and written greater-or-equal");
        REQUIRE(RendererProbe::Label(test.Device(), draw.pipeline) == "ShadowCascades");
        REQUIRE(RendererProbe::Samples(test.Device(), draw.pipeline) == 1);
        REQUIRE(state.cull == Fenrir::Graphics::CullMode::None);
        REQUIRE(state.depth.test);
        REQUIRE(state.depth.write);
        REQUIRE(state.depth.compare == CompareOp::GreaterEqual);
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        const bool cutOut = std::ranges::find(keys, "ALPHA_MASK") != keys.end();
        masked += cutOut ? 1 : 0;
        INFO("a Masked item, and only a Masked one, binds its prepass mask group at set 2");
        REQUIRE((test.GroupLabel(draw, 2) == "Prepass mask") == cutOut);
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
    INFO("one shadow pass");
    REQUIRE(passes.size() == 1);
    const Fenrir::Graphics::TextureHandle atlas = passes.front().pass.depth->texture;
    for (const char* name : {"Opaque", "Transparent"})
    {
        CAPTURE(name);
        const std::vector<RecordedDraw> draws = test.Draws(name);
        INFO("one draw in the pass");
        REQUIRE(draws.size() == 1);
        const BindingGroupEntry shadowAtlas = EntryAt(test, draws.front().groups.at(1), 2);
        INFO("the atlas at set 1 binding 2, with a sampler of its own (the comparison one), not the occlusion's");
        REQUIRE(shadowAtlas.texture == atlas);
        REQUIRE_FALSE(shadowAtlas.sampler.IsEmpty());
        REQUIRE(shadowAtlas.sampler != EntryAt(test, draws.front().groups.at(1), 1).sampler);
        const std::vector<std::byte> block = ViewBlock(test, draws.front());
        INFO("four cascades, their splits rising to the light's 100-unit shadow distance");
        REQUIRE(CascadesIn(block) == 4);
        float previous = 0.0f;
        for (std::size_t c = 0; c < 4; ++c)
        {
            const float split = RendererTest::FloatAt(block, ShadowSplitsAt + c * 4);
            REQUIRE(split > previous);
            previous = split;
        }
        REQUIRE(previous == 100.0f);
        INFO("the light's default normal and slope biases, in texels");
        REQUIRE(RendererTest::FloatAt(block, ShadowBiasAt) == 1.0f);
        REQUIRE(RendererTest::FloatAt(block, ShadowBiasAt + 4) == 2.0f);
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
        INFO("no shadow pass, and Opaque still draws");
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
    INFO("Opaque merges the five cubes into one draw");
    REQUIRE(test.Draws("Opaque").size() == 1);
    const std::vector<RecordedDraw> draws = test.Draws("ShadowCascades");
    INFO("and each cascade draws them as one draw of five instances, from the same first record");
    REQUIRE(draws.size() == 4);
    for (const RecordedDraw& draw : draws)
    {
        REQUIRE(draw.draw.instanceCount == 5);
        REQUIRE(draw.draw.firstInstance == test.Draws("Opaque").front().draw.firstInstance);
    }
    INFO("RendererStats counts four shadow draws");
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
        INFO("two 512-texel cascades side by side");
        REQUIRE(atlas.width == 2 * 512);
        REQUIRE(atlas.height == 512);
    }
    INFO("the cube once in each of two cascades of two views");
    REQUIRE(test.Draws("ShadowCascades").size() == 4);
    for (const RecordedDraw& draw : test.Draws("Opaque"))
    {
        INFO("each view reads two cascades");
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
    INFO("the skinned strip in each of four cascades");
    REQUIRE(draws.size() == 4);
    for (const RecordedDraw& draw : draws)
    {
        INFO("positions, normals and tangents from the Skinning pass's buffer, drawn rigidly");
        REQUIRE(RendererProbe::Label(test.Device(), draw.vertexBuffers.at(0)) == "Skinned vertices");
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        REQUIRE(std::ranges::find(keys, "SKINNED") == keys.end());
    }
    const std::vector<PassRecord> passes = ShadowPasses(test);
    INFO("the one shadow pass declares the skinned vertices as vertex input, so it runs after the Skinning pass");
    REQUIRE(passes.size() == 1);
    const auto& uses = passes.front().pass.uses;
    REQUIRE(std::ranges::any_of(uses, [](const Fenrir::Graphics::PassResourceUse& use) {
        return use.usage == Fenrir::Graphics::ResourceUsage::VertexInput;
    }));
    INFO("nothing was logged as an error");
    REQUIRE(test.messages->errors.empty());
}
