#include "FenrirText/GlyphAtlas.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "FenrirAssets/ImageData.hpp"

namespace Fenrir
{
    namespace Detail
    {
        // One step of a page's skyline: from x, `width` pixels wide, everything below y is taken.
        struct SkylineNode
        {
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            std::uint32_t width = 0;
        };

        struct GlyphAtlasState
        {
            AssetStore* store = nullptr;
            GlyphAtlasConfig config;
            std::vector<AssetHandle<Image>> pages;
            // The newest page's side and skyline; older pages are full.
            std::uint32_t side = 0;
            std::vector<SkylineNode> skyline;
        };
    } // namespace Detail

    namespace
    {
        struct Fit
        {
            std::size_t node = 0;
            std::uint32_t x = 0;
            std::uint32_t y = 0;
        };

        // The lowest place a `width` by `height` rectangle fits on the skyline, leftmost among equals (bottom-left fit,
        // as Jylänki's skyline packer and fontstash's).
        std::optional<Fit> FindFit(const std::vector<Detail::SkylineNode>& skyline, std::uint32_t side,
                                   std::uint32_t width, std::uint32_t height)
        {
            std::optional<Fit> best;
            for (std::size_t i = 0; i < skyline.size(); ++i)
            {
                const std::uint32_t x = skyline[i].x;
                if (x + width > side)
                {
                    break;
                }
                // The rectangle rests on the highest step it spans.
                std::uint32_t y = 0;
                std::uint32_t spanned = 0;
                for (std::size_t j = i; spanned < width; ++j)
                {
                    y = std::max(y, skyline[j].y);
                    spanned += skyline[j].width;
                }
                if (y + height > side)
                {
                    continue;
                }
                if (!best || y < best->y)
                {
                    best = Fit{i, x, y};
                }
            }
            return best;
        }

        // Raises the skyline under a rectangle placed at `fit`.
        void Place(std::vector<Detail::SkylineNode>& skyline, const Fit& fit, std::uint32_t width, std::uint32_t height)
        {
            skyline.insert(skyline.begin() + static_cast<std::ptrdiff_t>(fit.node), {fit.x, fit.y + height, width});
            // Trim the steps the new one now covers.
            const std::uint32_t right = fit.x + width;
            std::size_t next = fit.node + 1;
            while (next < skyline.size() && skyline[next].x < right)
            {
                const std::uint32_t end = skyline[next].x + skyline[next].width;
                if (end <= right)
                {
                    skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(next));
                    continue;
                }
                skyline[next].width = end - right;
                skyline[next].x = right;
                break;
            }
            // Merge neighbours of one height.
            for (std::size_t i = 0; i + 1 < skyline.size();)
            {
                if (skyline[i].y == skyline[i + 1].y)
                {
                    skyline[i].width += skyline[i + 1].width;
                    skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(i + 1));
                }
                else
                {
                    ++i;
                }
            }
        }

        ImageData BlankPage(std::uint32_t side)
        {
            ImageData page;
            page.width = side;
            page.height = side;
            page.format = ImageFormat::R8;
            page.colourSpace = ColourSpace::Linear;
            page.pixels.assign(std::size_t{side} * side, std::byte{0});
            return page;
        }
    } // namespace

    GlyphAtlas::GlyphAtlas(AssetStore& store, GlyphAtlasConfig config)
        : m_state(std::make_unique<Detail::GlyphAtlasState>())
    {
        m_state->store = &store;
        config.initialSize = std::max(config.initialSize, 1u);
        config.maxSize = std::max(config.maxSize, config.initialSize);
        m_state->config = config;
    }

    GlyphAtlas::~GlyphAtlas()
    {
        UnloadPages();
    }

    GlyphAtlas::GlyphAtlas(GlyphAtlas&&) noexcept = default;

    GlyphAtlas& GlyphAtlas::operator=(GlyphAtlas&& other) noexcept
    {
        if (this != &other)
        {
            // The pages this atlas held end with it, as they would in its destructor.
            UnloadPages();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    void GlyphAtlas::UnloadPages()
    {
        if (m_state)
        {
            for (const AssetHandle<Image> page : m_state->pages)
            {
                m_state->store->Unload(page);
            }
        }
    }

    std::optional<AtlasRegion> GlyphAtlas::Add(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::byte> coverage)
    {
        Detail::GlyphAtlasState& state = *m_state;
        const std::uint32_t padding = state.config.padding;
        if (width == 0 || height == 0 || coverage.size() != std::size_t{width} * height ||
            width + padding > state.config.maxSize || height + padding > state.config.maxSize)
        {
            return std::nullopt;
        }
        const std::uint32_t paddedWidth = width + padding;
        const std::uint32_t paddedHeight = height + padding;

        const auto openPage = [&](std::uint32_t side) {
            state.pages.push_back(state.store->Add<Image>(BlankPage(side), AssetOwner::App()));
            state.side = side;
            state.skyline = {{0, 0, side}};
        };
        if (state.pages.empty())
        {
            openPage(state.config.initialSize);
        }

        std::optional<Fit> fit = FindFit(state.skyline, state.side, paddedWidth, paddedHeight);
        while (!fit)
        {
            if (state.side >= state.config.maxSize)
            {
                openPage(state.config.maxSize);
            }
            else
            {
                // Double the newest page, keeping its rows; the new columns on the right are empty from the top.
                const std::uint32_t side = std::min(state.side * 2, state.config.maxSize);
                const AssetHandle<Image> page = state.pages.back();
                const ImageData& old = *state.store->TryGet(page);
                ImageData grown = BlankPage(side);
                for (std::uint32_t row = 0; row < old.height; ++row)
                {
                    std::memcpy(grown.pixels.data() + std::size_t{row} * side,
                                old.pixels.data() + std::size_t{row} * old.width, old.width);
                }
                state.skyline.push_back({state.side, 0, side - state.side});
                state.side = side;
                state.store->Replace(page, std::move(grown));
            }
            fit = FindFit(state.skyline, state.side, paddedWidth, paddedHeight);
        }
        Place(state.skyline, *fit, paddedWidth, paddedHeight);

        const AtlasRegion placed{static_cast<std::uint32_t>(state.pages.size() - 1), {fit->x, fit->y, width, height}};
        state.store->UpdateImage(state.pages.back(), placed.region, coverage);
        return placed;
    }

    std::span<const AssetHandle<Image>> GlyphAtlas::Pages() const noexcept
    {
        return m_state->pages;
    }
} // namespace Fenrir
