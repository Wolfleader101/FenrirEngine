#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirUI/UITarget.hpp"

namespace Fenrir
{
    class App;
    class AssetStore;

    /**
     * @brief A UI root's image target (ADR-0060, ADR-0027's image target): an `Image` asset the UI owns, at a size the
     * game sets, that the root draws into each frame through the renderer's ImageTargets2D. Give it to a root as
     * UIRootConfig::target; the image is then any image asset, such as a material's texture.
     *
     * The image is a render target image (flat RGBA8 with no pixels), encoded in `colourSpace`, and it holds the root's
     * premultiplied colour, transparent where nothing was drawn. Its logical units are its pixels, at a content scale
     * of 1, so a root on the `Height1080` scale rule spans 1080 UI units of the image's height. A root drawn here
     * blends linear by default (ADR-0065), and takes no window input: its elements see only what code sends them.
     *
     * Made with an app that has an asset store, and ended while it is there: the image is added on construction and
     * unloaded on destruction. Without an asset store it has no image and draws nothing. With no renderer, the lists
     * go nowhere.
     */
    class UIImageTarget final : public UITarget
    {
      public:
        /** @brief An image of `width` by `height` pixels, encoded in `colourSpace`, in `app`'s asset store. */
        UIImageTarget(App& app, std::uint32_t width, std::uint32_t height, ColourSpace colourSpace = ColourSpace::Srgb);
        UIImageTarget(const UIImageTarget&) = delete;
        UIImageTarget& operator=(const UIImageTarget&) = delete;
        UIImageTarget(UIImageTarget&&) = delete;
        UIImageTarget& operator=(UIImageTarget&&) = delete;
        /** @brief Unloads the image. */
        ~UIImageTarget() override;

        /** @brief The image the root draws into; empty without an asset store. */
        [[nodiscard]] AssetHandle<Image> GetImage() const noexcept
        {
            return m_image;
        }

        /** @brief The image's width in pixels. */
        [[nodiscard]] std::uint32_t GetWidth() const noexcept
        {
            return m_width;
        }

        /** @brief The image's height in pixels. */
        [[nodiscard]] std::uint32_t GetHeight() const noexcept
        {
            return m_height;
        }

        /**
         * @brief Makes the image `width` by `height` pixels, replacing its data under the same handle, so a material
         * sampling it keeps it; the root lays out at the new size from its next frame. A zero size draws nothing.
         */
        void SetSize(std::uint32_t width, std::uint32_t height);

        /** @brief The image's size in pixels, at a content scale of 1; zero without an image. */
        [[nodiscard]] UITargetSize GetSize(App& app) override;

        /** @brief Submits `list` to the image through the renderer's ImageTargets2D, when the app has it. */
        void Submit(App& app, DrawList2D list) override;

        /** @brief `Linear`: lit materials read the image, so it blends as the 3D frame does (ADR-0065). */
        [[nodiscard]] BlendSpace2D GetDefaultBlendSpace() const noexcept override
        {
            return BlendSpace2D::Linear;
        }

        /** @brief False: the image is not on the window, so its root's input comes from code. */
        [[nodiscard]] bool TakesWindowInput() const noexcept override
        {
            return false;
        }

      private:
        AssetStore* m_store = nullptr;
        AssetHandle<Image> m_image;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        ColourSpace m_colourSpace = ColourSpace::Srgb;
    };
} // namespace Fenrir
