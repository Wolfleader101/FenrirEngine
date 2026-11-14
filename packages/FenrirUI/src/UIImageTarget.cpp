#include "FenrirUI/UIImageTarget.hpp"

#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirRenderer/ImageTargets2D.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief A render target image of `width` by `height`: flat RGBA8 with no pixels (ADR-0026). */
        ImageData RenderTarget(std::uint32_t width, std::uint32_t height, ColourSpace colourSpace)
        {
            ImageData data;
            data.width = width;
            data.height = height;
            data.colourSpace = colourSpace;
            return data;
        }
    } // namespace

    UIImageTarget::UIImageTarget(App& app, std::uint32_t width, std::uint32_t height, ColourSpace colourSpace)
        : m_store(app.TryGetResource<AssetStore>()), m_width(width), m_height(height), m_colourSpace(colourSpace)
    {
        if (m_store == nullptr)
        {
            app.Logger()->Error(
                "FenrirUI: an image target needs the asset store (Assets::Install), so it draws nothing");
            return;
        }
        m_image = m_store->Add<Image>(RenderTarget(width, height, colourSpace), AssetOwner::App());
    }

    UIImageTarget::~UIImageTarget()
    {
        if (m_store != nullptr && !m_image.IsEmpty())
        {
            m_store->Unload(m_image);
        }
    }

    void UIImageTarget::SetSize(std::uint32_t width, std::uint32_t height)
    {
        if (width == m_width && height == m_height)
        {
            return;
        }
        m_width = width;
        m_height = height;
        if (m_store != nullptr && !m_image.IsEmpty())
        {
            m_store->Replace(m_image, RenderTarget(width, height, m_colourSpace));
        }
    }

    UITargetSize UIImageTarget::GetSize(App&)
    {
        if (m_image.IsEmpty())
        {
            return {};
        }
        return {{static_cast<float>(m_width), static_cast<float>(m_height)}, 1.0f};
    }

    void UIImageTarget::Submit(App& app, DrawList2D list)
    {
        if (ImageTargets2D* targets = app.TryGetResource<ImageTargets2D>(); targets != nullptr && !m_image.IsEmpty())
        {
            targets->Submit(m_image, std::move(list));
        }
    }
} // namespace Fenrir
