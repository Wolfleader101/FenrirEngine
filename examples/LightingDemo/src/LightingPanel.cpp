#include "LightingPanel.hpp"

#include <array>
#include <cstdint>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirScene/Scene.hpp"

namespace LightingDemo
{
    namespace
    {
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
                    Tonemappers.at((TonemapperIndex(settings.tonemapper) + 1) % Tonemappers.size());
                settings.tonemapper = next.tonemapper;
                app.Logger()->Info("T: the tonemapper is {}", next.name);
            }
            if (keyboard.JustPressed(Fenrir::Key::E))
            {
                // From one step to the next; from between steps, where the slider left it, to the second.
                std::size_t at = 0;
                for (std::size_t i = 0; i < Exposures.size(); ++i)
                {
                    if (Exposures.at(i) == settings.exposure)
                    {
                        at = i;
                    }
                }
                settings.exposure = Exposures.at((at + 1) % Exposures.size());
                app.Logger()->Info("E: the exposure is {:+.0f} EV", settings.exposure);
            }
            if (keyboard.JustPressed(Fenrir::Key::B))
            {
                settings.bloom.intensity = settings.bloom.intensity > 0.0f ? 0.0f : Fenrir::Bloom{}.intensity;
                app.Logger()->Info("B: bloom is {}", settings.bloom.intensity > 0.0f ? "on" : "off");
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
            ImGui::SliderFloat("Exposure (EV)", &settings.exposure, -6.0f, 6.0f, "%+.1f");
            if (ImGui::BeginCombo("Tonemapper", Tonemappers.at(TonemapperIndex(settings.tonemapper)).name))
            {
                for (const NamedTonemapper& choice : Tonemappers)
                {
                    if (ImGui::Selectable(choice.name, choice.tonemapper == settings.tonemapper))
                    {
                        settings.tonemapper = choice.tonemapper;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Bloom intensity", &settings.bloom.intensity, 0.0f, 1.0f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Bloom radius", &settings.bloom.radius, 0.0f, 1.0f);

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

        void Apply(Fenrir::Scene& scene, const LightingSettings& settings)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            const std::array<Fenrir::ComponentType, 1> cameras{Fenrir::ComponentType::Native<Fenrir::Camera>()};
            entities.ForEachWith(cameras, [&](std::uint32_t id) {
                auto& camera = entities.GetEntity(id).GetComponent<Fenrir::Camera>();
                camera.exposure = settings.exposure;
                camera.tonemapper = settings.tonemapper;
                camera.bloom = settings.bloom;
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
        Apply(scene, settings);
    }
} // namespace LightingDemo
