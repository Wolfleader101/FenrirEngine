#pragma once

// How FenrirAssets's types look to C#: each mapping names its mirror in the Fenrir.Assets contract
// (Managed/Fenrir.Assets), and is checked against it whenever a component, resource or event that holds it is, so a
// drift fails the load and names the field. Another package's extension whose components or resources hold asset
// handles (FenrirRenderer's MeshRenderer, Camera and Sky) includes this header, so its mappings can name them as
// fields.

#include <Iris/Iris.hpp>

#include <string_view>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"

namespace Fenrir
{
    /**
     * @brief C#'s `Fenrir.Assets.ShaderParameter`: a ShaderParameter without its name, as plain data, which is what
     * `Shaders.GetParameter` fills (the name goes to a span of its own).
     */
    struct ScriptShaderParameter
    {
        ShaderParameterType type = ShaderParameterType::Float;
        /** @brief A number parameter's default: one value per component, the rest zero. Unused for a texture. */
        Math::Vec4 value{0.0f};
        /** @brief A texture parameter's default. Unused for a number. */
        DefaultTexture texture = DefaultTexture::White;
    };

    namespace Detail
    {
        // The full name .NET gives `Fenrir.Assets.AssetHandle<T>` for the marker type `marker`: the generic argument is
        // named with the contract's assembly, whose version (Fenrir.Assets.csproj's <Version>) is part of it.
#define FENRIR_ASSET_HANDLE_TYPE_NAME(marker)            \
    "Fenrir.Assets.AssetHandle`1[[Fenrir.Assets." marker \
    ", Fenrir.Assets, Version=0.1.0.0, Culture=neutral, PublicKeyToken=null]]"
        inline constexpr std::string_view MeshHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Mesh");
        inline constexpr std::string_view ImageHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Image");
        inline constexpr std::string_view MaterialHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Material");
        inline constexpr std::string_view ShaderHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Shader");
        inline constexpr std::string_view ModelHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Model");
#undef FENRIR_ASSET_HANDLE_TYPE_NAME
    } // namespace Detail
} // namespace Fenrir

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Mesh>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Mesh>;
    static constexpr std::string_view typeName = Fenrir::Detail::MeshHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Image>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Image>;
    static constexpr std::string_view typeName = Fenrir::Detail::ImageHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Material>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Material>;
    static constexpr std::string_view typeName = Fenrir::Detail::MaterialHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Shader>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Shader>;
    static constexpr std::string_view typeName = Fenrir::Detail::ShaderHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Model>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Model>;
    static constexpr std::string_view typeName = Fenrir::Detail::ModelHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::EnumMapping<Fenrir::BlendMode>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.BlendMode";
};

template <>
struct Iris::EnumMapping<Fenrir::SamplerWrap>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.SamplerWrap";
};

template <>
struct Iris::EnumMapping<Fenrir::SamplerFilter>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.SamplerFilter";
};

template <>
struct Iris::EnumMapping<Fenrir::ShaderParameterType>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.ShaderParameterType";
};

template <>
struct Iris::EnumMapping<Fenrir::DefaultTexture>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.DefaultTexture";
};

template <>
struct Iris::StructMapping<Fenrir::SamplerDescription>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.SamplerDescription";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::SamplerDescription, wrapU, "WrapU"),
                           IRIS_FIELD_AS(Fenrir::SamplerDescription, wrapV, "WrapV"),
                           IRIS_FIELD_AS(Fenrir::SamplerDescription, filter, "Filter"),
                           IRIS_FIELD_AS(Fenrir::SamplerDescription, anisotropy, "Anisotropy"));
};

template <>
struct Iris::StructMapping<Fenrir::MaterialTexture>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.MaterialTexture";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MaterialTexture, image, "Image"),
                                                      IRIS_FIELD_AS(Fenrir::MaterialTexture, sampler, "Sampler"));
};

template <>
struct Iris::StructMapping<Fenrir::ScriptShaderParameter>
{
    static constexpr std::string_view typeName = "Fenrir.Assets.ShaderParameter";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ScriptShaderParameter, type, "Type"),
                                                      IRIS_FIELD_AS(Fenrir::ScriptShaderParameter, value, "Value"),
                                                      IRIS_FIELD_AS(Fenrir::ScriptShaderParameter, texture, "Texture"));
};
