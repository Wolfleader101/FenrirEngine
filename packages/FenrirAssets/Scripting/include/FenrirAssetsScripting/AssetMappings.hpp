#pragma once

// How FenrirAssets's handles look to C#: AssetHandle<T> is a bound type (every binding has its own handle type), so the
// generated binding (api/FenrirAssets.binding.toml) records its layout and these mappings, written by hand, name its
// mirror in the Fenrir.Assets contract (Managed/Fenrir.Assets/Handles.cs) with the full generic name .NET gives it.
// They are checked against it whenever a type that holds a handle is, so a drift fails the load and names the field.
// Every other assets mapping is generated, into Generated/FenrirAssetsMappings.hpp, which includes this header; another
// package's binding whose types hold asset handles (FenrirRenderer's) reaches both through that one. A native takes a
// handle through a pointer to it and gives one through an output pointer, as these mappings' types (ADR-0044).

#include <Iris/Iris.hpp>

#include <string_view>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"

namespace Fenrir
{
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
        inline constexpr std::string_view SkeletonHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Skeleton");
        inline constexpr std::string_view ClipHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("Clip");
        inline constexpr std::string_view BoneMaskHandleTypeName = FENRIR_ASSET_HANDLE_TYPE_NAME("BoneMask");
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
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Skeleton>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Skeleton>;
    static constexpr std::string_view typeName = Fenrir::Detail::SkeletonHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::Clip>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::Clip>;
    static constexpr std::string_view typeName = Fenrir::Detail::ClipHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};

template <>
struct Iris::StructMapping<Fenrir::AssetHandle<Fenrir::BoneMask>>
{
    using Handle = Fenrir::AssetHandle<Fenrir::BoneMask>;
    static constexpr std::string_view typeName = Fenrir::Detail::BoneMaskHandleTypeName;
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Handle, index, "Index"), IRIS_FIELD_AS(Handle, generation, "Generation"));
};
