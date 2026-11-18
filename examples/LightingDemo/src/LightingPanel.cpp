#include "LightingPanel.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/LensDirt.hpp"
#include "FenrirScene/Scene.hpp"

namespace LightingDemo
{
    namespace
    {
        using FenrirDemo::DemoRangeOptions;
        using FenrirDemo::DemoSection;
        using FenrirDemo::DemoValue;

        /** @brief The demo's generated lens dirt, an app resource (AddLensDirt). */
        struct LensDirt
        {
            Fenrir::AssetHandle<Fenrir::Image> image;
        };

        struct NamedTonemapper
        {
            Fenrir::Tonemapper tonemapper;
            const char* name;
        };
        // In the order T steps through them, from the default.
        constexpr std::array<NamedTonemapper, 4> Tonemappers{{{Fenrir::Tonemapper::AgX, "AgX"},
                                                              {Fenrir::Tonemapper::PbrNeutral, "PBR Neutral"},
                                                              {Fenrir::Tonemapper::Aces, "ACES"},
                                                              {Fenrir::Tonemapper::None, "None (clamped)"}}};
        // What E steps the exposure through, in EV stops.
        constexpr std::array<float, 4> Exposures{0.0f, 1.0f, 2.0f, -1.0f};

        struct NamedDebugView
        {
            Fenrir::CameraDebugView view;
            const char* name;
        };
        // In the order G steps through them, from the lit scene.
        constexpr std::array<NamedDebugView, 8> DebugViews{
            {{Fenrir::CameraDebugView::None, "the lit scene"},
             {Fenrir::CameraDebugView::AmbientOcclusion, "the raw ambient occlusion"},
             {Fenrir::CameraDebugView::ShadowCascades, "the shadow cascades as colours"},
             {Fenrir::CameraDebugView::ShadowAtlas, "the shadow atlas"},
             {Fenrir::CameraDebugView::SpotShadowAtlas, "the spot shadow atlas"},
             {Fenrir::CameraDebugView::LightClusters, "the light clusters' heat"},
             {Fenrir::CameraDebugView::Reflections, "the reflections"},
             {Fenrir::CameraDebugView::ReflectionConfidence, "the reflections' confidence"}}};
        // The two debug views the Reflections section's choice shows, in its order.
        constexpr std::array<Fenrir::CameraDebugView, 2> ReflectionViews{Fenrir::CameraDebugView::Reflections,
                                                                         Fenrir::CameraDebugView::ReflectionConfidence};

        /** @brief The index of the first of `entries` that `matches`, or `missing` when none does. */
        template <typename Entries, typename Matches>
        int IndexWhere(const Entries& entries, Matches matches, int missing)
        {
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (matches(entries.at(i)))
                {
                    return static_cast<int>(i);
                }
            }
            return missing;
        }

        std::size_t DebugViewIndex(Fenrir::CameraDebugView view)
        {
            return static_cast<std::size_t>(
                IndexWhere(DebugViews, [view](const NamedDebugView& entry) { return entry.view == view; }, 0));
        }

        std::size_t TonemapperIndex(Fenrir::Tonemapper tonemapper)
        {
            return static_cast<std::size_t>(IndexWhere(
                Tonemappers, [tonemapper](const NamedTonemapper& entry) { return entry.tonemapper == tonemapper; }, 0));
        }

        void ApplyKeys(Fenrir::App& app, LightingSettings& settings)
        {
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::T))
            {
                const NamedTonemapper& next =
                    Tonemappers.at((TonemapperIndex(settings.look.tonemapper) + 1) % Tonemappers.size());
                settings.look.tonemapper = next.tonemapper;
                app.Logger()->Info("T: the tonemapper is {}", next.name);
            }
            if (keyboard.JustPressed(Fenrir::Key::E))
            {
                // From one step to the next; from between steps, where the slider left it, to the second.
                std::size_t at = 0;
                for (std::size_t i = 0; i < Exposures.size(); ++i)
                {
                    if (Exposures.at(i) == settings.look.exposure)
                    {
                        at = i;
                    }
                }
                settings.look.exposure = Exposures.at((at + 1) % Exposures.size());
                app.Logger()->Info("E: the exposure is {:+.0f} EV", settings.look.exposure);
            }
            if (keyboard.JustPressed(Fenrir::Key::B))
            {
                settings.look.bloom.intensity = settings.look.bloom.intensity > 0.0f ? 0.0f : Fenrir::Bloom{}.intensity;
                app.Logger()->Info("B: bloom is {}", settings.look.bloom.intensity > 0.0f ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::O))
            {
                settings.ambientOcclusion.enabled = !settings.ambientOcclusion.enabled;
                app.Logger()->Info("O: ambient occlusion is {}", settings.ambientOcclusion.enabled ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::G))
            {
                const NamedDebugView& next =
                    DebugViews.at((DebugViewIndex(settings.debugView) + 1) % DebugViews.size());
                settings.debugView = next.view;
                app.Logger()->Info("G: the view shows {}", next.name);
            }
            if (keyboard.JustPressed(Fenrir::Key::H))
            {
                settings.sunShadows.castShadows = !settings.sunShadows.castShadows;
                app.Logger()->Info("H: the sun's shadows are {}", settings.sunShadows.castShadows ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::S) && settings.panoramas.size() > 1)
            {
                settings.panorama = (settings.panorama + 1) % settings.panoramas.size();
                app.Logger()->Info("S: the sky is {}", settings.panoramas.at(settings.panorama).name);
            }
        }

        /**
         * @brief A logarithmic range from 0 to `max`: the kit's logarithmic range needs a positive minimum, so the
         * slider starts at the smallest value `decimals` shows, and its left end writes 0.
         */
        void LogRangeFromZero(const DemoSection& section, std::string label, float* value, float max, int decimals,
                              std::string hint)
        {
            const float least = std::pow(10.0f, static_cast<float>(-decimals));
            section.Range(std::move(label),
                          DemoValue<float>([value] { return *value; },
                                           [value, least](const float& next) { *value = next <= least ? 0.0f : next; }),
                          least, max, DemoRangeOptions{.logarithmic = true, .decimals = decimals}, std::move(hint));
        }

        std::vector<std::string> Names(const auto& named)
        {
            std::vector<std::string> names;
            for (const auto& entry : named)
            {
                names.emplace_back(entry.name);
            }
            return names;
        }

        void AddCamera(const DemoSection& camera, Fenrir::CameraLook& look)
        {
            camera.Range("Exposure (EV)", &look.exposure, -6.0f, 6.0f, {.decimals = 1},
                         "The cameras' exposure in stops; E steps 0, +1, +2 and -1");
            camera.Choice("Tonemapper",
                          DemoValue<int>([&look] { return static_cast<int>(TonemapperIndex(look.tonemapper)); },
                                         [&look](const int& index) {
                                             look.tonemapper =
                                                 Tonemappers.at(static_cast<std::size_t>(index)).tonemapper;
                                         }),
                          Names(Tonemappers), "How the HDR image is brought into the display's range; T steps them");
            LogRangeFromZero(camera, "Bloom intensity", &look.bloom.intensity, 1.0f, 3,
                             "How much of the image blooms; B turns it off and back on");
            camera.Range("Bloom radius", &look.bloom.radius, 0.0f, 1.0f, {.decimals = 3}, "How far the bloom spreads");
            // The streaks' tint is the scene's own; the renderer scales it to unit luminance.
            LogRangeFromZero(camera, "Streak intensity", &look.anamorphicStreaks.intensity, 1.0f, 3,
                             "The anamorphic streaks across the bright lights");
            camera.Range("Streak length", &look.anamorphicStreaks.length, 0.0f, 1.0f, {.decimals = 3},
                         "How far the streaks reach");
            // The flare's ghosts and halo are the scene's own; the dirt scales the bloom's and the flare's share.
            LogRangeFromZero(camera, "Flare intensity", &look.lensFlare.intensity, 0.2f, 3,
                             "The lens flare's ghosts and halo");
            camera.Range("Lens dirt", &look.lensFlare.dirtIntensity, 0.0f, 10.0f, {},
                         "How much the lens dirt shows in the bloom and the flare");
        }

        void AddColourGrade(const DemoSection& grade, Fenrir::ColourGrading& grading)
        {
            // The tints are linear colours; only their hue and saturation count. The CDL is left to code.
            grade.Range("Temperature (K)", &grading.temperature, 2500.0f, 25000.0f,
                        {.logarithmic = true, .decimals = 0}, "White balance: the colour temperature of white");
            grade.Range("Tint", &grading.tint, -1.0f, 1.0f, {}, "White balance: green to magenta");
            grade.Range("Contrast", &grading.contrast, 0.25f, 2.5f, {.logarithmic = true},
                        "The picture's slope in stops around mid grey");
            grade.Colour("Shadow tint", LinearColour(&grading.shadowTint),
                         "The tint the shadows take; only its hue and saturation count");
            grade.Colour("Highlight tint", LinearColour(&grading.highlightTint),
                         "The tint the highlights take; only its hue and saturation count");
            grade.Range("Balance", &grading.balance, -1.0f, 1.0f, {}, "Where the shadows end and the highlights start");
            grade.Range("Saturation", &grading.saturation, 0.0f, 2.0f, {.decimals = 3},
                        "How far each colour is from grey");
            grade.Button("No grade", [&grading] { grading = {}; }, "Puts every grade setting back to none");
        }

        void AddCameraEffects(const DemoSection& effects, Fenrir::CameraLook& look)
        {
            // Each effect's main setting; the vignette's shape and the grain's response and size are the scene's own.
            effects.Range("Chromatic aberration", &look.chromaticAberration.intensity, 0.0f, 0.05f, {.decimals = 4},
                          "Colour fringes towards the edges");
            effects.Range("Vignette", &look.vignette.intensity, 0.0f, 1.0f, {.decimals = 3}, "How dark the corners go");
            effects.Range("Vignette radius", &look.vignette.radius, 0.0f, 1.0f, {.decimals = 3},
                          "Where the darkening starts");
            effects.Range("Film grain", &look.filmGrain.intensity, 0.0f, 0.2f, {.decimals = 3}, "The grain's strength");
        }

        void AddAmbientOcclusion(const DemoSection& section, Fenrir::AmbientOcclusion& occlusion)
        {
            section.Toggle("AO on", &occlusion.enabled, "Ground-truth ambient occlusion; O turns it off and on");
            section.Range("AO radius", &occlusion.radius, 0.05f, 3.0f, {.logarithmic = true},
                          "How far, in metres, the occlusion looks");
            section.Range("AO intensity", &occlusion.intensity, 0.0f, 3.0f, {.decimals = 3},
                          "How dark the occlusion makes it");
        }

        void AddReflections(const DemoSection& section, LightingSettings& settings)
        {
            Fenrir::Reflections& reflections = settings.reflections;
            section.Toggle("Reflections on", &reflections.enabled, "Screen-space reflections");
            section.Range("SSR intensity", &reflections.intensity, 0.0f, 1.0f, {.decimals = 3},
                          "How strong the reflections are");
            section.Range("Max distance", &reflections.maxDistance, 1.0f, 200.0f, {.logarithmic = true, .decimals = 0},
                          "How far, in metres, a reflection ray marches");
            section.Range("Thickness", &reflections.thickness, 0.02f, 5.0f, {.logarithmic = true},
                          "How thick, in metres, a surface is taken to be behind what the screen shows");
            section.Range("Roughness cutoff", &reflections.roughnessCutoff, 0.1f, 1.0f, {.decimals = 3},
                          "Surfaces rougher than this reflect nothing");
            // The two debug views that show them, also in the Debug view's list. The chosen one again shows the lit
            // scene, as its radio button did.
            Fenrir::CameraDebugView& view = settings.debugView;
            section.Choice(
                "Show",
                DemoValue<int>(
                    [&view] {
                        return IndexWhere(
                            ReflectionViews, [&view](Fenrir::CameraDebugView entry) { return entry == view; }, -1);
                    },
                    [&view](const int& index) {
                        const Fenrir::CameraDebugView chosen = ReflectionViews.at(static_cast<std::size_t>(index));
                        view = view == chosen ? Fenrir::CameraDebugView::None : chosen;
                    }),
                {"Show reflections", "Show confidence"},
                "Shows the reflections, or how sure each is; the chosen one again shows the lit scene");
        }

        void AddSunShadows(const DemoSection& section, Fenrir::DirectionalLight& sun)
        {
            section.Toggle("Shadows on", &sun.castShadows, "Every sun's cascaded shadows; H turns them off and on");
            section.Range("Distance", &sun.shadowDistance, 5.0f, 2000.0f, {.logarithmic = true, .decimals = 0},
                          "How far, in metres, the shadows reach");
            section.Range("Normal bias (texels)", &sun.shadowNormalBias, 0.0f, 4.0f, {},
                          "Pushes the lookup out along the surface; too low brings acne back");
            section.Range("Slope bias (texels)", &sun.shadowSlopeBias, 0.0f, 8.0f, {},
                          "Pushes the depth back on steep slopes; too high detaches the shadows");
        }

        /** @brief The Debug view section's rows; returns the light-cluster legend, shown only for that view. */
        FenrirDemo::DemoTextRow AddDebugView(const DemoSection& section, Fenrir::CameraDebugView& view)
        {
            section.Choice("Shows",
                           DemoValue<int>([&view] { return static_cast<int>(DebugViewIndex(view)); },
                                          [&view](const int& index) {
                                              view = DebugViews.at(static_cast<std::size_t>(index)).view;
                                          }),
                           Names(DebugViews), "What the cameras show; G steps through them");
            return section.Text("Lights a pixel loops over: none black, 1 blue, 4 cyan, 16 green, 64 yellow, 256 red.");
        }

        void AddSky(const DemoSection& section, LightingSettings& settings)
        {
            if (settings.panoramas.empty())
            {
                section.Text("No panorama loaded");
                return;
            }
            const bool switches = settings.panoramas.size() > 1;
            section.Choice(
                "Panorama",
                DemoValue<int>([&settings] { return static_cast<int>(settings.panorama); },
                               [&settings](const int& index) { settings.panorama = static_cast<std::size_t>(index); }),
                Names(settings.panoramas), switches ? "The sky's panorama; S steps through them" : "The sky");
            section.Range("Intensity", &settings.skyIntensity, 0.0f, 4.0f, {.decimals = 3},
                          "Scales the sky and the light it gives");
            section.Range("Rotation", &settings.skyRotation, 0.0f, 2.0f * std::numbers::pi_v<float>,
                          {.decimals = 0, .angle = true}, "Turns the sky about the vertical, and its light with it");
        }

        void Apply(Fenrir::App& app, Fenrir::Scene& scene, const LightingSettings& settings)
        {
            Fenrir::CameraLook look = settings.look;
            if (const LensDirt* dirt = app.TryGetResource<LensDirt>(); dirt != nullptr && look.lensFlare.dirt.IsEmpty())
            {
                look.lensFlare.dirt = dirt->image;
            }
            Fenrir::EntityList& entities = scene.GetEntityList();
            const std::array<Fenrir::ComponentType, 1> cameras{Fenrir::ComponentType::Native<Fenrir::Camera>()};
            entities.ForEachWith(cameras, [&](std::uint32_t id) {
                auto& camera = entities.GetEntity(id).GetComponent<Fenrir::Camera>();
                camera.look = look;
                camera.ambientOcclusion = settings.ambientOcclusion;
                camera.reflections = settings.reflections;
                camera.debugView = settings.debugView;
            });
            const std::array<Fenrir::ComponentType, 1> suns{Fenrir::ComponentType::Native<Fenrir::DirectionalLight>()};
            entities.ForEachWith(suns, [&](std::uint32_t id) {
                auto& sun = entities.GetEntity(id).GetComponent<Fenrir::DirectionalLight>();
                sun.castShadows = settings.sunShadows.castShadows;
                sun.shadowDistance = settings.sunShadows.shadowDistance;
                sun.shadowNormalBias = settings.sunShadows.shadowNormalBias;
                sun.shadowSlopeBias = settings.sunShadows.shadowSlopeBias;
            });

            if (settings.panoramas.empty())
            {
                return;
            }
            // Neither intensity nor rotation filters the sky again (ADR-0047), so it is written every frame.
            const Fenrir::Sky sky = Fenrir::PanoramaSky{settings.panoramas.at(settings.panorama).image,
                                                        settings.skyIntensity, settings.skyRotation};
            if (Fenrir::Sky* current = scene.GetResource<Fenrir::Sky>())
            {
                *current = sky;
            }
            else
            {
                scene.AddResource<Fenrir::Sky>(sky);
            }
        }
    } // namespace

    void LightingPanel::Update(Fenrir::App& app, LightingSettings& settings)
    {
        if (!m_camera.IsValid())
        {
            auto* kit = app.TryGetResource<FenrirDemo::DemoUI>();
            if (kit == nullptr)
            {
                return;
            }
            m_camera = kit->SceneSection("Camera");
            AddCamera(m_camera, settings.look);
            AddColourGrade(kit->SceneSection("Colour grade"), settings.look.colourGrading);
            AddCameraEffects(kit->SceneSection("Camera effects"), settings.look);
            AddAmbientOcclusion(kit->SceneSection("Ambient occlusion (GTAO)"), settings.ambientOcclusion);
            AddReflections(kit->SceneSection("Reflections (SSR)"), settings);
            AddSunShadows(kit->SceneSection("Sun shadows"), settings.sunShadows);
            m_clusterLegend = AddDebugView(kit->SceneSection("Debug view"), settings.debugView);
            AddSky(kit->SceneSection("Sky"), settings);
        }
        m_clusterLegend.SetShown(settings.debugView == Fenrir::CameraDebugView::LightClusters);
    }

    void AddLensDirt(Fenrir::App& app)
    {
        // Half of 1080p is fine detail enough: the dirt shows only where bloom and the flare light it, softly.
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        app.AddResource<LensDirt>(
            LensDirt{store.Add<Fenrir::Image>(Fenrir::GenerateLensDirt(960, 540, 2077), Fenrir::AssetOwner::App())});
    }

    FenrirDemo::DemoValue<Fenrir::Math::Vec3> LinearColour(Fenrir::Colour* colour)
    {
        return {[colour] { return Fenrir::Math::Vec3(colour->r, colour->g, colour->b); },
                [colour](const Fenrir::Math::Vec3& next) {
                    colour->r = next.x;
                    colour->g = next.y;
                    colour->b = next.z;
                }};
    }

    std::vector<FenrirDemo::DemoKeyRow> LightingKeyRows(bool panoramaSwitch)
    {
        std::vector<FenrirDemo::DemoKeyRow> rows{
            {"T", "the tonemapper (AgX, PBR Neutral, ACES, None)"},
            {"E", "the exposure (0, +1, +2, -1 EV)"},
            {"B", "bloom off or on"},
            {"O", "ambient occlusion off or on"},
            {"G", "the next debug view (occlusion, cascades, shadow map, spot shadow atlas, light clusters, "
                  "reflections, their confidence)"},
            {"H", "the sun's shadows off or on"}};
        if (panoramaSwitch)
        {
            rows.push_back({"S", "the next sky"});
        }
        return rows;
    }

    std::string FrameReadout(float frameSeconds)
    {
        return std::format("{:.2f} ms ({:.0f} fps)", frameSeconds * 1000.0f,
                           frameSeconds > 0.0f ? 1.0f / frameSeconds : 0.0f);
    }

    std::function<std::string()> RendererCount(Fenrir::App& app, std::uint32_t Fenrir::RendererStats::* count)
    {
        return [&app, count] { return std::format("{}", Fenrir::Renderer::Stats(app).*count); };
    }

    void RunLightingPanel(Fenrir::App& app, Fenrir::Scene& scene, LightingSettings& settings)
    {
        ApplyKeys(app, settings);
        settings.panel.Update(app, settings);
        Apply(app, scene, settings);
    }
} // namespace LightingDemo
