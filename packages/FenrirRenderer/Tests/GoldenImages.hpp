#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace RendererTest
{
    /** @brief An 8-bit RGBA image as an sRGB target stores it, four bytes a pixel, top row first. */
    struct Rgba8Image
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba;
    };

    /** @brief The PNG at `path` as 8-bit RGBA; empty if it is missing or not a PNG. */
    [[nodiscard]] std::optional<Rgba8Image> ReadPng(const std::filesystem::path& path);

    /** @brief Writes `image` as a PNG at `path`, making its directory. False if it could not be written. */
    bool WritePng(const std::filesystem::path& path, const Rgba8Image& image);

    /** @brief A colour in CIELAB under D65: lightness 0 to 100, and its green-red and blue-yellow axes. */
    struct Lab
    {
        double l = 0.0;
        double a = 0.0;
        double b = 0.0;
    };

    /** @brief The CIELAB colour of the 8-bit sRGB colour `rgb`, its first three bytes. */
    [[nodiscard]] Lab LabOfSrgb(const std::uint8_t* rgb);

    /**
     * @brief The perceptual difference of two colours, CIEDE2000 (ΔE00; Sharma, Wu and Dalal 2005): about 1 is the
     * smallest difference a viewer sees side by side.
     */
    [[nodiscard]] double DeltaE2000(const Lab& first, const Lab& second);

    /**
     * @brief How far a frame may stray from its golden image (ADR-0049): a pixel differs when its ΔE00 from the
     * golden's is over `deltaE`, and the frame matches while no more than `differingPixels` do, so a driver's
     * rasterisation of an edge or two passes and a changed colour fails.
     */
    struct GoldenTolerance
    {
        double deltaE = 2.0;
        std::size_t differingPixels = 16;
    };

    /** @brief A frame compared with its golden image. */
    struct GoldenComparison
    {
        /** @brief Whether the sizes agree; nothing else is compared when they do not. */
        bool sameSize = false;
        /** @brief How many pixels differ by more than the tolerance's ΔE00. */
        std::size_t differingPixels = 0;
        /** @brief The largest ΔE00 of any pixel. */
        double largestDeltaE = 0.0;
        /** @brief The golden image dimmed to a quarter, with each differing pixel magenta. */
        Rgba8Image difference;

        /** @brief Whether the frame matches its golden image under `tolerance`. */
        [[nodiscard]] bool Matches(const GoldenTolerance& tolerance) const noexcept
        {
            return sameSize && differingPixels <= tolerance.differingPixels;
        }
    };

    /** @brief `actual` compared pixel by pixel with `golden` under `tolerance`. */
    [[nodiscard]] GoldenComparison CompareWithGolden(const Rgba8Image& golden, const Rgba8Image& actual,
                                                     const GoldenTolerance& tolerance = {});

    /** @brief Whether golden images are to be written rather than compared: `FENRIR_UPDATE_GOLDENS` is set to 1. */
    [[nodiscard]] bool UpdatingGoldens();

    /**
     * @brief Requires `actual` to match the golden image `name`.png in `goldens` (ADR-0049). On a mismatch it writes
     * `name`.actual.png and `name`.difference.png into `failures` and fails, saying where. With UpdatingGoldens it
     * writes `actual` as the golden image instead, and says so: the only way goldens change.
     */
    void RequireGolden(std::string_view name, const Rgba8Image& actual, const std::filesystem::path& goldens,
                       const std::filesystem::path& failures, const GoldenTolerance& tolerance = {});
} // namespace RendererTest
