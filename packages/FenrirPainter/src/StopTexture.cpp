#include "FenrirPainter/StopTexture.hpp"

#include <algorithm>
#include <cstring>

#include "Colours.hpp"

namespace Fenrir
{
    namespace
    {
        bool SameStops(std::span<const GradientStop> a, std::span<const GradientStop> b)
        {
            return std::ranges::equal(a, b, [](const GradientStop& l, const GradientStop& r) {
                return l.offset == r.offset && l.colour.r == r.colour.r && l.colour.g == r.colour.g &&
                       l.colour.b == r.colour.b && l.colour.a == r.colour.a;
            });
        }

        /** @brief The row's texels: texel `x` is the gradient at offset `x / (Width - 1)`, so both ends are stops. */
        std::vector<std::byte> Texels(std::span<const GradientStop> stops)
        {
            std::vector<std::byte> texels(std::size_t{StopTexture::Width} * 4);
            for (std::uint32_t x = 0; x < StopTexture::Width; ++x)
            {
                const float offset = static_cast<float>(x) / static_cast<float>(StopTexture::Width - 1);
                Math::Vec4 value = PainterDetail::SrgbPremultiplied(stops.front().colour);
                if (offset >= stops.back().offset)
                {
                    value = PainterDetail::SrgbPremultiplied(stops.back().colour);
                }
                else
                {
                    for (std::size_t i = 0; i + 1 < stops.size(); ++i)
                    {
                        const GradientStop& from = stops[i];
                        const GradientStop& to = stops[i + 1];
                        if (offset >= from.offset && offset < to.offset)
                        {
                            const float s = (offset - from.offset) / (to.offset - from.offset);
                            value = PainterDetail::SrgbPremultiplied(from.colour) * (1.0f - s) +
                                    PainterDetail::SrgbPremultiplied(to.colour) * s;
                            break;
                        }
                    }
                }
                const std::uint32_t packed = PainterDetail::Pack(value);
                std::memcpy(texels.data() + std::size_t{x} * 4, &packed, 4);
            }
            return texels;
        }
    } // namespace

    ImageData StopTexture::Blank(std::uint32_t rows)
    {
        ImageData image;
        image.width = Width;
        image.height = std::max(rows, 1u);
        image.format = ImageFormat::RGBA8;
        image.colourSpace = ColourSpace::Linear;
        image.pixels.assign(std::size_t{image.width} * image.height * 4, std::byte{0});
        return image;
    }

    StopTexture::StopTexture(AssetHandle<Image> image, std::uint32_t rows)
        : m_image(image), m_capacity(std::max(rows, 1u))
    {
    }

    std::optional<float> StopTexture::AddRow(std::span<const GradientStop> stops)
    {
        if (stops.empty())
        {
            return std::nullopt;
        }
        std::size_t index = 0;
        while (index < m_rows.size() && !SameStops(m_rows[index].stops, stops))
        {
            ++index;
        }
        if (index == m_rows.size())
        {
            if (m_rows.size() >= m_capacity)
            {
                return std::nullopt;
            }
            m_rows.push_back({{stops.begin(), stops.end()}, Texels(stops)});
        }
        return (static_cast<float>(index) + 0.5f) / static_cast<float>(m_capacity);
    }

    ImageData StopTexture::TakeImageData()
    {
        ImageData image = Blank(m_capacity);
        for (std::size_t row = 0; row < m_rows.size(); ++row)
        {
            std::memcpy(image.pixels.data() + row * Width * 4, m_rows[row].texels.data(), m_rows[row].texels.size());
        }
        m_rows.clear();
        return image;
    }
} // namespace Fenrir
