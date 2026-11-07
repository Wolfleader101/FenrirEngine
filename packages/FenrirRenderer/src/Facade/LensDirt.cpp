#include "FenrirRenderer/LensDirt.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace Fenrir
{
    namespace
    {
        /** @brief A small PCG32 generator: the same numbers from the same seed on every platform and compiler. */
        class Random
        {
          public:
            explicit Random(std::uint32_t seed) : m_state(0x853c49e6748fea9bull + seed)
            {
                Next();
            }

            std::uint32_t Next()
            {
                const std::uint64_t old = m_state;
                m_state = old * 6364136223846793005ull + 1442695040888963407ull;
                const auto shifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
                const auto rotation = static_cast<std::uint32_t>(old >> 59u);
                return (shifted >> rotation) | (shifted << ((32u - rotation) & 31u));
            }

            /** @brief A number in [low, high). */
            float Range(float low, float high)
            {
                return low + (high - low) * static_cast<float>(Next() >> 8u) / static_cast<float>(1u << 24u);
            }

          private:
            std::uint64_t m_state;
        };

        /** @brief Brightness per pixel while the dirt is drawn, added up and clamped to 1 at the end. */
        struct Canvas
        {
            std::uint32_t width;
            std::uint32_t height;
            std::vector<float> values;

            /**
             * @brief Adds a soft ellipse at (x, y) in pixels, `radius` across its long axis and `squash` of that
             * across its short one, turned by `angle`, peaking at `strength` and falling off as a Gaussian.
             */
            void Blob(float x, float y, float radius, float squash, float angle, float strength)
            {
                const float reach = radius * 2.5f;
                const auto x0 = static_cast<int>(std::max(std::floor(x - reach), 0.0f));
                const auto x1 = static_cast<int>(std::min(std::ceil(x + reach), static_cast<float>(width) - 1.0f));
                const auto y0 = static_cast<int>(std::max(std::floor(y - reach), 0.0f));
                const auto y1 = static_cast<int>(std::min(std::ceil(y + reach), static_cast<float>(height) - 1.0f));
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const float across = radius * squash;
                for (int py = y0; py <= y1; ++py)
                {
                    for (int px = x0; px <= x1; ++px)
                    {
                        const float dx = static_cast<float>(px) + 0.5f - x;
                        const float dy = static_cast<float>(py) + 0.5f - y;
                        const float u = (dx * c + dy * s) / radius;
                        const float v = (-dx * s + dy * c) / across;
                        values[static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px)] +=
                            strength * std::exp(-2.0f * (u * u + v * v));
                    }
                }
            }
        };
    } // namespace

    ImageData GenerateLensDirt(std::uint32_t width, std::uint32_t height, std::uint32_t seed)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        Canvas canvas{width, height, std::vector<float>(static_cast<std::size_t>(width) * height, 0.0f)};
        Random random(seed);
        const float w = static_cast<float>(width);
        const float h = static_cast<float>(height);
        const float side = std::min(w, h);
        const float area = w * h;

        // A faint haze over the whole lens, thicker towards one side, as a lens wiped once is.
        const float hazeAngle = random.Range(0.0f, 2.0f * std::numbers::pi_v<float>);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / w - 0.5f;
                const float v = (static_cast<float>(y) + 0.5f) / h - 0.5f;
                const float along = u * std::cos(hazeAngle) + v * std::sin(hazeAngle);
                canvas.values[static_cast<std::size_t>(y) * width + x] = 0.03f + 0.03f * (along + 0.5f);
            }
        }

        // Smudges: wide, soft and faint, some stretched where a finger dragged.
        for (int i = 0; i < 24; ++i)
        {
            canvas.Blob(random.Range(0.0f, w), random.Range(0.0f, h), side * random.Range(0.03f, 0.14f),
                        random.Range(0.35f, 1.0f), random.Range(0.0f, std::numbers::pi_v<float>),
                        random.Range(0.08f, 0.3f));
        }

        // Specks of dust: small and bright, a few per ten thousand pixels.
        const int specks = std::max(static_cast<int>(area / 2500.0f), 8);
        for (int i = 0; i < specks; ++i)
        {
            const float size = random.Range(0.0f, 1.0f);
            canvas.Blob(random.Range(0.0f, w), random.Range(0.0f, h),
                        std::max(side * 0.0015f, 0.6f) + size * size * 2.5f, random.Range(0.6f, 1.0f),
                        random.Range(0.0f, std::numbers::pi_v<float>), random.Range(0.3f, 1.0f));
        }

        // Hairs: a few thin, gently curving streaks, drawn as a run of tiny blobs.
        for (int i = 0; i < 4; ++i)
        {
            float x = random.Range(0.0f, w);
            float y = random.Range(0.0f, h);
            float heading = random.Range(0.0f, 2.0f * std::numbers::pi_v<float>);
            const float bend = random.Range(-2.0f, 2.0f) / side;
            const float length = side * random.Range(0.1f, 0.35f);
            const float strength = random.Range(0.35f, 0.7f);
            const float thickness = std::max(side * 0.0008f, 0.5f);
            for (float travelled = 0.0f; travelled < length; travelled += thickness * 0.5f)
            {
                canvas.Blob(x, y, thickness, 1.0f, 0.0f, strength * 0.35f);
                x += std::cos(heading) * thickness * 0.5f;
                y += std::sin(heading) * thickness * 0.5f;
                heading += bend * thickness * 0.5f;
            }
        }

        ImageData image;
        image.width = width;
        image.height = height;
        image.format = ImageFormat::RGBA8;
        image.colourSpace = ColourSpace::Linear;
        image.pixels.reserve(canvas.values.size() * 4);
        for (const float value : canvas.values)
        {
            const auto grey = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            image.pixels.insert(image.pixels.end(),
                                {std::byte{grey}, std::byte{grey}, std::byte{grey}, std::byte{255}});
        }
        return image;
    }
} // namespace Fenrir
