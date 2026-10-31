#include "FetchedAssets.hpp"

#include <cstdint>
#include <filesystem>
#include <format>
#include <system_error>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirScene/Scene.hpp"

namespace LightingDemo
{
    namespace
    {
        // The key of the message ShowMissingAssets keeps in place each frame.
        constexpr std::int64_t MissingAssetsMessage = 0x4C49'4748;

        bool SmokeRun()
        {
            return FenrirDemo::DemoRunOptions::FromEnvironment().smokeFrames.has_value();
        }
    } // namespace

    std::optional<std::string> FetchedAsset(std::string_view path)
    {
#ifdef LIGHTING_DEMO_FETCHED_ASSETS
        if (SmokeRun())
        {
            return std::nullopt;
        }
        const std::filesystem::path file = std::filesystem::path(LIGHTING_DEMO_FETCHED_ASSETS) / path;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error))
        {
            return file.generic_string();
        }
#else
        static_cast<void>(path);
#endif
        return std::nullopt;
    }

    void ShowMissingAssets(Fenrir::Scene& scene, std::string_view sceneName)
    {
        Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
        if (draw == nullptr)
        {
            return;
        }
#ifdef LIGHTING_DEMO_FETCHED_ASSETS
        const char* why = SmokeRun() ? "smoke runs use committed assets only (ADR-0052)"
                                     : "some are not in " LIGHTING_DEMO_FETCHED_ASSETS
                                       ": configure again to fetch them";
#else
        const char* why = "configure with -DFENRIR_FETCH_DEMO_ASSETS=ON to download them (ADR-0052)";
#endif
        draw->Message(MissingAssetsMessage, std::format("{} needs the fetched demo assets: {}", sceneName, why),
                      Fenrir::Colour{1.0f, 0.75f, 0.3f, 1.0f}, {.size = 22.0f});
    }
} // namespace LightingDemo
