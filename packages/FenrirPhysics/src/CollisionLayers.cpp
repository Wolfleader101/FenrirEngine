#include "FenrirPhysics/CollisionLayers.hpp"

#include <algorithm>
#include <bit>
#include <string>

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    namespace
    {
        constexpr std::size_t layerCount = 32;
    }

    CollisionLayers::CollisionLayers(std::span<const CollisionLayer> layers, ILogger& logger)
    {
        if (layers.size() > layerCount)
        {
            logger.Warn("PhysicsConfig names " + std::to_string(layers.size()) + " collision layers; only the first " +
                        std::to_string(layerCount) + " are used");
            layers = layers.first(layerCount);
        }
        m_layers.assign(layers.begin(), layers.end());
        for (auto layer = m_layers.begin(); layer != m_layers.end(); ++layer)
        {
            std::string& name = layer->name;
            const auto first = std::find_if(m_layers.begin(), layer,
                                            [&name](const CollisionLayer& other) { return other.name == name; });
            if (!name.empty() && first != layer)
            {
                const std::string index = std::to_string(layer - m_layers.begin());
                logger.Warn("PhysicsConfig names collision layers " + std::to_string(first - m_layers.begin()) +
                            " and " + index + " both \"" + name + "\"; layer " + index + " is left unnamed");
                name.clear();
            }
        }
    }

    std::uint32_t CollisionLayers::Bit(std::string_view name) const
    {
        if (name.empty())
        {
            return 0;
        }
        const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                     [name](const CollisionLayer& layer) { return layer.name == name; });
        return it == m_layers.end() ? 0u : 1u << (it - m_layers.begin());
    }

    CollisionFilter CollisionLayers::Filter(std::string_view name) const
    {
        const std::uint32_t bit = Bit(name);
        if (bit == 0)
        {
            return CollisionFilter{.layers = 0, .mask = 0};
        }
        return CollisionFilter{.layers = bit, .mask = m_layers[static_cast<std::size_t>(std::countr_zero(bit))].mask};
    }

    std::string_view CollisionLayers::Name(int index) const
    {
        if (index < 0 || static_cast<std::size_t>(index) >= m_layers.size())
        {
            return {};
        }
        return m_layers[static_cast<std::size_t>(index)].name;
    }
} // namespace Fenrir
