#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief How the renderer takes the image 2D targets' submissions. Defined in FenrirRenderer's sources. */
        struct ImageTargetSubmissions;
    } // namespace Detail

    /**
     * @brief The image 2D target (ADR-0060): an app resource Renderer::Install adds, through which 2D draw lists are
     * drawn into image assets, as the Overlay draws them over a window. Submit lists to an image at any time before
     * `Render`; the renderer draws them that frame and clears them after it. The image is then any image asset: a
     * material's texture parameter, a 2D command's texture slot.
     *
     * **The image** is a render target image: flat RGBA8 with a size and no pixels, encoded sRGB or linear as its
     * colour space says. Its texture is made the first time it is drawn or sampled and made again when it is replaced.
     * A frame that submits to it clears it to transparent, then draws its lists in submission order and each list's
     * commands in order; a frame that submits nothing leaves it as it was. A submission to an empty or stale handle, or
     * to an image that is not a render target, is dropped and logged once per image.
     *
     * **The target:** local coordinates are the image's pixels from its top-left, Y down, a content scale of 1, with no
     * depth and no culling; a scissor is rounded outwards to whole pixels and clamped to the image, as on the window.
     * Blending is linear and an sRGB image encodes on write; the `Gamma` blend space is allowed only on an sRGB image.
     * Its premultiplied alpha is kept, so a painter's image composites without dark fringes.
     *
     * **Order:** images draw in the order of their first submission each frame, before the window overlay and before
     * any view whose materials sample them. A list sampling an image drawn earlier in the frame reads what that list
     * drew; a list sampling its own image binds the slot's default texture there, logged once; one sampling an image
     * drawn later reads what that image held before. Submit may be called from any system, parallel ones included.
     */
    class ImageTargets2D
    {
      public:
        ImageTargets2D() = default;
        ImageTargets2D(const ImageTargets2D&) = delete;
        ImageTargets2D& operator=(const ImageTargets2D&) = delete;
        ImageTargets2D(ImageTargets2D&&) = delete;
        ImageTargets2D& operator=(ImageTargets2D&&) = delete;
        ~ImageTargets2D() = default;

        /** @brief Draws `list` into `image` in the next `Render`, after the lists submitted to it before. */
        void Submit(AssetHandle<Image> image, DrawList2D list)
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_submissions.push_back({image, std::move(list)});
        }

      private:
        friend struct Detail::ImageTargetSubmissions;

        /** @brief One Submit call's arguments. */
        struct Submission
        {
            AssetHandle<Image> image;
            DrawList2D list;
        };

        std::mutex m_mutex;
        std::vector<Submission> m_submissions;
    };
} // namespace Fenrir
