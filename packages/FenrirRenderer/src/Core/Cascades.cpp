#include "Core/Cascades.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
    std::vector<float> CascadeSplits(float near, float far, std::uint32_t count, float lambda)
    {
        std::vector<float> splits;
        splits.reserve(std::size_t{count} + 1);
        const float ratio = far / near;
        for (std::uint32_t i = 0; i <= count; ++i)
        {
            const float share = static_cast<float>(i) / static_cast<float>(count);
            const float logarithmic = near * std::pow(ratio, share);
            const float even = near + (far - near) * share;
            splits.push_back(lambda * logarithmic + (1.0f - lambda) * even);
        }
        // Exactly the ends, whatever pow rounds them to.
        splits.front() = near;
        splits.back() = far;
        return splits;
    }

    DepthRange DepthRangeOf(const Math::Mat4& projection)
    {
        // ReversedZPerspective: w = -z, and depth near / -z, so [3][2] is the near plane and there is no far one.
        if (projection[2][3] != 0.0f)
        {
            return {projection[3][2], std::numeric_limits<float>::infinity()};
        }
        // ReversedZOrthographic: depth (far + z) / (far - near).
        const float far = projection[3][2] / projection[2][2];
        return {far - 1.0f / projection[2][2], far};
    }

    CascadeSphere FitCascadeSphere(const Math::Mat4& projection, float near, float far)
    {
        CascadeSphere sphere;
        // The half-diagonal of the frustum's cross-section: per unit of depth for a perspective, in units for an
        // orthographic projection.
        const float halfWidth = 1.0f / projection[0][0];
        const float halfHeight = 1.0f / projection[1][1];
        const float diagonal = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight);
        if (projection[2][3] != 0.0f)
        {
            // Equally far from the near and far rims: (c - n)^2 + (n k)^2 = (f - c)^2 + (f k)^2. Past the far plane
            // (a wide frustum), the far rim's own circle is the smallest.
            const float k2 = diagonal * diagonal;
            sphere.distance = std::min(0.5f * (near + far) * (1.0f + k2), far);
            const float along = far - sphere.distance;
            sphere.radius = std::sqrt(along * along + far * far * k2);
        }
        else
        {
            const float half = 0.5f * (far - near);
            sphere.distance = 0.5f * (near + far);
            sphere.radius = std::sqrt(half * half + diagonal * diagonal);
        }
        // Rounded up, so float noise in the inputs never moves the texel size.
        sphere.radius = std::ceil(sphere.radius * 16.0f) / 16.0f;
        return sphere;
    }

    Math::Mat4 LightRotation(Math::Vec3 direction)
    {
        // The light's view looks along its -Z, so +Z points back at the light; its up is world +Y, or +Z for a light
        // straight up or down.
        const Math::Vec3 back = -direction;
        const Math::Vec3 up =
            std::abs(direction.y) > 0.99f ? Math::Vec3(0.0f, 0.0f, 1.0f) : Math::Vec3(0.0f, 1.0f, 0.0f);
        const Math::Vec3 right = glm::normalize(glm::cross(up, back));
        const Math::Vec3 top = glm::cross(back, right);
        Math::Mat4 rotation(1.0f);
        for (int column = 0; column < 3; ++column)
        {
            rotation[column][0] = right[column];
            rotation[column][1] = top[column];
            rotation[column][2] = back[column];
        }
        return rotation;
    }

    std::vector<Cascade> FitCascades(const Math::Mat4& view, const Math::Mat4& projection, Math::Vec3 lightDirection,
                                     float distance, const CascadeConfig& config)
    {
        const DepthRange range = DepthRangeOf(projection);
        const float far = std::min(distance, range.far);
        const std::uint32_t count = std::clamp(config.count, 1u, MaxCascades);
        if (config.count == 0 || config.mapSize == 0 || !(far > range.near) || range.near <= 0.0f)
        {
            return {};
        }
        const std::vector<float> splits = CascadeSplits(range.near, far, count, config.lambda);
        const Math::Mat4 cameraWorld = glm::inverse(view);
        const Math::Mat4 rotation = LightRotation(lightDirection);
        std::vector<Cascade> cascades;
        cascades.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            // From where the cascade before blends into this one: its last CascadeBlendShare, as the shader reads it
            // (cascade 0's range measured from depth 0).
            float start = splits[i];
            if (i > 0)
            {
                const float before = i >= 2 ? splits[i - 1] : 0.0f;
                start -= CascadeBlendShare * (splits[i] - before);
            }
            const CascadeSphere sphere = FitCascadeSphere(projection, start, splits[i + 1]);
            const Math::Vec3 centre = Math::Vec3(cameraWorld * Math::Vec4(0.0f, 0.0f, -sphere.distance, 1.0f));

            Cascade cascade;
            cascade.split = splits[i + 1];
            cascade.radius = sphere.radius;
            // A texel's margin round the sphere, so it stays inside the map after its centre is snapped.
            const auto mapSize = static_cast<float>(std::max(config.mapSize, 4u));
            cascade.texelSize = 2.0f * sphere.radius / (mapSize - 2.0f);
            // The centre in the light's view space, moved onto whole texels across; along the light it may go
            // anywhere, which moves depths, never texels.
            Math::Vec3 inLight = Math::Vec3(rotation * Math::Vec4(centre, 1.0f));
            inLight.x = std::floor(inLight.x / cascade.texelSize) * cascade.texelSize;
            inLight.y = std::floor(inLight.y / cascade.texelSize) * cascade.texelSize;
            cascade.centre = Math::Vec3(glm::transpose(rotation) * Math::Vec4(inLight, 1.0f));
            // The light's eye behind the sphere, by its radius and `distance` more for casters outside it.
            const float reach = sphere.radius + distance;
            const Math::Vec3 eye(inLight.x, inLight.y, inLight.z + reach);
            Math::Mat4 toEye(1.0f);
            toEye[3] = Math::Vec4(-eye, 1.0f);
            cascade.view = toEye * rotation;
            cascade.projection = ReversedZOrthographic(cascade.texelSize * mapSize, 1.0f, 0.0f, reach + sphere.radius);
            cascade.viewProjection = cascade.projection * cascade.view;
            cascades.push_back(cascade);
        }
        return cascades;
    }

    Math::Mat4 AtlasMatrix(const Cascade& cascade, std::uint32_t index, std::uint32_t count)
    {
        // Clip x in [-1, 1] across its tile, and clip +y up to v 0 at the top row (ADR-0016); depth as drawn.
        const float tiles = static_cast<float>(count);
        Math::Mat4 atlas(1.0f);
        atlas[0][0] = 0.5f / tiles;
        atlas[1][1] = -0.5f;
        atlas[3][0] = (0.5f + static_cast<float>(index)) / tiles;
        atlas[3][1] = 0.5f;
        return atlas * cascade.viewProjection;
    }
} // namespace Fenrir::Render
