// The asset store's material edits (ADR-0024, ticket 41): a material stores only its parameter overrides, and reads
// fall back to its shader's parameter defaults.
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "AssetStoreState.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    namespace
    {
        // The GLSL word for each parameter type, in ShaderParameterType's order.
        constexpr std::array<std::string_view, 5> typeNames = {"float", "vec2", "vec3", "vec4", "texture"};

        std::string_view TypeName(ShaderParameterType type)
        {
            return typeNames[static_cast<std::size_t>(type)];
        }

        // MaterialValue's alternatives are in ShaderParameterType's order.
        ShaderParameterType TypeOf(const MaterialValue& value)
        {
            return static_cast<ShaderParameterType>(value.index());
        }

        const ShaderParameter* FindParameter(std::span<const ShaderParameter> parameters, std::string_view name)
        {
            const auto found =
                std::find_if(parameters.begin(), parameters.end(),
                             [name](const ShaderParameter& parameter) { return parameter.name == name; });
            return found != parameters.end() ? &*found : nullptr;
        }

        // A parameter default as a value of its type. A texture's default is no image: the renderer binds the
        // shader's default texture, read with the default sampler.
        MaterialValue DefaultValue(const ShaderParameter& parameter)
        {
            const std::array<float, 4>& v = parameter.value;
            if (parameter.type == ShaderParameterType::Float)
            {
                return v[0];
            }
            if (parameter.type == ShaderParameterType::Vector2)
            {
                return Math::Vec2(v[0], v[1]);
            }
            if (parameter.type == ShaderParameterType::Vector3)
            {
                return Math::Vec3(v[0], v[1], v[2]);
            }
            if (parameter.type == ShaderParameterType::Vector4)
            {
                return Math::Vec4(v[0], v[1], v[2], v[3]);
            }
            return MaterialTexture{};
        }

        template <typename V>
        std::optional<V> As(const std::optional<MaterialValue>& value)
        {
            if (!value)
            {
                return std::nullopt;
            }
            return std::get<V>(*value);
        }

        std::optional<Colour> AsColour(const std::optional<MaterialValue>& value)
        {
            const std::optional<Math::Vec4> vector = As<Math::Vec4>(value);
            if (!vector)
            {
                return std::nullopt;
            }
            return Colour{vector->x, vector->y, vector->z, vector->w};
        }

        Detail::AssetSlot SlotOf(AssetHandle<Material> material)
        {
            return {material.index, material.generation};
        }
    } // namespace

    AssetHandle<Material> AssetStore::CreateMaterial(AssetHandle<Shader> shader, AssetOwner owner)
    {
        if (!IsValid(shader))
        {
            Refuse("material", "create", "its shader handle is empty or stale");
            return {};
        }
        MaterialData data;
        data.shader = shader;
        return Add<Material>(std::move(data), owner);
    }

    AssetHandle<Material> AssetStore::CloneMaterial(AssetHandle<Material> material, AssetOwner owner)
    {
        const MaterialData* source = TryGet(material);
        if (source == nullptr)
        {
            Refuse("material", "clone", "its handle is empty or stale");
            return {};
        }
        return Add<Material>(*source, owner);
    }

    bool AssetStore::SetFloat(AssetHandle<Material> material, std::string_view name, float value)
    {
        return SetParameter(material, name, value);
    }

    bool AssetStore::SetVector2(AssetHandle<Material> material, std::string_view name, Math::Vec2 value)
    {
        return SetParameter(material, name, value);
    }

    bool AssetStore::SetVector3(AssetHandle<Material> material, std::string_view name, Math::Vec3 value)
    {
        return SetParameter(material, name, value);
    }

    bool AssetStore::SetVector4(AssetHandle<Material> material, std::string_view name, Math::Vec4 value)
    {
        return SetParameter(material, name, value);
    }

    bool AssetStore::SetColour(AssetHandle<Material> material, std::string_view name, Colour value)
    {
        return SetParameter(material, name, Math::Vec4(value.r, value.g, value.b, value.a));
    }

    bool AssetStore::SetTexture(AssetHandle<Material> material, std::string_view name, AssetHandle<Image> image,
                                SamplerDescription sampler)
    {
        return SetParameter(material, name, MaterialTexture{image, sampler});
    }

    bool AssetStore::SetBlendMode(AssetHandle<Material> material, BlendMode blendMode)
    {
        MaterialData* data = EditMaterial(material, "set the blend mode of");
        if (data != nullptr)
        {
            data->blendMode = blendMode;
        }
        return data != nullptr;
    }

    bool AssetStore::SetAlphaCutoff(AssetHandle<Material> material, float alphaCutoff)
    {
        MaterialData* data = EditMaterial(material, "set the alpha cutoff of");
        if (data != nullptr)
        {
            data->alphaCutoff = alphaCutoff;
        }
        return data != nullptr;
    }

    bool AssetStore::SetDoubleSided(AssetHandle<Material> material, bool doubleSided)
    {
        MaterialData* data = EditMaterial(material, "set the double-sidedness of");
        if (data != nullptr)
        {
            data->doubleSided = doubleSided;
        }
        return data != nullptr;
    }

    std::optional<float> AssetStore::TryGetFloat(AssetHandle<Material> material, std::string_view name) const
    {
        return As<float>(EffectiveValue(material, name, ShaderParameterType::Float));
    }

    std::optional<Math::Vec2> AssetStore::TryGetVector2(AssetHandle<Material> material, std::string_view name) const
    {
        return As<Math::Vec2>(EffectiveValue(material, name, ShaderParameterType::Vector2));
    }

    std::optional<Math::Vec3> AssetStore::TryGetVector3(AssetHandle<Material> material, std::string_view name) const
    {
        return As<Math::Vec3>(EffectiveValue(material, name, ShaderParameterType::Vector3));
    }

    std::optional<Math::Vec4> AssetStore::TryGetVector4(AssetHandle<Material> material, std::string_view name) const
    {
        return As<Math::Vec4>(EffectiveValue(material, name, ShaderParameterType::Vector4));
    }

    std::optional<Colour> AssetStore::TryGetColour(AssetHandle<Material> material, std::string_view name) const
    {
        return AsColour(EffectiveValue(material, name, ShaderParameterType::Vector4));
    }

    std::optional<MaterialTexture> AssetStore::TryGetTexture(AssetHandle<Material> material,
                                                             std::string_view name) const
    {
        return As<MaterialTexture>(EffectiveValue(material, name, ShaderParameterType::Texture));
    }

    std::optional<float> AssetStore::TryGetOverrideFloat(AssetHandle<Material> material, std::string_view name) const
    {
        return As<float>(OverrideValue(material, name, ShaderParameterType::Float));
    }

    std::optional<Math::Vec2> AssetStore::TryGetOverrideVector2(AssetHandle<Material> material,
                                                                std::string_view name) const
    {
        return As<Math::Vec2>(OverrideValue(material, name, ShaderParameterType::Vector2));
    }

    std::optional<Math::Vec3> AssetStore::TryGetOverrideVector3(AssetHandle<Material> material,
                                                                std::string_view name) const
    {
        return As<Math::Vec3>(OverrideValue(material, name, ShaderParameterType::Vector3));
    }

    std::optional<Math::Vec4> AssetStore::TryGetOverrideVector4(AssetHandle<Material> material,
                                                                std::string_view name) const
    {
        return As<Math::Vec4>(OverrideValue(material, name, ShaderParameterType::Vector4));
    }

    std::optional<Colour> AssetStore::TryGetOverrideColour(AssetHandle<Material> material, std::string_view name) const
    {
        return AsColour(OverrideValue(material, name, ShaderParameterType::Vector4));
    }

    std::optional<MaterialTexture> AssetStore::TryGetOverrideTexture(AssetHandle<Material> material,
                                                                     std::string_view name) const
    {
        return As<MaterialTexture>(OverrideValue(material, name, ShaderParameterType::Texture));
    }

    bool AssetStore::IsSet(AssetHandle<Material> material, std::string_view name) const
    {
        const MaterialData* data = TryGet(material);
        return data != nullptr && data->overrides.contains(name);
    }

    bool AssetStore::Reset(AssetHandle<Material> material, std::string_view name)
    {
        Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Material, SlotOf(material));
        if (record == nullptr)
        {
            return false;
        }
        auto& overrides = static_cast<MaterialData*>(record->data.get())->overrides;
        const auto found = overrides.find(name);
        if (found == overrides.end())
        {
            return false;
        }
        overrides.erase(found);
        ++record->revision;
        return true;
    }

    MaterialData* AssetStore::EditMaterial(AssetHandle<Material> material, std::string_view action)
    {
        Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Material, SlotOf(material));
        if (record == nullptr)
        {
            m_state->logger->Error("Could not {} a material: its handle is empty or stale", action);
            return nullptr;
        }
        ++record->revision;
        return static_cast<MaterialData*>(record->data.get());
    }

    bool AssetStore::SetParameter(AssetHandle<Material> material, std::string_view name, MaterialValue value)
    {
        Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Material, SlotOf(material));
        if (record == nullptr)
        {
            m_state->logger->Error("Could not set parameter '{}' of a material: its handle is empty or stale", name);
            return false;
        }
        auto& data = *static_cast<MaterialData*>(record->data.get());
        const ShaderParameter* parameter = FindParameter(GetShaderParameters(data.shader), name);
        std::string problem;
        if (parameter == nullptr)
        {
            problem =
                std::format("Material parameter '{}' is not one of its shader's parameters; the set is ignored", name);
        }
        else if (parameter->type != TypeOf(value))
        {
            problem = std::format("Material parameter '{}' is a {}, not a {}; the set is ignored", name,
                                  TypeName(parameter->type), TypeName(TypeOf(value)));
        }
        if (!problem.empty())
        {
            // Once per material and problem, so a set repeated every frame does not flood the log.
            if (record->logged.insert(problem).second)
            {
                m_state->logger->Error("{}", problem);
            }
            return false;
        }
        data.overrides.insert_or_assign(std::string(name), std::move(value));
        ++record->revision;
        return true;
    }

    std::optional<MaterialValue> AssetStore::EffectiveValue(AssetHandle<Material> material, std::string_view name,
                                                            ShaderParameterType type) const
    {
        const MaterialData* data = TryGet(material);
        if (data == nullptr)
        {
            return std::nullopt;
        }
        const ShaderParameter* parameter = FindParameter(GetShaderParameters(data->shader), name);
        if (parameter == nullptr || parameter->type != type)
        {
            return std::nullopt;
        }
        // An override of another type (kept from before the shader changed) is ignored, so the default shows.
        if (const auto found = data->overrides.find(name);
            found != data->overrides.end() && TypeOf(found->second) == type)
        {
            return found->second;
        }
        return DefaultValue(*parameter);
    }

    std::optional<MaterialValue> AssetStore::OverrideValue(AssetHandle<Material> material, std::string_view name,
                                                           ShaderParameterType type) const
    {
        const MaterialData* data = TryGet(material);
        if (data == nullptr)
        {
            return std::nullopt;
        }
        const auto found = data->overrides.find(name);
        if (found == data->overrides.end() || TypeOf(found->second) != type)
        {
            return std::nullopt;
        }
        return found->second;
    }

    void AssetStore::WarnOverridesShaderLacks(AssetHandle<Shader> shader)
    {
        const std::span<const ShaderParameter> parameters = GetShaderParameters(shader);
        for (Detail::AssetRecord& record :
             m_state->tables[static_cast<std::size_t>(Detail::AssetKind::Material)].records)
        {
            const auto* data = static_cast<const MaterialData*>(record.data.get());
            if (record.revision == 0 || data == nullptr || data->shader != shader)
            {
                continue;
            }
            for (const auto& [name, value] : data->overrides)
            {
                const ShaderParameter* parameter = FindParameter(parameters, name);
                if (parameter != nullptr && parameter->type == TypeOf(value))
                {
                    continue;
                }
                std::string problem =
                    parameter == nullptr
                        ? std::format("Material parameter '{}' is no longer one of its shader's parameters; the "
                                      "override is kept and ignored",
                                      name)
                        : std::format("Material parameter '{}' is now a {}, not a {}; the override is kept and ignored",
                                      name, TypeName(parameter->type), TypeName(TypeOf(value)));
                if (record.logged.insert(problem).second)
                {
                    m_state->logger->Warn("{}", problem);
                }
            }
        }
    }
} // namespace Fenrir
