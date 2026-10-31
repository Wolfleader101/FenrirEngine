// Golden images (ADR-0049): PNG files through stb, and frames compared with them by CIEDE2000.
#include "GoldenImages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace RendererTest
{
    namespace
    {
        double Radians(double degrees)
        {
            return degrees * std::numbers::pi / 180.0;
        }

        double Degrees(double radians)
        {
            return radians * 180.0 / std::numbers::pi;
        }

        /** @brief The hue angle of (a, b) in degrees, in [0, 360); 0 for a neutral colour. */
        double Hue(double a, double b)
        {
            if (a == 0.0 && b == 0.0)
            {
                return 0.0;
            }
            const double hue = Degrees(std::atan2(b, a));
            return hue < 0.0 ? hue + 360.0 : hue;
        }

        double Linear(std::uint8_t encoded)
        {
            const double c = encoded / 255.0;
            return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }

        double LabCurve(double t)
        {
            constexpr double delta = 6.0 / 29.0;
            return t > delta * delta * delta ? std::cbrt(t) : t / (3.0 * delta * delta) + 4.0 / 29.0;
        }
    } // namespace

    std::optional<Rgba8Image> ReadPng(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return std::nullopt;
        }
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (pixels == nullptr)
        {
            return std::nullopt;
        }
        Rgba8Image image;
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.rgba.assign(pixels, pixels + std::size_t{image.width} * image.height * 4);
        stbi_image_free(pixels);
        return image;
    }

    bool WritePng(const std::filesystem::path& path, const Rgba8Image& image)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::vector<char> bytes;
        const auto append = [](void* context, void* data, int size) {
            auto* out = static_cast<std::vector<char>*>(context);
            const auto* first = static_cast<const char*>(data);
            out->insert(out->end(), first, first + size);
        };
        const int written =
            stbi_write_png_to_func(append, &bytes, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                                   image.rgba.data(), static_cast<int>(image.width * 4));
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return written != 0 && static_cast<bool>(file);
    }

    Lab LabOfSrgb(const std::uint8_t* rgb)
    {
        const double r = Linear(rgb[0]);
        const double g = Linear(rgb[1]);
        const double b = Linear(rgb[2]);
        // Linear sRGB to XYZ, each over D65's white.
        const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
        const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
        const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
        const double fx = LabCurve(x);
        const double fy = LabCurve(y);
        const double fz = LabCurve(z);
        return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
    }

    double DeltaE2000(const Lab& first, const Lab& second)
    {
        // Sharma, Wu and Dalal, "The CIEDE2000 Color-Difference Formula", 2005, equations 2 to 22, with kL, kC and
        // kH 1.
        const double pow25To7 = 6103515625.0;
        const double c1 = std::hypot(first.a, first.b);
        const double c2 = std::hypot(second.a, second.b);
        const double cMean7 = std::pow((c1 + c2) / 2.0, 7.0);
        const double g = 0.5 * (1.0 - std::sqrt(cMean7 / (cMean7 + pow25To7)));
        const double a1 = (1.0 + g) * first.a;
        const double a2 = (1.0 + g) * second.a;
        const double c1p = std::hypot(a1, first.b);
        const double c2p = std::hypot(a2, second.b);
        const double h1p = Hue(a1, first.b);
        const double h2p = Hue(a2, second.b);

        const double dL = second.l - first.l;
        const double dC = c2p - c1p;
        double dh = 0.0;
        if (c1p * c2p != 0.0)
        {
            dh = h2p - h1p;
            if (dh > 180.0)
            {
                dh -= 360.0;
            }
            else if (dh < -180.0)
            {
                dh += 360.0;
            }
        }
        const double dH = 2.0 * std::sqrt(c1p * c2p) * std::sin(Radians(dh / 2.0));

        const double lMean = (first.l + second.l) / 2.0;
        const double cMeanP = (c1p + c2p) / 2.0;
        double hMean = h1p + h2p;
        if (c1p * c2p != 0.0)
        {
            if (std::abs(h1p - h2p) <= 180.0)
            {
                hMean /= 2.0;
            }
            else
            {
                hMean = hMean < 360.0 ? (hMean + 360.0) / 2.0 : (hMean - 360.0) / 2.0;
            }
        }
        const double t = 1.0 - 0.17 * std::cos(Radians(hMean - 30.0)) + 0.24 * std::cos(Radians(2.0 * hMean)) +
                         0.32 * std::cos(Radians(3.0 * hMean + 6.0)) - 0.20 * std::cos(Radians(4.0 * hMean - 63.0));
        const double dTheta = 30.0 * std::exp(-std::pow((hMean - 275.0) / 25.0, 2.0));
        const double cMeanP7 = std::pow(cMeanP, 7.0);
        const double rC = 2.0 * std::sqrt(cMeanP7 / (cMeanP7 + pow25To7));
        const double lOffset2 = (lMean - 50.0) * (lMean - 50.0);
        const double sL = 1.0 + 0.015 * lOffset2 / std::sqrt(20.0 + lOffset2);
        const double sC = 1.0 + 0.045 * cMeanP;
        const double sH = 1.0 + 0.015 * cMeanP * t;
        const double rT = -std::sin(Radians(2.0 * dTheta)) * rC;
        const double l = dL / sL;
        const double c = dC / sC;
        const double h = dH / sH;
        return std::sqrt(l * l + c * c + h * h + rT * c * h);
    }

    GoldenComparison CompareWithGolden(const Rgba8Image& golden, const Rgba8Image& actual,
                                       const GoldenTolerance& tolerance)
    {
        GoldenComparison comparison;
        comparison.sameSize =
            golden.width == actual.width && golden.height == actual.height && golden.rgba.size() == actual.rgba.size();
        if (!comparison.sameSize)
        {
            return comparison;
        }
        comparison.difference = golden;
        for (std::size_t i = 0; i < golden.rgba.size(); i += 4)
        {
            const double deltaE = DeltaE2000(LabOfSrgb(&golden.rgba[i]), LabOfSrgb(&actual.rgba[i]));
            comparison.largestDeltaE = std::max(comparison.largestDeltaE, deltaE);
            std::uint8_t* out = &comparison.difference.rgba[i];
            if (deltaE > tolerance.deltaE)
            {
                ++comparison.differingPixels;
                out[0] = 255;
                out[1] = 0;
                out[2] = 255;
            }
            else
            {
                out[0] = static_cast<std::uint8_t>(out[0] / 4);
                out[1] = static_cast<std::uint8_t>(out[1] / 4);
                out[2] = static_cast<std::uint8_t>(out[2] / 4);
            }
            out[3] = 255;
        }
        return comparison;
    }

    bool UpdatingGoldens()
    {
        const char* flag = std::getenv("FENRIR_UPDATE_GOLDENS");
        return flag != nullptr && std::string_view(flag) == "1";
    }

    void RequireGolden(std::string_view name, const Rgba8Image& actual, const std::filesystem::path& goldens,
                       const std::filesystem::path& failures, const GoldenTolerance& tolerance)
    {
        const std::filesystem::path golden = goldens / (std::string(name) + ".png");
        if (UpdatingGoldens())
        {
            INFO("the golden image " << golden.string() << " is written");
            REQUIRE(WritePng(golden, actual));
            WARN("FENRIR_UPDATE_GOLDENS=1: wrote the golden image " << golden.string());
            return;
        }
        const std::optional<Rgba8Image> expected = ReadPng(golden);
        INFO("the golden image " << golden.string()
                                 << " exists; make it by running the GL tests with FENRIR_UPDATE_GOLDENS=1");
        REQUIRE(expected.has_value());

        const GoldenComparison comparison = CompareWithGolden(*expected, actual, tolerance);
        if (!comparison.Matches(tolerance))
        {
            const std::filesystem::path actualPath = failures / (std::string(name) + ".actual.png");
            const std::filesystem::path differencePath = failures / (std::string(name) + ".difference.png");
            WritePng(actualPath, actual);
            if (comparison.sameSize)
            {
                WritePng(differencePath, comparison.difference);
            }
            INFO("the frame is " << actual.width << " x " << actual.height << ", the golden " << expected->width
                                 << " x " << expected->height);
            INFO(comparison.differingPixels << " pixels differ by over dE00 " << tolerance.deltaE << " (at most "
                                            << tolerance.differingPixels << " may), the largest by "
                                            << comparison.largestDeltaE);
            INFO("the frame is in " << actualPath.string() << " and the difference, magenta where it differs, in "
                                    << differencePath.string());
            FAIL("the frame does not match its golden image " << golden.string());
        }
    }
} // namespace RendererTest
