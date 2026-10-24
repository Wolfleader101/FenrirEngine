#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirScene/Scene.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr Fenrir::WindowId MainWindow{1};

        // The colours C cycles the cube through, sRGB.
        constexpr std::array<std::uint32_t, 4> CubeColours{0xE8E8E8FF, 0xD04040FF, 0x40B040FF, 0x4070E0FF};

        /** @brief The scene's cube, what its keys, clicks and ImGui window have set, kept as a scene resource. */
        struct OverlayState
        {
            Fenrir::AssetHandle<Fenrir::Shader> shader2D;
            Fenrir::AssetHandle<Fenrir::Material> cubePaint;
            Fenrir::Entity cube;
            float angle = 0.0f;
            float speed = 0.7f;
            bool paused = false;
            std::size_t colour = 0;
            int clicks = 0;
            std::array<char, 64> label{"Type here"};
            Fenrir::EventReader<FenrirDemo::DemoSceneClickEvent> clickReader;
        };

        /** @brief An RGBA8 vertex colour, red in the lowest byte, as builtin:2d reads it. */
        constexpr std::uint32_t Rgba(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a = 255)
        {
            return r | (g << 8u) | (b << 16u) | (a << 24u);
        }

        /**
         * @brief Appends a rectangle drawn with `pipeline`, its top edge `top` and its bottom edge `bottom` in colour,
         * clipped to `scissor` if given.
         */
        void AddRect(Fenrir::DrawList2D& list, std::uint32_t pipeline, Fenrir::Rect2D rect, std::uint32_t top,
                     std::uint32_t bottom, std::optional<Fenrir::Rect2D> scissor = std::nullopt)
        {
            const float right = rect.x + rect.width;
            const float lower = rect.y + rect.height;
            const std::array<Fenrir::Vertex2D, 4> vertices{{{{rect.x, rect.y}, {0.0f, 0.0f}, top},
                                                            {{right, rect.y}, {0.0f, 0.0f}, top},
                                                            {{right, lower}, {0.0f, 0.0f}, bottom},
                                                            {{rect.x, lower}, {0.0f, 0.0f}, bottom}}};
            constexpr std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
            Fenrir::Command2D command;
            command.pipeline = pipeline;
            command.baseVertex =
                static_cast<std::int32_t>(list.AppendVertices(std::span<const Fenrir::Vertex2D>(vertices)));
            command.indexOffset = list.AppendIndices(std::span<const std::uint16_t>(indices));
            command.indexCount = static_cast<std::uint32_t>(indices.size());
            command.scissor = scissor;
            list.commands.push_back(std::move(command));
        }

        /** @brief The index of builtin:2d's pipeline with `blendMode` in `list` (sRGB vertex colours). */
        std::uint32_t Pipeline(Fenrir::DrawList2D& list, const OverlayState& state, Fenrir::BlendMode2D blendMode)
        {
            Fenrir::PipelineDescription2D pipeline;
            pipeline.shader = state.shader2D;
            pipeline.vertexLayout = Fenrir::Vertex2DLayout();
            pipeline.blendMode = blendMode;
            return list.AddPipeline(pipeline);
        }

        // Layers: three overlapping squares submitted red, green, blue, on layers 2, 1 and 0, so they draw blue first
        // and red last, on top.
        void SubmitLayers(Fenrir::Overlay& overlay, Fenrir::DebugDraw& draw, const OverlayState& state)
        {
            constexpr std::array<std::pair<std::uint32_t, std::int32_t>, 3> squares{
                {{Rgba(220, 50, 50), 2}, {Rgba(50, 200, 70), 1}, {Rgba(60, 100, 230), 0}}};
            float offset = 0.0f;
            for (const auto& [colour, layer] : squares)
            {
                Fenrir::DrawList2D list;
                const std::uint32_t pipeline = Pipeline(list, state, Fenrir::BlendMode2D::AlphaBlend);
                AddRect(list, pipeline, {40.0f + offset, 120.0f + offset, 120.0f, 120.0f}, colour, colour);
                overlay.Submit(MainWindow, layer, std::move(list));
                offset += 40.0f;
            }
            draw.ScreenText({40.0f, 96.0f}, "Layers: sent 2, 1, 0", Fenrir::Colour::White);
        }

        // Scissors: a quad red at its top and blue at its bottom, faint in full, and solid where a scissor keeps only
        // its top-left quarter; and a strip whose scissor keeps only its middle third.
        void AddScissors(Fenrir::DrawList2D& list, Fenrir::DebugDraw& draw, const OverlayState& state)
        {
            const std::uint32_t pipeline = Pipeline(list, state, Fenrir::BlendMode2D::AlphaBlend);
            const Fenrir::Rect2D quad{360.0f, 120.0f, 240.0f, 160.0f};
            AddRect(list, pipeline, quad, Rgba(230, 40, 40, 60), Rgba(40, 40, 230, 60));
            AddRect(list, pipeline, quad, Rgba(230, 40, 40), Rgba(40, 40, 230),
                    Fenrir::Rect2D{quad.x, quad.y, quad.width / 2.0f, quad.height / 2.0f});
            const Fenrir::Rect2D strip{360.0f, 300.0f, 240.0f, 24.0f};
            AddRect(list, pipeline, strip, Rgba(250, 210, 60), Rgba(250, 210, 60),
                    Fenrir::Rect2D{strip.x + 80.0f, strip.y - 10.0f, 80.0f, 44.0f});
            draw.ScreenText({360.0f, 96.0f}, "Scissors", Fenrir::Colour::White);
        }

        // Blend modes: the same half-transparent orange in each mode, over black and white stripes.
        void AddBlendModes(Fenrir::DrawList2D& list, Fenrir::DebugDraw& draw, const OverlayState& state)
        {
            const std::uint32_t opaque = Pipeline(list, state, Fenrir::BlendMode2D::Opaque);
            constexpr float left = 40.0f;
            constexpr float top = 400.0f;
            for (int stripe = 0; stripe < 8; ++stripe)
            {
                const std::uint32_t shade = stripe % 2 == 0 ? Rgba(0, 0, 0) : Rgba(255, 255, 255);
                AddRect(list, opaque, {left, top + 20.0f * static_cast<float>(stripe), 560.0f, 20.0f}, shade, shade);
            }
            constexpr std::array<std::pair<Fenrir::BlendMode2D, const char*>, 4> modes{
                {{Fenrir::BlendMode2D::AlphaBlend, "AlphaBlend"},
                 {Fenrir::BlendMode2D::PremultipliedAlpha, "Premultiplied"},
                 {Fenrir::BlendMode2D::Additive, "Additive"},
                 {Fenrir::BlendMode2D::Opaque, "Opaque"}}};
            constexpr std::uint32_t halfOrange = Rgba(255, 140, 0, 128);
            float x = left + 20.0f;
            for (const auto& [mode, name] : modes)
            {
                AddRect(list, Pipeline(list, state, mode), {x, top + 20.0f, 110.0f, 120.0f}, halfOrange, halfOrange);
                draw.ScreenText({x, top + 170.0f}, name, Fenrir::Colour::White);
                x += 140.0f;
            }
            draw.ScreenText({left, top - 24.0f}, "Blend modes: half-transparent orange over stripes",
                            Fenrir::Colour::White);
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            OverlayState state;
            state.shader2D = store.Load<Fenrir::Shader>("builtin:2d", scene).handle;
            // Clicks before the scene was entered are not its own.
            (void)state.clickReader.Read(app);

            const Fenrir::LoadResult<Fenrir::Mesh> cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("Overlay: the cube did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto material = [&](std::uint32_t srgb) {
                const auto made = store.CreateMaterial(lit, scene);
                store.SetColour(made, "baseColour", Fenrir::Colour::FromSrgb(srgb));
                store.SetFloat(made, "roughness", 0.5f);
                return made;
            };
            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto add = [&](const Vec3& position, const Vec3& scale, Fenrir::AssetHandle<Fenrir::Material> paint) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube.handle, paint});
                return entity;
            };

            // A floor and a spinning cube to the right of the raw draw list, under the ImGui window.
            add({2.0f, -0.25f, 0.0f}, {5.0f, 0.5f, 5.0f}, material(0x8A8F99FF));
            state.cubePaint = material(CubeColours[0]);
            state.cube = add({2.0f, 0.9f, 0.0f}, {1.2f, 1.2f, 1.2f}, state.cubePaint);

            Fenrir::Entity light = entities.CreateEntity();
            light.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-0.4f, -0.8f, -0.5f)), Vec3(0.0f, 1.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();
            scene.AddResource<Fenrir::AmbientLight>(Fenrir::AmbientLight{Fenrir::Colour{0.2f, 0.22f, 0.25f, 1.0f}});
            scene.AddResource<OverlayState>(std::move(state));
        }

        // The ImGui window: a text field and a slider over the scene, and what the scene and the claims say.
        void ImGuiPanel(Fenrir::App& app, OverlayState& state)
        {
            ImGui::SetNextWindowPos({760.0f, 60.0f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({460.0f, 180.0f}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Overlay");
            ImGui::TextWrapped("While ImGui has the pointer or the keyboard, the scene's keys and clicks stop.");
            ImGui::InputText("Label", state.label.data(), state.label.size());
            ImGui::SliderFloat("Spin speed", &state.speed, 0.0f, 5.0f);
            ImGui::Separator();
            ImGui::Text("Scene: spin %s (Space), colour %zu (C), %d click(s)", state.paused ? "paused" : "on",
                        state.colour + 1, state.clicks);
            const Fenrir::WindowInputClaims claims = app.GetResource<Fenrir::InputClaims>().For(MainWindow);
            ImGui::Text("Claims: pointer %s, capture %s, keyboard %s, text %s", claims.pointer ? "yes" : "no",
                        claims.pointerCapture ? "yes" : "no", claims.keyboardFocus ? "yes" : "no",
                        claims.textInput ? "yes" : "no");
            ImGui::End();
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            OverlayState* state = scene.GetResource<OverlayState>();
            if (state == nullptr)
            {
                return;
            }

            // The scene's own keys and clicks: what ImGui took never reaches them.
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::Space))
            {
                state->paused = !state->paused;
                app.Logger()->Info("Space: the cube's spin is {}", state->paused ? "paused" : "on");
            }
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                state->colour = (state->colour + 1) % CubeColours.size();
                app.GetResource<Fenrir::AssetStore>().SetColour(
                    state->cubePaint, "baseColour", Fenrir::Colour::FromSrgb(CubeColours.at(state->colour)));
                app.Logger()->Info("C: the cube's colour is {}", state->colour + 1);
            }
            for (const FenrirDemo::DemoSceneClickEvent& click : state->clickReader.Read(app))
            {
                ++state->clicks;
                app.Logger()->Info("Overlay: scene click {} at ({}, {})", state->clicks, click.position.x,
                                   click.position.y);
            }

            if (!state->paused)
            {
                state->angle += state->speed * static_cast<float>(app.GetTime().deltaTime);
            }
            if (state->cube.IsValid())
            {
                state->cube.GetComponent<Fenrir::Transform>().rot =
                    Fenrir::Math::AngleAxis(state->angle, Vec3(0.0f, 1.0f, 0.0f));
            }

            auto* overlay = app.TryGetResource<Fenrir::Overlay>();
            auto* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (overlay != nullptr && draw != nullptr)
            {
                SubmitLayers(*overlay, *draw, *state);
                Fenrir::DrawList2D list;
                AddScissors(list, *draw, *state);
                AddBlendModes(list, *draw, *state);
                overlay->Submit(MainWindow, 0, std::move(list));
                draw->WorldText({2.0f, 1.7f, 0.0f}, state->label.data(), Fenrir::Colour::White);
            }

            ImGuiPanel(app, *state);
        }
    } // namespace

    FenrirDemo::DemoScene OverlayScene()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Overlay";
        scene.keyHelp = "Space: pause or resume the cube's spin. C: the cube's colour. Left click: counted as a scene "
                        "click. All three stop while the ImGui window has the pointer or the keyboard.";
        scene.keys = {Fenrir::Key::Space, Fenrir::Key::C, Fenrir::Key::Space, Fenrir::Key::C};
        scene.camera.position = {0.5f, 1.8f, 6.0f};
        scene.camera.pitch = -0.15f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
