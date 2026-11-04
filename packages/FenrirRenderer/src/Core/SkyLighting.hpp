#pragma once

#include <cstdint>
#include <vector>

#include "Core/RenderGraph.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /**
     * @brief What the scene's Sky lights with this frame (ADR-0047): its cube texture, a Sky cubemap's or a panorama's,
     * ready to sample by the time the `Prepare` stage's later passes run; or, with no cube, one colour (a ColourSky).
     * A new image revision makes a new cube texture, so the texture names the image and its revision.
     */
    struct SkySource
    {
        /** @brief The cube texture, with every mip level; empty for a colour. */
        Graphics::TextureHandle cube;
        /** @brief Its description: its faces' side and mip levels. */
        Graphics::TextureDesc cubeDesc;
        /** @brief The colour, linear, when `cube` is empty. */
        Math::Vec3 colour{0.0f};

        friend bool operator==(const SkySource&, const SkySource&) = default;
    };

    /** @brief What set 0 binds of the sky's light, for `Fenrir/Frame.glsl`. */
    struct SkyLightingTextures
    {
        /** @brief Nine texels along one row: the L2 spherical harmonics of the irradiance divided by pi, RGBA16F. */
        Graphics::TextureHandle irradiance;
        /** @brief The prefiltered specular cube, RGBA16F, one roughness step a mip level. */
        Graphics::TextureHandle specular;
        /** @brief The split sum's BRDF lookup table, RG16F. */
        Graphics::TextureHandle brdfLut;
        /** @brief The specular cube's highest mip level, which holds roughness 1 (frame.sky.w). */
        float specularMaxLevel = 0.0f;
        /** @brief What all three are sampled with: clamped at the edges (a cube's at its faces), trilinear. */
        Graphics::SamplerHandle sampler;
    };

    /**
     * @brief The scene's Sky filtered into image-based lighting on the GPU (ADR-0047), in render graph passes of the
     * `Prepare` stage, kept across frames.
     *
     * - **BrdfLut**, once, in the first frame: the split sum's 128 x 128 RG16F lookup table.
     * - **SkyIrradiance** and **SkySpecular**, when the sky's source changes (a new cube texture, or a colour sky for a
     *   cube one): the cube projected onto L2 spherical harmonics, from its mip of at most 32 x 32 texels a face, and
     *   prefiltered with GGX into a cube of faces at most 128 texels wide with every mip level, one pass a face and
     *   level. Nothing else refilters: the sky's intensity scales the light in the shader, and its rotation turns the
     *   lookup direction, as the Background pass turns it.
     * - **A colour sky** runs no pass: its coefficients are written from the CPU (ConstantIrradiance) and its specular
     *   cube is 1 x 1, the colour.
     *
     * Before the first frame the sky is black. A cube's passes are added again each frame until they have been
     * recorded. The old textures are destroyed when the source changes; the device defers that past the frames in
     * flight.
     */
    class SkyLighting
    {
      public:
        SkyLighting(Graphics::GraphicsDevice& device, ILogger& logger);
        ~SkyLighting();

        SkyLighting(const SkyLighting&) = delete;
        SkyLighting& operator=(const SkyLighting&) = delete;
        SkyLighting(SkyLighting&&) = delete;
        SkyLighting& operator=(SkyLighting&&) = delete;

        /** @brief Adds the passes this frame needs for `sky`: the lookup table's in the first frame, and filtering. */
        void AddPasses(RenderGraph& graph, const SkySource& sky);

        /**
         * @brief What set 0 binds this frame: the textures of the source last given to AddPasses, which its passes
         * (in the `Prepare` stage, before any view) fill this frame if they have not already.
         */
        [[nodiscard]] const SkyLightingTextures& Textures() const noexcept
        {
            return m_current.textures;
        }

      private:
        /** @brief The textures and passes' blocks and groups of one filtered source. */
        struct Filtered
        {
            SkySource source;
            SkyLightingTextures textures;
            Graphics::TextureDesc irradianceDesc;
            Graphics::TextureDesc specularDesc;
            Graphics::BufferHandle params;
            /** @brief The SkyIrradiance pass's group, then each SkySpecular pass's, face by face within each level. */
            std::vector<Graphics::BindingGroupHandle> groups;
            /** @brief Whether its passes have been recorded (a colour has none). */
            bool ready = false;
        };

        /** @brief Makes the textures and passes' groups of `sky`; a colour's are written at once and ready. */
        void Filter(const SkySource& sky);
        void Destroy(Filtered& filtered);
        void AddFilterPasses(RenderGraph& graph);
        [[nodiscard]] Graphics::PipelineHandle MakePipeline(const ShaderContainer& container,
                                                            Graphics::BindingGroupLayoutHandle layout,
                                                            Graphics::TextureFormat format);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;

        ShaderContainer m_irradianceProgram;
        ShaderContainer m_specularProgram;
        ShaderContainer m_brdfProgram;
        /** @brief Set 2 of both filtering programs: their block at binding 0 and the sky's cube at binding 1. */
        Graphics::BindingGroupLayoutHandle m_passLayout;
        Graphics::PipelineHandle m_irradiancePipeline;
        Graphics::PipelineHandle m_specularPipeline;
        Graphics::PipelineHandle m_brdfPipeline;
        /** @brief Clamped at the edges (a cube's at its faces), trilinear across the mips. */
        Graphics::SamplerHandle m_sampler;
        /** @brief Whether a cube sky lit as its fallback colour because a filtering pipeline is missing was logged. */
        bool m_loggedFallback = false;

        Graphics::TextureHandle m_brdfLut;
        Graphics::TextureDesc m_brdfDesc;
        /** @brief Whether the lookup table's pass has been recorded. */
        bool m_brdfMade = false;

        /** @brief The source last asked for, filtered or being filtered. */
        Filtered m_current;
    };
} // namespace Fenrir::Render
