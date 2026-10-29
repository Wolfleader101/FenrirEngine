#include "SkinPreparation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>

namespace Fenrir
{
    std::optional<BoneQuad> QuantiseBoneWeights(Math::Vec4 weights)
    {
        std::array<double, 4> values{};
        double sum = 0.0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float weight = weights[static_cast<glm::length_t>(i)];
            if (!std::isfinite(weight) || weight < 0.0f)
            {
                return std::nullopt;
            }
            values[i] = weight;
            sum += weight;
        }
        BoneQuad quantised{0, 0, 0, 0};
        if (sum == 0.0)
        {
            return quantised;
        }

        // Each weight scaled to unorm16 and rounded down; what the floors lose (at most three units) goes back one
        // unit at a time to the largest fractional parts, so the four sum to exactly BoneWeightOne.
        std::array<double, 4> fractions{};
        std::uint32_t total = 0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const double scaled = values[i] / sum * BoneWeightOne;
            const double floor = std::min(std::floor(scaled), double{BoneWeightOne});
            quantised[i] = static_cast<std::uint16_t>(floor);
            fractions[i] = scaled - floor;
            total += quantised[i];
        }
        std::array<std::size_t, 4> order{0, 1, 2, 3};
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return fractions[a] > fractions[b]; });
        for (std::size_t k = 0; total < BoneWeightOne; k = (k + 1) % 4)
        {
            if (quantised[order[k]] < BoneWeightOne)
            {
                ++quantised[order[k]];
                ++total;
            }
        }
        return quantised;
    }

    namespace Detail
    {
        namespace
        {
            void Grow(std::optional<Math::AABB>& box, const Math::Vec3& position)
            {
                if (!box)
                {
                    box = Math::AABB(position, position);
                    return;
                }
                box->min = glm::min(box->min, position);
                box->max = glm::max(box->max, position);
            }
        } // namespace

        std::string PrepareSkin(MeshSkin& skin, std::span<const Math::Vec3> positions,
                                std::vector<std::string>& warnings)
        {
            const std::size_t vertices = positions.size();
            if (skin.joints.size() != vertices)
            {
                return std::format("its skin has {} bone index sets for {} positions", skin.joints.size(), vertices);
            }
            if (skin.weights.size() != vertices)
            {
                return std::format("its skin has {} bone weight sets for {} positions", skin.weights.size(), vertices);
            }

            std::uint32_t highest = 0;
            bool anyJoint = false;
            std::size_t unweighted = 0;
            for (std::size_t v = 0; v < vertices; ++v)
            {
                std::uint32_t sum = 0;
                for (std::size_t slot = 0; slot < 4; ++slot)
                {
                    sum += skin.weights[v][slot];
                    highest = std::max<std::uint32_t>(highest, skin.joints[v][slot]);
                    anyJoint = true;
                }
                if (sum == 0)
                {
                    ++unweighted;
                }
                else if (sum != BoneWeightOne)
                {
                    return std::format("vertex {}'s bone weights sum to {}, not {} (or 0): make them with "
                                       "QuantiseBoneWeights",
                                       v, sum, BoneWeightOne);
                }
            }

            if (skin.boneCount == 0)
            {
                skin.boneCount = anyJoint ? highest + 1 : 0;
            }
            else if (anyJoint && highest >= skin.boneCount)
            {
                return std::format("its skin names bone {}, but its bone count is {}", highest, skin.boneCount);
            }

            if (unweighted > 0)
            {
                warnings.push_back(std::format("{} of its {} vertices have no bone weight, so they ride rigidly in "
                                               "skeleton bind space",
                                               unweighted, vertices));
            }

            skin.boneBounds.assign(skin.boneCount, std::nullopt);
            skin.unweightedBounds.reset();
            for (std::size_t v = 0; v < vertices; ++v)
            {
                bool weighted = false;
                for (std::size_t slot = 0; slot < 4; ++slot)
                {
                    if (skin.weights[v][slot] > 0)
                    {
                        Grow(skin.boneBounds[skin.joints[v][slot]], positions[v]);
                        weighted = true;
                    }
                }
                if (!weighted)
                {
                    Grow(skin.unweightedBounds, positions[v]);
                }
            }
            return {};
        }
    } // namespace Detail
} // namespace Fenrir
