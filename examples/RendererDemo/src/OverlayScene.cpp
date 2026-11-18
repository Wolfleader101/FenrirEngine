#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirScene/Scene.hpp"
#include "RendererScenes.hpp"
#include "SceneLabels.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr Fenrir::WindowId MainWindow{1};

        // The colours C cycles the cube through, sRGB.
        constexpr std::array<std::uint32_t, 4> CubeColours{0xE8E8E8FF, 0xD04040FF, 0x40B040FF, 0x4070E0FF};
        // The labels' layer: over the layers test's squares (0 to 2), as debug screen text was over every layer.
        constexpr std::int32_t LabelLayer = 3;

        /** @brief The scene's cube, what its keys, clicks and demo panel section have set, kept as a scene resource. */
        struct OverlayState
        {
            Fenrir::AssetHandle<Fenrir::Shader> shader2D;
            Fenrir::AssetHandle<Fenrir::Shader> painterShader;
            SceneLabels labels;
            Fenrir::AssetHandle<Fenrir::Material> cubePaint;
            Fenrir::Entity cube;
            float angle = 0.0f;
            float speed = 0.7f;
            bool paused = false;
            std::size_t colour = 0;
            int clicks = 0;
            std::string label = "Type here";
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
            Fenrir::DrawCommand2D command;
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
        void SubmitLayers(Fenrir::Overlay& overlay, SceneLabels& labels, const OverlayState& state)
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
            labels.Draw("Layers: sent 2, 1, 0", {40.0f, 96.0f});
        }

        // Scissors: a quad red at its top and blue at its bottom, faint in full, and solid where a scissor keeps only
        // its top-left quarter; and a strip whose scissor keeps only its middle third.
        void AddScissors(Fenrir::DrawList2D& list, SceneLabels& labels, const OverlayState& state)
        {
            const std::uint32_t pipeline = Pipeline(list, state, Fenrir::BlendMode2D::AlphaBlend);
            const Fenrir::Rect2D quad{360.0f, 120.0f, 240.0f, 160.0f};
            AddRect(list, pipeline, quad, Rgba(230, 40, 40, 60), Rgba(40, 40, 230, 60));
            AddRect(list, pipeline, quad, Rgba(230, 40, 40), Rgba(40, 40, 230),
                    Fenrir::Rect2D{quad.x, quad.y, quad.width / 2.0f, quad.height / 2.0f});
            const Fenrir::Rect2D strip{360.0f, 300.0f, 240.0f, 24.0f};
            AddRect(list, pipeline, strip, Rgba(250, 210, 60), Rgba(250, 210, 60),
                    Fenrir::Rect2D{strip.x + 80.0f, strip.y - 10.0f, 80.0f, 44.0f});
            labels.Draw("Scissors", {360.0f, 96.0f});
        }

        // Blend modes: the same half-transparent orange in each mode, over black and white stripes.
        void AddBlendModes(Fenrir::DrawList2D& list, SceneLabels& labels, const OverlayState& state)
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
                labels.Draw(name, {x, top + 170.0f});
                x += 140.0f;
            }
            labels.Draw("Blend modes: half-transparent orange over stripes", {left, top - 24.0f});
        }

        /** @brief "yes" or "no", for a claims readout. */
        std::string YesNo(bool value)
        {
            return value ? "yes" : "no";
        }

        // The scene's section of the demo panel: the label's entry and the spin speed over the scene, what the scene
        // has counted, and the main window's live input claims. Its rows go before the state they point into.
        void AddSection(Fenrir::App& app, FenrirDemo::DemoUI& kit, OverlayState& state)
        {
            const FenrirDemo::DemoSection section = kit.SceneSection("Overlay input");
            section.Text("While the demo panel has the pointer or the keyboard, the scene's keys and clicks stop.");
            section.TextEntry("Label", &state.label, "The text over the cube; Space and C type into it");
            section.Range("Spin speed", &state.speed, 0.0f, 5.0f, {}, "How fast the cube turns, in radians a second");
            section.Readout("Spin", [&state] { return std::string(state.paused ? "paused (Space)" : "on (Space)"); });
            section.Readout("Colour",
                            [&state] { return std::format("{} of {} (C)", state.colour + 1, CubeColours.size()); });
            section.Readout("Scene clicks", [&state] { return std::to_string(state.clicks); });

            section.Separator("Input claims");
            const auto claims = [&app] { return app.GetResource<Fenrir::InputClaims>().For(MainWindow); };
            section.Readout("Pointer", [claims] { return YesNo(claims().pointer); });
            section.Readout("Capture", [claims] { return YesNo(claims().pointerCapture); });
            section.Readout("Keyboard", [claims] { return YesNo(claims().keyboardFocus); });
            section.Readout("Text input", [claims] { return YesNo(claims().textInput); });
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            OverlayState state;
            state.shader2D = store.Load<Fenrir::Shader>("builtin:2d", scene).handle;
            // The labels' painter shader, registered once for the app by main(), through Painting::Install.
            state.painterShader = store.Load<Fenrir::Shader>("builtin:painter", scene).handle;
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

            // A floor and a spinning cube to the right of the raw draw list.
            add({2.0f, -0.25f, 0.0f}, {5.0f, 0.5f, 5.0f}, material(0x8A8F99FF));
            state.cubePaint = material(CubeColours[0]);
            state.cube = add({2.0f, 0.9f, 0.0f}, {1.2f, 1.2f, 1.2f}, state.cubePaint);

            Fenrir::Entity light = entities.CreateEntity();
            light.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-0.4f, -0.8f, -0.5f)), Vec3(0.0f, 1.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.2f, 0.22f, 0.25f, 1.0f}});
            scene.AddResource<OverlayState>(std::move(state));
            if (auto* kit = app.TryGetResource<FenrirDemo::DemoUI>(); kit != nullptr)
            {
                AddSection(app, *kit, *scene.GetResource<OverlayState>());
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            OverlayState* state = scene.GetResource<OverlayState>();
            if (state == nullptr)
            {
                return;
            }

            // The scene's own keys and clicks: what the demo panel took never reaches them.
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

            if (auto* overlay = app.TryGetResource<Fenrir::Overlay>(); overlay != nullptr)
            {
                // The labels, in FenrirText, painted into a list of their own over the tests' lists.
                state->labels.Begin(app, MainWindow, state->painterShader);
                SubmitLayers(*overlay, state->labels, *state);
                Fenrir::DrawList2D list;
                AddScissors(list, state->labels, *state);
                AddBlendModes(list, state->labels, *state);
                overlay->Submit(MainWindow, 0, std::move(list));
                state->labels.Submit(*overlay, LabelLayer);
            }
            if (auto* draw = scene.GetResource<Fenrir::DebugDraw>(); draw != nullptr)
            {
                draw->WorldText({2.0f, 1.7f, 0.0f}, state->label, Fenrir::Colour::White);
            }
        }
    } // namespace

    FenrirDemo::DemoScene OverlayScene()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Overlay";
        // None of the three reach the scene while the demo panel has the keyboard (the keys) or the pointer (a click).
        scene.keyRows = {{"Space", "pause or resume the cube's spin, unless the panel has the keyboard"},
                         {"C", "the cube's colour, unless the panel has the keyboard"},
                         {"Left click", "counted as a scene click, unless it lands on the panel"}};
        scene.keys = {Fenrir::Key::Space, Fenrir::Key::C, Fenrir::Key::Space, Fenrir::Key::C};
        scene.camera.position = {0.5f, 1.8f, 6.0f};
        scene.camera.pitch = -0.15f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
