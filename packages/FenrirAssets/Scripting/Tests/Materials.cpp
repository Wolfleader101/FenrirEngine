// Material edits from C# (spec: "Scripting: the extensions", Fenrir.Assets; "FenrirAssets", material edits; seam 7:
// material reads matching C++): the same edits made from C# and from C++ on the fixture shader give the same set
// results, TryGet, TryGetOverride, IsSet and Reset results and revisions; Clone copies overrides only.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/MaterialData.hpp"

using namespace Fenrir;
using namespace AssetsScriptingTests;
using Math::Vec2;
using Math::Vec3;
using Math::Vec4;

namespace
{
    // The names Describe reads: the fixture shader's five parameters and one it lacks.
    constexpr const char* DescribedNames = "tint,roughness,offset,emissive,baseColourTexture,missing";

    // The fixture's edit sequence, made from C++: each result, "1" or "0", in order (MaterialCalls.Edit).
    std::string EditFromCpp(AssetStore& store, AssetHandle<Material> material, AssetHandle<Image> image)
    {
        const SamplerDescription sampler{SamplerWrap::ClampToEdge, SamplerWrap::MirroredRepeat, SamplerFilter::Nearest,
                                         1};
        const bool results[] = {
            store.SetFloat(material, "roughness", 0.25f),
            store.SetColour(material, "tint", Colour{0.5f, 0.25f, 1.0f, 1.0f}),
            store.SetVector2(material, "offset", Vec2(4.0f, 5.0f)),
            store.SetVector3(material, "emissive", Vec3(1.0f, 2.0f, 3.0f)),
            store.SetTexture(material, "baseColourTexture", image, sampler),
            store.SetFloat(material, "tint", 1.0f),
            store.SetFloat(material, "missing", 1.0f),
            store.SetVector4(material, "tint", Vec4(0.5f, 0.25f, 1.0f, 0.75f)),
            store.SetBlendMode(material, BlendMode::Masked),
            store.SetAlphaCutoff(material, 0.25f),
            store.SetDoubleSided(material, true),
            store.Reset(material, "emissive"),
            store.Reset(material, "emissive"),
        };
        std::string text;
        for (const bool result : results)
        {
            text += std::string(text.empty() ? "" : ",") + (result ? "1" : "0");
        }
        return text;
    }

    std::string Numbers(std::initializer_list<float> values)
    {
        std::string text;
        for (const float value : values)
        {
            text += (text.empty() ? "" : ",") + std::format("{}", value);
        }
        return text;
    }

    std::string Shown(const std::optional<float>& value)
    {
        return value ? Numbers({*value}) : "-";
    }
    std::string Shown(const std::optional<Vec2>& value)
    {
        return value ? Numbers({value->x, value->y}) : "-";
    }
    std::string Shown(const std::optional<Vec3>& value)
    {
        return value ? Numbers({value->x, value->y, value->z}) : "-";
    }
    std::string Shown(const std::optional<Vec4>& value)
    {
        return value ? Numbers({value->x, value->y, value->z, value->w}) : "-";
    }
    std::string Shown(const std::optional<Colour>& value)
    {
        return value ? Numbers({value->r, value->g, value->b, value->a}) : "-";
    }
    std::string Shown(const std::optional<MaterialTexture>& value)
    {
        if (!value)
        {
            return "-";
        }
        return std::format("{}:{}|{}|{}|{}|{}", value->image.index, value->image.generation,
                           static_cast<int>(value->sampler.wrapU), static_cast<int>(value->sampler.wrapV),
                           static_cast<int>(value->sampler.filter), value->sampler.anisotropy);
    }

    // Every read of every described name, from C++, in MaterialCalls.Describe's format.
    std::string DescribeFromCpp(const AssetStore& store, AssetHandle<Material> material)
    {
        std::string text;
        std::stringstream names(DescribedNames);
        for (std::string name; std::getline(names, name, ',');)
        {
            text += name + ": set=" + (store.IsSet(material, name) ? "1" : "0");
            text += " float=" + Shown(store.TryGetFloat(material, name)) + "/" +
                    Shown(store.TryGetOverrideFloat(material, name));
            text += " vec2=" + Shown(store.TryGetVector2(material, name)) + "/" +
                    Shown(store.TryGetOverrideVector2(material, name));
            text += " vec3=" + Shown(store.TryGetVector3(material, name)) + "/" +
                    Shown(store.TryGetOverrideVector3(material, name));
            text += " vec4=" + Shown(store.TryGetVector4(material, name)) + "/" +
                    Shown(store.TryGetOverrideVector4(material, name));
            text += " colour=" + Shown(store.TryGetColour(material, name)) + "/" +
                    Shown(store.TryGetOverrideColour(material, name));
            text += " texture=" + Shown(store.TryGetTexture(material, name)) + "/" +
                    Shown(store.TryGetOverrideTexture(material, name));
            text += "\n";
        }
        return text;
    }

    std::string DescribeFromCSharp(AssetsScriptingApp& test, AssetHandle<Material> material)
    {
        return test.Text(MaterialCallsType, "Describe", BitsOf(material), std::string_view(DescribedNames));
    }

    // A one-pixel image a texture parameter can name.
    AssetHandle<Image> AddPixel(AssetStore& store)
    {
        ImageData image;
        image.width = 1;
        image.height = 1;
        image.pixels.assign(4, std::byte{0xff});
        return store.Add<Image>(std::move(image), AssetOwner::App());
    }
} // namespace

TEST_CASE("A new material made from C# reads its shader's defaults as one made from C++ does", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const AssetHandle<Material> fromCpp = test.Store().CreateMaterial(test.shader, AssetOwner::App());
    const auto fromCSharp =
        HandleFrom<Material>(test.Call<std::uint64_t>(MaterialCallsType, "Create", BitsOf(test.shader)));
    {
        INFO("C# did not make a material on the fixture shader");
        REQUIRE(test.Store().IsValid(fromCSharp));
        REQUIRE(test.Store().TryGet(fromCSharp)->shader == test.shader);
    }
    const std::string expected = DescribeFromCpp(test.Store(), fromCpp);
    {
        INFO("C++ does not read the fixture's defaults: roughness 0.5, tint as a vec4 and a colour, the texture's "
             "default image");
        REQUIRE(expected.find("roughness: set=0 float=0.5/- vec2=-/-") != std::string::npos);
        REQUIRE(expected.find("vec4=1,0.5,0.25,1/- colour=1,0.5,0.25,1/-") != std::string::npos);
        REQUIRE(expected.find("baseColourTexture: set=0 float=-/- vec2=-/- vec3=-/- vec4=-/- colour=-/- "
                              "texture=0:0|0|0|2|8/-") != std::string::npos);
    }
    INFO("C# reads a new material otherwise than C++ does:\n" << DescribeFromCSharp(test, fromCSharp));
    REQUIRE(DescribeFromCSharp(test, fromCSharp) == expected);
}

TEST_CASE("The same edits from C# and C++ give the same results, reads, resets and revisions", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const AssetHandle<Image> pixel = AddPixel(test.Store());
    const AssetHandle<Material> fromCpp = test.Store().CreateMaterial(test.shader, AssetOwner::App());
    const AssetHandle<Material> fromCSharp = test.Store().CreateMaterial(test.shader, AssetOwner::App());

    // Every set takes but the float set on the vec4 and on a name the shader lacks; the first reset removes an
    // override and the second finds none.
    const std::string expectedResults = "1,1,1,1,1,0,0,1,1,1,1,1,0";
    {
        INFO("The edits from C++ did not give the expected results");
        REQUIRE(EditFromCpp(test.Store(), fromCpp, pixel) == expectedResults);
    }
    {
        INFO("The edits from C# did not give the results C++'s did");
        REQUIRE(test.Text(MaterialCallsType, "Edit", BitsOf(fromCSharp), BitsOf(pixel)) == expectedResults);
    }
    {
        INFO("The revisions differ, or are not 1 plus the ten edits that changed something");
        REQUIRE(test.Store().Revision(fromCpp) == 11);
        REQUIRE(test.Store().Revision(fromCSharp) == 11);
    }
    {
        const MaterialData* cpp = test.Store().TryGet(fromCpp);
        const MaterialData* csharp = test.Store().TryGet(fromCSharp);
        INFO("The blend mode, alpha cutoff or double-sidedness set from C# is not what C++ set");
        REQUIRE(csharp->blendMode == BlendMode::Masked);
        REQUIRE(csharp->blendMode == cpp->blendMode);
        REQUIRE_FALSE(csharp->alphaCutoff < cpp->alphaCutoff);
        REQUIRE_FALSE(csharp->alphaCutoff > cpp->alphaCutoff);
        REQUIRE(csharp->doubleSided == cpp->doubleSided);
        REQUIRE(csharp->doubleSided);
    }

    const std::string expected = DescribeFromCpp(test.Store(), fromCpp);
    {
        INFO("C++ does not read its own edits back: roughness 0.25 set, tint the vec4 set last, emissive reset to the "
             "default, the texture with its sampler");
        REQUIRE(expected.find("roughness: set=1 float=0.25/0.25") != std::string::npos);
        REQUIRE(expected.find("tint: set=1 float=-/- vec2=-/- vec3=-/- vec4=0.5,0.25,1,0.75/0.5,0.25,1,0.75") !=
                std::string::npos);
        REQUIRE(expected.find("emissive: set=0 float=-/- vec2=-/- vec3=0,0,0/-") != std::string::npos);
        REQUIRE(expected.find(std::format("texture={}:{}|2|1|0|1/", pixel.index, pixel.generation)) !=
                std::string::npos);
        REQUIRE(expected.find("missing: set=0 float=-/- vec2=-/- vec3=-/- vec4=-/- colour=-/- texture=-/-") !=
                std::string::npos);
    }
    INFO("C# reads its edited material otherwise than C++ reads the same edits:\n"
         << DescribeFromCSharp(test, fromCSharp));
    REQUIRE(DescribeFromCSharp(test, fromCSharp) == expected);
    REQUIRE(DescribeFromCSharp(test, fromCpp) == expected);
}

TEST_CASE("Clone from C# copies a material's overrides and settings, not its resolved defaults", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const AssetHandle<Image> pixel = AddPixel(test.Store());
    const AssetHandle<Material> source = test.Store().CreateMaterial(test.shader, AssetOwner::App());
    REQUIRE(EditFromCpp(test.Store(), source, pixel) == "1,1,1,1,1,0,0,1,1,1,1,1,0");

    const auto clone = HandleFrom<Material>(test.Call<std::uint64_t>(MaterialCallsType, "Clone", BitsOf(source)));
    {
        INFO("C# did not clone the material");
        REQUIRE(test.Store().IsValid(clone));
        REQUIRE(clone != source);
    }
    const MaterialData* copied = test.Store().TryGet(clone);
    const MaterialData* original = test.Store().TryGet(source);
    {
        INFO("The clone does not hold exactly the source's four overrides, with its shader and settings");
        REQUIRE(copied->overrides.size() == 4);
        REQUIRE(copied->overrides == original->overrides);
        REQUIRE(copied->shader == original->shader);
        REQUIRE(copied->blendMode == original->blendMode);
        REQUIRE_FALSE(copied->alphaCutoff < original->alphaCutoff);
        REQUIRE_FALSE(copied->alphaCutoff > original->alphaCutoff);
        REQUIRE(copied->doubleSided == original->doubleSided);
    }
    {
        INFO("The clone's unset emissive is an override, not the shader's default it reads");
        REQUIRE_FALSE(test.Store().IsSet(clone, "emissive"));
        REQUIRE(test.Store().TryGetVector3(clone, "emissive") == Vec3(0.0f));
    }
    INFO("C# reads the clone otherwise than C++ reads the source");
    REQUIRE(DescribeFromCSharp(test, clone) == DescribeFromCpp(test.Store(), source));
}

TEST_CASE("A sampler and a texture value made new in C# hold C++'s defaults", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const SamplerDescription defaults;
    INFO("new SamplerDescription() is not repeat, repeat, trilinear, anisotropy 8, or new MaterialTexture() not the "
         "empty image with it");
    REQUIRE(test.Text(MaterialCallsType, "DefaultSampler") ==
            std::format("{},{},{},{}", static_cast<int>(defaults.wrapU), static_cast<int>(defaults.wrapV),
                        static_cast<int>(defaults.filter), defaults.anisotropy));
    REQUIRE(test.Call<bool>(MaterialCallsType, "DefaultTextureUsesDefaultSampler"));
}
