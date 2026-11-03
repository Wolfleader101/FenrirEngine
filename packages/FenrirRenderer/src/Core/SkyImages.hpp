#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "Core/RenderGraph.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief A Sky image's cube texture, as SkyImages::Cube answers it; an empty texture when there is none to show.
     */
    struct SkyCube
    {
        /** @brief The cube texture, with every mip level. */
        Graphics::TextureHandle texture;
        /** @brief Its description: its faces' side and mip levels. */
        Graphics::TextureDesc desc;
    };

    /**
     * @brief The Sky images (ADR-0047), kept by key and revision as the facade prepares and releases them: each a cube
     * texture that the Background pass draws and the sky's light is filtered from.
     *
     * - **A cubemap** is uploaded as a cube texture with every mip level of each face made at upload.
     * - **A panorama** is uploaded with every mip level, then drawn by `Prepare`-stage `Panorama` passes into an
     * RGBA16F cube texture with faces half as wide as the image is high and every mip level, one pass a face and level.
     * Its passes are added each frame until they have been recorded; the frame after, the image's own texture and the
     *   passes' blocks and groups are freed.
     */
    class SkyImages
    {
      public:
        SkyImages(Graphics::GraphicsDevice& device, ILogger& logger);
        ~SkyImages();

        SkyImages(const SkyImages&) = delete;
        SkyImages& operator=(const SkyImages&) = delete;
        SkyImages(SkyImages&&) = delete;
        SkyImages& operator=(SkyImages&&) = delete;

        /** @brief Whether the cubemap `key` is prepared at `revision`. */
        [[nodiscard]] bool IsCubemapCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief Uploads the cubemap `key` at `revision`, a cubemap image with pixels, replacing any before. */
        void PrepareCubemap(AssetKey key, std::uint32_t revision, const ImageData& image);
        /** @brief Frees the cubemap `key`'s texture, if it is prepared. */
        void ReleaseCubemap(AssetKey key);
        /** @brief The prepared cubemaps' keys. */
        [[nodiscard]] std::vector<AssetKey> CubemapKeys() const;

        /** @brief Whether the panorama `key` is prepared at `revision`. */
        [[nodiscard]] bool IsPanoramaCurrent(AssetKey key, std::uint32_t revision) const;
        /**
         * @brief Uploads the panorama `key` at `revision`, a flat image with pixels twice as wide as high, replacing
         * any before, and makes its cube texture for the next AddPasses' passes to draw.
         */
        void PreparePanorama(AssetKey key, std::uint32_t revision, const ImageData& image);
        /** @brief Frees the panorama `key`'s textures and passes, if it is prepared. */
        void ReleasePanorama(AssetKey key);
        /** @brief The prepared panoramas' keys. */
        [[nodiscard]] std::vector<AssetKey> PanoramaKeys() const;

        /**
         * @brief Adds the `Panorama` passes of each panorama not yet drawn into its cube texture, and frees what those
         * drawn in an earlier frame no longer need.
         */
        void AddPasses(RenderGraph& graph);

        /**
         * @brief The cube texture of the cubemap or panorama `key`. A panorama's is ready by the time this frame's
         * later `Prepare` passes run; empty when neither is prepared, or the panorama's passes cannot run
         * (builtin:panorama did not load), so its cube texture holds nothing.
         */
        [[nodiscard]] SkyCube Cube(AssetKey key) const;

      private:
        /** @brief A prepared cubemap: its cube texture. */
        struct CubemapEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle texture;
            Graphics::TextureDesc desc;
        };

        /**
         * @brief A prepared panorama: its cube texture, and until its passes have run, the image's own texture and
         * those passes' blocks and groups.
         */
        struct PanoramaEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle cube;
            /** @brief The cube texture's description: its faces' side and mip levels. */
            Graphics::TextureDesc cubeDesc;
            Graphics::TextureHandle source;
            /** @brief Each pass's block (face, half a texel, the panorama's mip), one uniform slot a pass. */
            Graphics::BufferHandle params;
            /** @brief Each pass's group, face by face within each mip level, from mip 0. */
            std::vector<Graphics::BindingGroupHandle> passGroups;
            /** @brief Whether its passes have been recorded, so its source can go. */
            bool converted = false;
        };

        /** @brief Frees a panorama's image texture and its passes' blocks and groups, if it still has them. */
        void FreePasses(PanoramaEntry& entry);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        std::map<AssetKey, CubemapEntry> m_cubemaps;
        std::map<AssetKey, PanoramaEntry> m_panoramas;
        /** @brief `builtin:panorama`, the Panorama passes' program, and its pipeline; empty if it failed. */
        ShaderContainer m_program;
        Graphics::PipelineHandle m_pipeline;
        /** @brief Set 2 of the Panorama passes: their block and the panorama. */
        Graphics::BindingGroupLayoutHandle m_layout;
        /** @brief Wraps round a panorama's sides and clamps at its top and bottom. */
        Graphics::SamplerHandle m_sampler;
    };
} // namespace Fenrir::Render
