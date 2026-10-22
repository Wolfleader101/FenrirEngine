#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

using AssetsTest::AssetsApp;
using Fenrir::AssetHandle;
using Fenrir::AssetOwner;
using Fenrir::Material;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;

namespace
{
    using Type = Fenrir::ShaderParameterType;

    // A fixture shader container written by hand: one variant with a parameter of every type, as
    // FenrirShaderCompiler would reflect `#pragma fenrir default` lines for them.
    std::vector<std::byte> FixtureShaderBytes(std::vector<Fenrir::ShaderParameter> parameters)
    {
        Fenrir::ShaderContainer container;
        container.name = "MaterialFixture";
        Fenrir::ShaderVariant variant;
        variant.stages.push_back({Fenrir::ShaderStage::Vertex, {0x07230203u}, {0x07230203u}});
        variant.stages.push_back({Fenrir::ShaderStage::Fragment, {0x07230203u}, {0x07230203u}});
        variant.parameters = std::move(parameters);
        container.variants.push_back(std::move(variant));
        return Fenrir::WriteShaderContainer(container);
    }

    // The fixture's parameters: tint (vec4, a colour), roughness (float), offset (vec2), emissive (vec3, no
    // pragma so zero) and baseColourTexture (default normal).
    std::vector<Fenrir::ShaderParameter> FixtureParameters()
    {
        return {
            {"tint", Type::Vector4, {1.0f, 0.5f, 0.25f, 1.0f}},
            {"roughness", Type::Float, {0.5f}},
            {"offset", Type::Vector2, {2.0f, 3.0f}},
            {"emissive", Type::Vector3, {}},
            {"baseColourTexture", Type::Texture, {}, Fenrir::DefaultTexture::Normal},
        };
    }

    AssetHandle<Fenrir::Shader> AddShader(AssetsApp& test, std::vector<Fenrir::ShaderParameter> parameters)
    {
        Fenrir::ShaderData data;
        data.bytes = FixtureShaderBytes(std::move(parameters));
        return test.store.Add<Fenrir::Shader>(std::move(data), AssetOwner::App());
    }

    AssetHandle<Fenrir::Shader> AddFixtureShader(AssetsApp& test)
    {
        return AddShader(test, FixtureParameters());
    }

    // A one-pixel image a texture parameter can name.
    AssetHandle<Fenrir::Image> AddPixel(AssetsApp& test)
    {
        Fenrir::ImageData image;
        image.width = 1;
        image.height = 1;
        image.pixels.assign(4, std::byte{0xff});
        return test.store.Add<Fenrir::Image>(std::move(image), AssetOwner::App());
    }
} // namespace

TEST_CASE("A new material reads its shader's parameter defaults and sets nothing", "[assets][materials]")
{
    AssetsApp test;
    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());
    REQUIRE(test.store.IsValid(material));

    INFO("a float reads the shader's default");
    REQUIRE(test.store.TryGetFloat(material, "roughness") == 0.5f);
    INFO("a vec2 reads the shader's default");
    REQUIRE(test.store.TryGetVector2(material, "offset") == Vec2(2.0f, 3.0f));
    INFO("a vec3 with no pragma reads zero");
    REQUIRE(test.store.TryGetVector3(material, "emissive") == Vec3(0.0f));
    INFO("a vec4 reads the shader's default, as a vector or as a colour");
    REQUIRE(test.store.TryGetVector4(material, "tint") == Vec4(1.0f, 0.5f, 0.25f, 1.0f));
    const std::optional<Fenrir::Colour> tint = test.store.TryGetColour(material, "tint");
    REQUIRE(tint.has_value());
    REQUIRE((tint->r == 1.0f && tint->g == 0.5f && tint->b == 0.25f && tint->a == 1.0f));
    INFO("a texture reads no image, so the shader's default texture, with the default sampler");
    const std::optional<Fenrir::MaterialTexture> texture = test.store.TryGetTexture(material, "baseColourTexture");
    REQUIRE(texture.has_value());
    REQUIRE(texture->image.IsEmpty());
    REQUIRE(texture->sampler == Fenrir::SamplerDescription{});

    INFO("no parameter is set");
    for (const char* name : {"roughness", "offset", "emissive", "tint", "baseColourTexture"})
    {
        INFO(name);
        REQUIRE_FALSE(test.store.IsSet(material, name));
    }
    REQUIRE_FALSE(test.store.TryGetOverrideFloat(material, "roughness").has_value());
    REQUIRE_FALSE(test.store.TryGetOverrideTexture(material, "baseColourTexture").has_value());
    INFO("and the material's own table is empty");
    REQUIRE(test.store.TryGet(material)->overrides.empty());
}

TEST_CASE("The default sampler repeats, filters trilinearly and takes anisotropy 8", "[assets][materials]")
{
    const Fenrir::SamplerDescription sampler;

    INFO("the spec's default sampler");
    REQUIRE(sampler.wrapU == Fenrir::SamplerWrap::Repeat);
    REQUIRE(sampler.wrapV == Fenrir::SamplerWrap::Repeat);
    REQUIRE(sampler.filter == Fenrir::SamplerFilter::Trilinear);
    REQUIRE(sampler.anisotropy == 8);
}

TEST_CASE("Setting a parameter overrides its default until it is reset", "[assets][materials]")
{
    AssetsApp test;
    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());

    INFO("the set is taken");
    REQUIRE(test.store.SetFloat(material, "roughness", 0.9f));
    INFO("the parameter is now set");
    REQUIRE(test.store.IsSet(material, "roughness"));
    INFO("the override reads back as an override");
    REQUIRE(test.store.TryGetOverrideFloat(material, "roughness") == 0.9f);
    INFO("and as the effective value");
    REQUIRE(test.store.TryGetFloat(material, "roughness") == 0.9f);
    INFO("the other parameters are still unset");
    REQUIRE_FALSE(test.store.IsSet(material, "offset"));

    INFO("Reset removes the override");
    REQUIRE(test.store.Reset(material, "roughness"));
    REQUIRE_FALSE(test.store.IsSet(material, "roughness"));
    REQUIRE_FALSE(test.store.TryGetOverrideFloat(material, "roughness").has_value());
    INFO("so the shader's default shows again");
    REQUIRE(test.store.TryGetFloat(material, "roughness") == 0.5f);
}

TEST_CASE("Every parameter type and a colour can be set and read back", "[assets][materials]")
{
    AssetsApp test;
    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());
    const AssetHandle<Fenrir::Image> image = AddPixel(test);
    const Fenrir::SamplerDescription sampler{Fenrir::SamplerWrap::ClampToEdge, Fenrir::SamplerWrap::MirroredRepeat,
                                             Fenrir::SamplerFilter::Nearest, 1};

    REQUIRE(test.store.SetVector2(material, "offset", Vec2(-1.0f, 4.0f)));
    REQUIRE(test.store.SetVector3(material, "emissive", Vec3(0.25f, 0.5f, 0.75f)));
    REQUIRE(test.store.SetColour(material, "tint", Fenrir::Colour{0.125f, 0.25f, 0.375f, 0.5f}));
    REQUIRE(test.store.SetTexture(material, "baseColourTexture", image, sampler));

    INFO("a vec2 reads back");
    REQUIRE(test.store.TryGetOverrideVector2(material, "offset") == Vec2(-1.0f, 4.0f));
    INFO("a vec3 reads back");
    REQUIRE(test.store.TryGetVector3(material, "emissive") == Vec3(0.25f, 0.5f, 0.75f));
    INFO("a colour is a vec4: it reads back as either");
    REQUIRE(test.store.TryGetVector4(material, "tint") == Vec4(0.125f, 0.25f, 0.375f, 0.5f));
    const std::optional<Fenrir::Colour> tint = test.store.TryGetOverrideColour(material, "tint");
    REQUIRE(tint.has_value());
    REQUIRE((tint->r == 0.125f && tint->g == 0.25f && tint->b == 0.375f && tint->a == 0.5f));
    INFO("a texture reads back with its image and its sampler");
    REQUIRE(test.store.TryGetTexture(material, "baseColourTexture") == Fenrir::MaterialTexture{image, sampler});

    REQUIRE(test.store.SetVector4(material, "tint", Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
    INFO("SetVector4 sets the same parameter");
    REQUIRE(test.store.TryGetVector4(material, "tint") == Vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE("Each set bumps a material's revision, and reads never do", "[assets][materials][revisions]")
{
    AssetsApp test;
    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());
    INFO("a new material starts at revision 1");
    REQUIRE(test.store.Revision(material) == 1);

    REQUIRE(test.store.SetFloat(material, "roughness", 0.9f));
    INFO("a parameter set bumps it");
    REQUIRE(test.store.Revision(material) == 2);
    REQUIRE(test.store.SetFloat(material, "roughness", 0.9f));
    INFO("setting the same value again still bumps it");
    REQUIRE(test.store.Revision(material) == 3);
    REQUIRE(test.store.SetBlendMode(material, Fenrir::BlendMode::Masked));
    REQUIRE(test.store.SetAlphaCutoff(material, 0.25f));
    REQUIRE(test.store.SetDoubleSided(material, true));
    INFO("blend mode, alpha cutoff and double-sidedness sets bump it once each");
    REQUIRE(test.store.Revision(material) == 6);

    (void)test.store.TryGetFloat(material, "roughness");
    (void)test.store.TryGetOverrideFloat(material, "roughness");
    (void)test.store.TryGetTexture(material, "baseColourTexture");
    (void)test.store.IsSet(material, "roughness");
    (void)test.store.TryGet(material);
    INFO("reads leave it alone");
    REQUIRE(test.store.Revision(material) == 6);

    INFO("resetting an unset parameter removes nothing and leaves it alone");
    REQUIRE_FALSE(test.store.Reset(material, "offset"));
    REQUIRE(test.store.Revision(material) == 6);
    INFO("resetting a set parameter bumps it");
    REQUIRE(test.store.Reset(material, "roughness"));
    REQUIRE(test.store.Revision(material) == 7);
    INFO("resetting it again removes nothing");
    REQUIRE_FALSE(test.store.Reset(material, "roughness"));
    REQUIRE(test.store.Revision(material) == 7);
}

TEST_CASE("A clone copies overrides, not resolved defaults", "[assets][materials]")
{
    AssetsApp test;
    const std::vector<Fenrir::ShaderParameter> before = FixtureParameters();
    const AssetHandle<Fenrir::Shader> shader = AddShader(test, before);
    const AssetHandle<Material> material = test.store.CreateMaterial(shader, AssetOwner::App());
    REQUIRE(test.store.SetFloat(material, "roughness", 0.9f));
    REQUIRE(test.store.SetBlendMode(material, Fenrir::BlendMode::Additive));
    REQUIRE(test.store.SetDoubleSided(material, true));

    const AssetHandle<Material> clone = test.store.CloneMaterial(material, AssetOwner::App());
    INFO("the clone is a new material");
    REQUIRE(test.store.IsValid(clone));
    REQUIRE(clone != material);
    INFO("at revision 1");
    REQUIRE(test.store.Revision(clone) == 1);
    INFO("it keeps the override");
    REQUIRE(test.store.TryGetOverrideFloat(clone, "roughness") == 0.9f);
    INFO("and the blend mode, double-sidedness and shader");
    REQUIRE(test.store.TryGet(clone)->blendMode == Fenrir::BlendMode::Additive);
    REQUIRE(test.store.TryGet(clone)->doubleSided);
    REQUIRE(test.store.TryGet(clone)->shader == shader);
    INFO("but the parameters the original left unset are unset in the clone too");
    REQUIRE_FALSE(test.store.IsSet(clone, "offset"));
    REQUIRE(test.store.TryGet(clone)->overrides.size() == 1);

    std::vector<Fenrir::ShaderParameter> after = before;
    after[2].value = {7.0f, 8.0f, 0.0f, 0.0f}; // offset's default changes
    Fenrir::ShaderData changed;
    changed.bytes = FixtureShaderBytes(after);
    REQUIRE(test.store.Replace(shader, std::move(changed)));
    INFO("so the clone's unset parameter follows the shader's new default");
    REQUIRE(test.store.TryGetVector2(clone, "offset") == Vec2(7.0f, 8.0f));
    INFO("while its override stays");
    REQUIRE(test.store.TryGetFloat(clone, "roughness") == 0.9f);

    REQUIRE(test.store.SetFloat(clone, "roughness", 0.1f));
    INFO("editing the clone leaves the original alone");
    REQUIRE(test.store.TryGetFloat(material, "roughness") == 0.9f);
}

TEST_CASE("An unknown name or a wrong type is logged once per material and ignored", "[assets][materials]")
{
    AssetsApp test;
    const AssetHandle<Fenrir::Shader> shader = AddFixtureShader(test);
    const AssetHandle<Material> material = test.store.CreateMaterial(shader, AssetOwner::App());

    INFO("a set of a name the shader lacks is refused");
    REQUIRE_FALSE(test.store.SetFloat(material, "shininess", 1.0f));
    REQUIRE_FALSE(test.store.SetFloat(material, "shininess", 2.0f));
    INFO("and logged once, naming the parameter");
    REQUIRE(test.errors->size() == 1);
    REQUIRE(test.errors->back().find("shininess") != std::string::npos);

    INFO("a set of the wrong type is refused");
    REQUIRE_FALSE(test.store.SetFloat(material, "offset", 1.0f));
    REQUIRE_FALSE(test.store.SetFloat(material, "offset", 2.0f));
    INFO("and logged once, naming the parameter");
    REQUIRE(test.errors->size() == 2);
    REQUIRE(test.errors->back().find("offset") != std::string::npos);

    INFO("neither left an override or bumped the revision");
    REQUIRE(test.store.TryGet(material)->overrides.empty());
    REQUIRE(test.store.Revision(material) == 1);
    INFO("reading an unknown name is empty");
    REQUIRE_FALSE(test.store.TryGetFloat(material, "shininess").has_value());
    INFO("reading with the wrong type is empty");
    REQUIRE_FALSE(test.store.TryGetFloat(material, "offset").has_value());
    REQUIRE_FALSE(test.store.TryGetColour(material, "offset").has_value());
    REQUIRE_FALSE(test.store.TryGetTexture(material, "roughness").has_value());

    const AssetHandle<Material> other = test.store.CreateMaterial(shader, AssetOwner::App());
    REQUIRE_FALSE(test.store.SetFloat(other, "shininess", 1.0f));
    INFO("another material logs the same mistake once of its own");
    REQUIRE(test.errors->size() == 3);
}

TEST_CASE("Blend mode, alpha cutoff, double-sidedness and texture samplers round-trip", "[assets][materials]")
{
    AssetsApp test;
    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());
    const Fenrir::MaterialData* data = test.store.TryGet(material);
    INFO("a new material is opaque and single-sided");
    REQUIRE(data->blendMode == Fenrir::BlendMode::Opaque);
    REQUIRE_FALSE(data->doubleSided);

    for (const Fenrir::BlendMode mode : {Fenrir::BlendMode::Masked, Fenrir::BlendMode::AlphaBlend,
                                         Fenrir::BlendMode::Additive, Fenrir::BlendMode::Opaque})
    {
        REQUIRE(test.store.SetBlendMode(material, mode));
        INFO("each blend mode reads back as set");
        REQUIRE(test.store.TryGet(material)->blendMode == mode);
    }
    REQUIRE(test.store.SetAlphaCutoff(material, 0.375f));
    INFO("the alpha cutoff reads back as set");
    REQUIRE(test.store.TryGet(material)->alphaCutoff == 0.375f);
    REQUIRE(test.store.SetDoubleSided(material, true));
    INFO("double-sidedness reads back as set");
    REQUIRE(test.store.TryGet(material)->doubleSided);

    const AssetHandle<Fenrir::Image> image = AddPixel(test);
    for (const Fenrir::SamplerDescription sampler :
         {Fenrir::SamplerDescription{Fenrir::SamplerWrap::Repeat, Fenrir::SamplerWrap::Repeat,
                                     Fenrir::SamplerFilter::Trilinear, 8},
          Fenrir::SamplerDescription{Fenrir::SamplerWrap::MirroredRepeat, Fenrir::SamplerWrap::ClampToEdge,
                                     Fenrir::SamplerFilter::Bilinear, 4},
          Fenrir::SamplerDescription{Fenrir::SamplerWrap::ClampToEdge, Fenrir::SamplerWrap::Repeat,
                                     Fenrir::SamplerFilter::Nearest, 1}})
    {
        REQUIRE(test.store.SetTexture(material, "baseColourTexture", image, sampler));
        const std::optional<Fenrir::MaterialTexture> texture =
            test.store.TryGetOverrideTexture(material, "baseColourTexture");
        INFO("each sampler reads back with its texture");
        REQUIRE(texture.has_value());
        REQUIRE(texture->image == image);
        REQUIRE(texture->sampler == sampler);
    }
    INFO("a texture set without a sampler takes the default sampler");
    REQUIRE(test.store.SetTexture(material, "baseColourTexture", image));
    REQUIRE(test.store.TryGetTexture(material, "baseColourTexture")->sampler == Fenrir::SamplerDescription{});
}

TEST_CASE("An override the changed shader lacks is kept, logged once and ignored", "[assets][materials]")
{
    AssetsApp test;
    const std::vector<Fenrir::ShaderParameter> before = FixtureParameters();
    const AssetHandle<Fenrir::Shader> shader = AddShader(test, before);
    const AssetHandle<Material> material = test.store.CreateMaterial(shader, AssetOwner::App());
    REQUIRE(test.store.SetFloat(material, "roughness", 0.9f));
    REQUIRE(test.store.SetVector2(material, "offset", Vec2(1.0f, 1.0f)));

    // roughness is gone, and offset becomes a vec3.
    const std::vector<Fenrir::ShaderParameter> after = {
        before[0], {"offset", Type::Vector3, {5.0f, 6.0f, 7.0f}}, before[3], before[4]};
    Fenrir::ShaderData changed;
    changed.bytes = FixtureShaderBytes(after);
    REQUIRE(test.store.Replace(shader, changed));

    INFO("both overrides are kept");
    REQUIRE(test.store.IsSet(material, "roughness"));
    REQUIRE(test.store.TryGetOverrideFloat(material, "roughness") == 0.9f);
    REQUIRE(test.store.TryGetOverrideVector2(material, "offset") == Vec2(1.0f, 1.0f));
    INFO("and ignored: the missing one reads empty, the retyped one reads the new default");
    REQUIRE_FALSE(test.store.TryGetFloat(material, "roughness").has_value());
    REQUIRE(test.store.TryGetVector3(material, "offset") == Vec3(5.0f, 6.0f, 7.0f));
    INFO("each is logged as a warning");
    REQUIRE(test.warnings->size() == 2);

    REQUIRE(test.store.Replace(shader, changed));
    INFO("only once per material");
    REQUIRE(test.warnings->size() == 2);
}

TEST_CASE("Materials need a live shader and a live handle", "[assets][materials]")
{
    AssetsApp test;
    INFO("a material on an empty shader handle is refused and logged");
    REQUIRE(test.store.CreateMaterial(AssetHandle<Fenrir::Shader>{}, AssetOwner::App()).IsEmpty());
    REQUIRE(test.errors->size() == 1);

    const AssetHandle<Material> material = test.store.CreateMaterial(AddFixtureShader(test), AssetOwner::App());
    REQUIRE(test.store.Unload(material));
    INFO("a freed material cannot be set, cloned or reset, and reads empty");
    REQUIRE_FALSE(test.store.SetFloat(material, "roughness", 0.9f));
    REQUIRE_FALSE(test.store.SetBlendMode(material, Fenrir::BlendMode::Masked));
    REQUIRE(test.store.CloneMaterial(material, AssetOwner::App()).IsEmpty());
    REQUIRE_FALSE(test.store.Reset(material, "roughness"));
    REQUIRE_FALSE(test.store.TryGetFloat(material, "roughness").has_value());
    REQUIRE_FALSE(test.store.IsSet(material, "roughness"));
    INFO("the sets and the clone were logged");
    REQUIRE(test.errors->size() == 4);
}
