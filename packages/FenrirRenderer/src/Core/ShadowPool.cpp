#include "Core/ShadowPool.hpp"

#include <algorithm>
#include <limits>

namespace Fenrir::Render
{
    ShadowChoice ChooseShadowedLights(std::span<const PunctualLight> lights, std::span<const Math::Vec3> cameras,
                                      std::uint32_t budget, LightKind kind)
    {
        ShadowChoice choice;
        std::vector<float> distance(lights.size(), 0.0f);
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            const PunctualLight& light = lights[i];
            if (light.kind != kind || !light.castShadows)
            {
                continue;
            }
            choice.lights.push_back(i);
            float nearest = cameras.empty() ? 0.0f : std::numeric_limits<float>::infinity();
            for (const Math::Vec3 camera : cameras)
            {
                nearest = std::min(nearest, glm::distance(light.position, camera));
            }
            distance[i] = nearest;
        }
        choice.wanted = choice.lights.size();
        std::ranges::stable_sort(choice.lights,
                                 [&](std::size_t a, std::size_t b) { return distance[a] < distance[b]; });
        choice.lights.resize(std::min<std::size_t>(choice.lights.size(), budget));
        return choice;
    }
} // namespace Fenrir::Render
