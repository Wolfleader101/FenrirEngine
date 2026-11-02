#include "DemoMaterials.hpp"

#include <cstddef>
#include <utility>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/ShaderData.hpp"

namespace LightingDemo
{
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
