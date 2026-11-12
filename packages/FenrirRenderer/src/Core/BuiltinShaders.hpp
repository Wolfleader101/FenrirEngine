#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief One of the renderer's built-in shader programs, embedded in it as a shader container (ADR-0018). */
    enum class BuiltinProgram : std::uint8_t
    {
        /** @brief `builtin:unlit`: base colour times vertex colour. */
        Unlit,
        /** @brief `builtin:lit`: metallic-roughness GGX through `Fenrir/Lighting.glsl`. */
        Lit,
        /** @brief `builtin:fallback`: the fallback material's flat magenta. */
        Fallback,
        /** @brief `builtin:background`: the Background pass at depth 0, a colour or (its CUBEMAP variant) a cubemap. */
        Background,
        /** @brief `builtin:output`: the Output pass, SceneColor clamped into the backbuffer. */
        Output,
        /** @brief `builtin:debug-lines`: debug lines, expanded to screen-space quads of their thickness. */
        DebugLines,
        /** @brief `builtin:debug-triangles`: solid debug shapes, alpha-blended. */
        DebugTriangles,
        /** @brief `builtin:debug-text`: debug text from the debug font's signed-distance-field atlas. */
        DebugText,
        /** @brief `builtin:2d`: 2D draw lists on the ImGui vertex layout, textured and vertex-coloured (ADR-0027). */
        Draw2D,
        /** @brief `builtin:panorama`: a panorama sky's image drawn into one face and mip of its cube texture. */
        Panorama,
        /** @brief `builtin:sky-irradiance`: the Sky cube projected onto L2 spherical harmonics. */
        SkyIrradiance,
        /** @brief `builtin:sky-specular`: the Sky cube prefiltered with GGX into one face and mip of its specular cube.
         */
        SkySpecular,
        /** @brief `builtin:brdf-lut`: the split sum's BRDF lookup table. */
        BrdfLut,
        /** @brief `builtin:bloom-downsample`: a bloom level from the one above, Jimenez's 13-tap filter. */
        BloomDownsample,
        /** @brief `builtin:bloom-upsample`: a bloom level tent-filtered up over the one above, or over SceneColor. */
        BloomUpsample,
        /** @brief `builtin:lens-flare`: a lens flare's ghosts and halo from a bloom level (ADR-0059). */
        LensFlare,
        /** @brief `builtin:depth-prepass`: the depth prepass's fragment stage, an item's view-space normal. */
        DepthPrepass,
        /** @brief `builtin:gtao`: a view's GTAO from its depth prepass, the horizon search and, DENOISE, the blur. */
        Gtao,
        /**
         * @brief `builtin:skinning`: a compute program, each skinned mesh's vertices skinned once a frame for every
         * pass to draw rigidly (ADR-0050).
         */
        Skinning,
        /** @brief `builtin:shadow`: the shadow passes' fragment stage, depth only, cutting Masked items out. */
        Shadow,
        /**
         * @brief `builtin:light-clusters`: a compute program, each view's point, spot and rect lights listed in its
         * clusters (ADR-0049).
         */
        LightClusters,
        /**
         * @brief `builtin:cull`: a view's cull pass, each candidate kept written into its indirection and counted into
         * its run's indirect command (ADR-0057).
         */
        Cull,
        /** @brief `builtin:hi-z`: a level of a view's Hi-Z pyramid, COPY level 0 from SceneDepth (ADR-0056). */
        HiZ,
        /**
         * @brief `builtin:scene-history`: a level of a view's history colour, the base variant level 0 from SceneColor
         * and DOWNSAMPLE each level below (ADR-0056).
         */
        SceneHistory,
        /** @brief `builtin:reflection-trace`: a view's mirror rays marched through its Hi-Z pyramid (ADR-0056). */
        ReflectionTrace,
    };

    /** @brief A built-in program's `builtin:` name and its embedded container's bytes. */
    struct BuiltinShader
    {
        BuiltinProgram program = BuiltinProgram::Unlit;
        std::string_view name;
        std::span<const std::byte> bytes;
    };

    /** @brief Every built-in program, in BuiltinProgram's order, for the facade to register in the asset store. */
    [[nodiscard]] std::span<const BuiltinShader> BuiltinShaders();

    /** @brief One built-in program's embedded container. */
    [[nodiscard]] const BuiltinShader& GetBuiltinShader(BuiltinProgram program);

    /** @brief One built-in program's container, read from its embedded bytes; empty, and logged, if it cannot be. */
    [[nodiscard]] ShaderContainer ReadBuiltin(BuiltinProgram program, ILogger& logger);

    /** @brief A built-in pipeline of `desc`; when the device cannot make it, logs why under `name` and returns empty.
     */
    [[nodiscard]] Graphics::PipelineHandle MakeBuiltinPipeline(Graphics::GraphicsDevice& device, ILogger& logger,
                                                               const Graphics::PipelineDesc& desc,
                                                               std::string_view name);
} // namespace Fenrir::Render
