#include "LightingPanel.hpp"

#include <array>
#include <cstdint>

#include <imgui.h>

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

        std::size_t DebugViewIndex(Fenrir::CameraDebugView view)
        {
            for (std::size_t i = 0; i < DebugViews.size(); ++i)
            {
                if (DebugViews.at(i).view == view)
                {
                    return i;
                }
            }
            return 0;
        }

        std::size_t TonemapperIndex(Fenrir::Tonemapper tonemapper)
        {
            for (std::size_t i = 0; i < Tonemappers.size(); ++i)
            {
                if (Tonemappers.at(i).tonemapper == tonemapper)
                {
                    return i;
                }
            }
            return 0;
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

        void DrawWindow(LightingSettings& settings, const std::function<void()>& sceneControls)
        {
            ImGui::SetNextWindowPos({16.0f, 16.0f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Lighting");

            ImGui::SeparatorText("Camera");
            ImGui::SliderFloat("Exposure (EV)", &settings.look.exposure, -6.0f, 6.0f, "%+.1f");
            if (ImGui::BeginCombo("Tonemapper", Tonemappers.at(TonemapperIndex(settings.look.tonemapper)).name))
            {
                for (const NamedTonemapper& choice : Tonemappers)
                {
                    if (ImGui::Selectable(choice.name, choice.tonemapper == settings.look.tonemapper))
                    {
                        settings.look.tonemapper = choice.tonemapper;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Bloom intensity", &settings.look.bloom.intensity, 0.0f, 1.0f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Bloom radius", &settings.look.bloom.radius, 0.0f, 1.0f);
            // The streaks' tint is the scene's own; the renderer scales it to unit luminance.
            ImGui::SliderFloat("Streak intensity", &settings.look.anamorphicStreaks.intensity, 0.0f, 1.0f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Streak length", &settings.look.anamorphicStreaks.length, 0.0f, 1.0f);
            // The flare's ghosts and halo are the scene's own; the dirt scales the bloom's and the flare's share.
            ImGui::SliderFloat("Flare intensity", &settings.look.lensFlare.intensity, 0.0f, 0.2f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Lens dirt", &settings.look.lensFlare.dirtIntensity, 0.0f, 10.0f, "%.2f");

            // The grade's tints are linear colours; only their hue and saturation count. The CDL is left to code.
            ImGui::SeparatorText("Colour grade");
            Fenrir::ColourGrading& grading = settings.look.colourGrading;
            ImGui::SliderFloat("Temperature (K)", &grading.temperature, 2500.0f, 25000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Tint", &grading.tint, -1.0f, 1.0f, "%+.2f");
            ImGui::SliderFloat("Contrast", &grading.contrast, 0.25f, 2.5f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::ColorEdit3("Shadow tint", &grading.shadowTint.r, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Highlight tint", &grading.highlightTint.r, ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Balance", &grading.balance, -1.0f, 1.0f, "%+.2f");
            ImGui::SliderFloat("Saturation", &grading.saturation, 0.0f, 2.0f);
            if (ImGui::Button("No grade"))
            {
                grading = {};
            }

            // Each effect's main setting; the vignette's shape and the grain's response and size are the scene's own.
            ImGui::SeparatorText("Camera effects");
            ImGui::SliderFloat("Chromatic aberration", &settings.look.chromaticAberration.intensity, 0.0f, 0.05f,
                               "%.4f");
            ImGui::SliderFloat("Vignette", &settings.look.vignette.intensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Vignette radius", &settings.look.vignette.radius, 0.0f, 1.0f);
            ImGui::SliderFloat("Film grain", &settings.look.filmGrain.intensity, 0.0f, 0.2f, "%.3f");

            ImGui::SeparatorText("Ambient occlusion (GTAO)");
            ImGui::Checkbox("AO on", &settings.ambientOcclusion.enabled);
            ImGui::SliderFloat("AO radius", &settings.ambientOcclusion.radius, 0.05f, 3.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("AO intensity", &settings.ambientOcclusion.intensity, 0.0f, 3.0f);

            ImGui::SeparatorText("Reflections (SSR)");
            ImGui::Checkbox("Reflections on", &settings.reflections.enabled);
            ImGui::SliderFloat("SSR intensity", &settings.reflections.intensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Max distance", &settings.reflections.maxDistance, 1.0f, 200.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Thickness", &settings.reflections.thickness, 0.02f, 5.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Roughness cutoff", &settings.reflections.roughnessCutoff, 0.1f, 1.0f);
            // The two debug views that show them, also in the Debug view list below.
            const auto showButton = [&](const char* label, Fenrir::CameraDebugView view) {
                if (ImGui::RadioButton(label, settings.debugView == view))
                {
                    settings.debugView = settings.debugView == view ? Fenrir::CameraDebugView::None : view;
                }
            };
            showButton("Show reflections", Fenrir::CameraDebugView::Reflections);
            ImGui::SameLine();
            showButton("Show confidence", Fenrir::CameraDebugView::ReflectionConfidence);

            ImGui::SeparatorText("Sun shadows");
            ImGui::Checkbox("Shadows on", &settings.sunShadows.castShadows);
            ImGui::SliderFloat("Distance", &settings.sunShadows.shadowDistance, 5.0f, 2000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Normal bias", &settings.sunShadows.shadowNormalBias, 0.0f, 4.0f, "%.2f texels");
            ImGui::SliderFloat("Slope bias", &settings.sunShadows.shadowSlopeBias, 0.0f, 8.0f, "%.2f texels");

            ImGui::SeparatorText("Debug view");
            if (ImGui::BeginCombo("Shows", DebugViews.at(DebugViewIndex(settings.debugView)).name))
            {
                for (const NamedDebugView& choice : DebugViews)
                {
                    if (ImGui::Selectable(choice.name, choice.view == settings.debugView))
                    {
                        settings.debugView = choice.view;
                    }
                }
                ImGui::EndCombo();
            }
            if (settings.debugView == Fenrir::CameraDebugView::LightClusters)
            {
                ImGui::TextUnformatted(
                    "Lights a pixel loops over: none black, 1 blue, 4 cyan, 16 green, 64 yellow, 256 red.");
            }

            ImGui::SeparatorText("Sky");
            if (settings.panoramas.empty())
            {
                ImGui::TextDisabled("No panorama loaded");
            }
            else
            {
                if (ImGui::BeginCombo("Panorama", settings.panoramas.at(settings.panorama).name.c_str()))
                {
                    for (std::size_t i = 0; i < settings.panoramas.size(); ++i)
                    {
                        if (ImGui::Selectable(settings.panoramas.at(i).name.c_str(), i == settings.panorama))
                        {
                            settings.panorama = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Intensity", &settings.skyIntensity, 0.0f, 4.0f);
                ImGui::SliderAngle("Rotation", &settings.skyRotation, 0.0f, 360.0f);
            }

            if (sceneControls)
            {
                ImGui::SeparatorText("Scene");
                sceneControls();
            }
            ImGui::End();
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

    void AddLensDirt(Fenrir::App& app)
    {
        // Half of 1080p is fine detail enough: the dirt shows only where bloom and the flare light it, softly.
        Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
        app.AddResource<LensDirt>(
            LensDirt{store.Add<Fenrir::Image>(Fenrir::GenerateLensDirt(960, 540, 2077), Fenrir::AssetOwner::App())});
    }

    std::string LightingKeyHelp(bool panoramaSwitch)
    {
        std::string help =
            "T: the tonemapper (AgX, PBR Neutral, ACES, None). E: the exposure (0, +1, +2, -1 EV). B: "
            "bloom off or on. O: ambient occlusion off or on. G: the next debug view (occlusion, cascades, shadow "
            "map, spot shadow atlas, light clusters, reflections, their confidence). H: the sun's shadows off or on.";
        if (panoramaSwitch)
        {
            help += " S: the next sky.";
        }
        return help + " The Lighting window sets each of them, and the sky's intensity and rotation.";
    }

    void RunLightingPanel(Fenrir::App& app, Fenrir::Scene& scene, LightingSettings& settings,
                          const std::function<void()>& sceneControls)
    {
        ApplyKeys(app, settings);
        DrawWindow(settings, sceneControls);
        Apply(app, scene, settings);
    }
} // namespace LightingDemo
