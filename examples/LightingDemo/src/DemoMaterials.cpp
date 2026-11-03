#include "DemoMaterials.hpp"

#include <cstddef>
#include <utility>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

namespace LightingDemo
{
    Fenrir::AssetHandle<Fenrir::Material> LitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                      Fenrir::Colour colour, float metallic, float roughness,
                                                      Fenrir::Colour emissive, float strength)
    {
        const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", scene).handle, scene);
        store.SetColour(material, "baseColour", colour);
        store.SetFloat(material, "metallic", metallic);
        store.SetFloat(material, "roughness", roughness);
        store.SetColour(material, "emissive", emissive);
        store.SetFloat(material, "emissiveStrength", strength);
        return material;
    }

    Fenrir::AssetHandle<Fenrir::Material> UnlitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                        Fenrir::Colour colour)
    {
        const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", scene).handle, scene);
        store.SetColour(material, "baseColour", colour);
        return material;
    }

    Fenrir::Entity Box(Fenrir::EntityList& entities, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                       Fenrir::AssetHandle<Fenrir::Material> material, Fenrir::Math::Vec3 position,
                       Fenrir::Math::Vec3 scale)
    {
        Fenrir::Entity entity = entities.CreateEntity();
        Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.scale = scale;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube, material});
        return entity;
    }

    Fenrir::AssetHandle<Fenrir::Material> EmbeddedMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                           std::span<const std::uint8_t> bytes)
    {
        Fenrir::ShaderData shader;
        const auto raw = std::as_bytes(bytes);
        shader.bytes.assign(raw.begin(), raw.end());
        return store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(shader), scene), scene);
    }

    Fenrir::AssetHandle<Fenrir::Image> AsLinear(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                Fenrir::AssetHandle<Fenrir::Image> image)
    {
        const Fenrir::ImageData* data = store.TryGet(image);
        if (data == nullptr)
        {
            return {};
        }
        Fenrir::ImageData linear = *data;
        linear.colourSpace = Fenrir::ColourSpace::Linear;
        return store.Add<Fenrir::Image>(std::move(linear), scene);
    }
} // namespace LightingDemo
