#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <variant>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief How a material's pixels combine with what is behind them. It picks the render stage and whether depth is
     * written (ADR-0024).
     */
    enum class BlendMode : std::uint8_t
    {
        /** @brief Drawn over what is behind, writing depth. */
        Opaque,
        /** @brief Opaque, but a pixel whose alpha is below the material's `alphaCutoff` is discarded. */
        Masked,
        /** @brief Mixed with what is behind by its alpha, without writing depth. */
        AlphaBlend,
        /** @brief Added to what is behind, without writing depth. */
        Additive,
    };

    /** @brief What a texture does with coordinates outside [0, 1]. */
    enum class SamplerWrap : std::uint8_t
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
    };

    /** @brief How a texture is filtered between its pixels and its mips. */
    enum class SamplerFilter : std::uint8_t
    {
        /** @brief The nearest pixel of the nearest mip. */
        Nearest,
        /** @brief Linear between pixels, from the nearest mip. */
        Bilinear,
        /** @brief Linear between pixels and between mips. */
        Trilinear,
    };

    /**
     * @brief How a texture parameter is sampled (ADR-0024): its wrap, filter and anisotropy. The renderer core shares
     * one sampler between parameters with equal descriptions. Default made, it is the spec's default sampler: repeat,
     * trilinear, anisotropy 8.
     */
    struct SamplerDescription
    {
        SamplerWrap wrap = SamplerWrap::Repeat;
        SamplerFilter filter = SamplerFilter::Trilinear;
        /** @brief The most anisotropic filtering taken, 1 for none. The renderer clamps it to what the device has. */
        std::uint32_t anisotropy = 8;

        friend bool operator==(const SamplerDescription&, const SamplerDescription&) = default;
    };

    /**
     * @brief A texture parameter's value: an image and the sampler it is read with. An empty image stands for the
     * shader's default texture (`white`, `black` or `normal`, from GetShaderParameters), which the renderer binds.
     */
    struct MaterialTexture
    {
        AssetHandle<Image> image;
        SamplerDescription sampler;

        friend bool operator==(const MaterialTexture&, const MaterialTexture&) = default;
    };

    /**
     * @brief One parameter override's value. The alternatives are in ShaderParameterType's order, so `index()` is the
     * parameter type the value fits. A colour is a `vec4`, linear.
     */
    using MaterialValue = std::variant<float, Math::Vec2, Math::Vec3, Math::Vec4, MaterialTexture>;

    /**
     * @brief A material as plain CPU-side data (ADR-0021, ADR-0024): a shader, a blend mode, whether it is
     * double-sided, and its parameter overrides. It stores only the parameters it sets: every other parameter takes its
     * shader's parameter default, so it follows a changed shader.
     *
     * Edit a stored material through the AssetStore's material calls (SetFloat, Reset...), which check names and
     * types against the shader and bump the revision; AssetStore::TryGetFloat and the rest read the effective value.
     */
    struct MaterialData
    {
        /** @brief The shader the material draws with, whose parameters it overrides. */
        AssetHandle<Shader> shader;
        BlendMode blendMode = BlendMode::Opaque;
        /** @brief The alpha below which a `Masked` material discards a pixel; unused by the other blend modes. */
        float alphaCutoff = 0.5f;
        /** @brief Whether both faces are drawn: culling is off. */
        bool doubleSided = false;
        /**
         * @brief The parameter overrides, by parameter name. An override the shader lacks, or whose type no longer
         * matches after the shader changed, is kept and ignored.
         */
        std::map<std::string, MaterialValue, std::less<>> overrides;
    };
} // namespace Fenrir
