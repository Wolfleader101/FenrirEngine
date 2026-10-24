#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::DebugDraw;
        using Fenrir::DepthMode;
        using Fenrir::Math::Vec3;

        constexpr float Pi = 3.14159265f;
        // The groups the number keys show and hide, and the one F adds to and X clears.
        constexpr std::array<const char*, 3> ToggledGroups{"Wire", "Solid", "Labels"};
        constexpr const char* ForeverGroup = "Forever";
        // The two blocks of shapes, seven to a row, three rows high: the depth-tested block on the left and the on-top
        // block on the right, both behind a wall that hides their bottom row.
        constexpr float TestedBlockX = -21.0f;
        constexpr float OnTopBlockX = 3.0f;
        constexpr float Spacing = 3.0f;
        constexpr float RowHeight = 2.8f;
        constexpr int Columns = 7;

        /** @brief What the scene keeps between frames, as a scene resource. */
        struct DebugDrawState
        {
            Fenrir::Entity orbiter;
            float seconds = 0.0f;
            std::uint32_t frames = 0;
            int foreverMarks = 0;
            // Whether the orbiter's forever circle is drawn: a scene made this frame has no DebugDraw until the next.
            bool circleDrawn = false;
        };

        /** @brief A reversed-Z perspective view-projection (ADR-0026) looking along -Z from `eye`, for a frustum. */
        Fenrir::Math::Mat4 FrustumOf(const Vec3& eye)
        {
            constexpr float near = 0.4f;
            const float focal = 1.0f / std::tan(0.5f * 0.6f);
            Fenrir::Math::Mat4 projection(0.0f);
            projection[0][0] = focal;
            projection[1][1] = focal;
            projection[2][3] = -1.0f;
            projection[3][2] = near;
            return projection * glm::translate(Fenrir::Math::Mat4(1.0f), -eye);
        }

        /**
         * @brief A block of every wire and solid shape from `left`, seven to a row and a label over each, in `depth`
         * mode; the on-top block is drawn in warmer colours.
         */
        void DrawBlock(DebugDraw& draw, float left, DepthMode depth)
        {
            const bool onTop = depth == DepthMode::OnTop;
            const Colour wire = onTop ? Colour::FromSrgb(0xFFE070FF) : Colour::FromSrgb(0x40E0FFFF);
            const Colour solid = onTop ? Colour{1.0f, 0.5f, 0.1f, 0.5f} : Colour{0.2f, 0.6f, 1.0f, 0.5f};
            const Fenrir::ShapeOptions options{.group = "Wire", .thickness = 2.0f, .depth = depth};
            const Fenrir::ShapeOptions solidOptions{.group = "Solid", .depth = depth};
            const Fenrir::TextOptions label{.group = "Labels", .size = 14.0f};
            int index = 0;
            const auto next = [&](const char* name) {
                const Vec3 at(left + Spacing * static_cast<float>(index % Columns),
                              1.0f + RowHeight * static_cast<float>(index / Columns), 0.0f);
                ++index;
                draw.WorldText(at + Vec3(0.0f, 1.25f, 0.0f), name, Colour::White, label);
                return at;
            };

            Vec3 at = next("Line");
            draw.Line(at - Vec3(0.8f, 0.8f, 0.0f), at + Vec3(0.8f, 0.8f, 0.0f), wire, options);
            at = next("Ray");
            draw.Ray(at - Vec3(0.8f, 0.0f, 0.0f), Vec3(1.6f, 0.6f, 0.0f), wire, options);
            at = next("Arrow");
            draw.Arrow(at - Vec3(0.0f, 0.8f, 0.0f), at + Vec3(0.0f, 0.8f, 0.0f), 0.4f, wire, options);
            at = next("Polyline");
            const std::array<Vec3, 5> star{at + Vec3(0.0f, 0.9f, 0.0f), at + Vec3(0.55f, -0.75f, 0.0f),
                                           at + Vec3(-0.85f, 0.3f, 0.0f), at + Vec3(0.85f, 0.3f, 0.0f),
                                           at + Vec3(-0.55f, -0.75f, 0.0f)};
            draw.Polyline(star, true, wire, options);
            at = next("Points");
            for (int i = 0; i < 3; ++i)
            {
                draw.Point(at + Vec3(static_cast<float>(i - 1) * 0.6f, 0.0f, 0.0f), 4.0f + 4.0f * static_cast<float>(i),
                           wire, options);
            }
            at = next("Box");
            draw.Box(Fenrir::Math::AABB{at - Vec3(0.7f), at + Vec3(0.7f)}, wire, options);
            at = next("Oriented box");
            draw.Box(at, Vec3(0.8f, 0.5f, 0.4f), Fenrir::Math::AngleAxis(0.6f, Vec3(0.0f, 0.0f, 1.0f)), wire, options);
            at = next("Sphere");
            draw.Sphere(at, 0.8f, wire, options);
            at = next("Capsule");
            draw.Capsule(at - Vec3(0.0f, 0.5f, 0.0f), at + Vec3(0.0f, 0.5f, 0.0f), 0.4f, wire, options);
            at = next("Cylinder");
            draw.Cylinder(at - Vec3(0.0f, 0.7f, 0.0f), at + Vec3(0.0f, 0.7f, 0.0f), 0.5f, wire, options);
            at = next("Cone");
            draw.Cone(at + Vec3(0.0f, 0.8f, 0.0f), at - Vec3(0.0f, 0.8f, 0.0f), 0.6f, wire, options);
            at = next("Circle");
            draw.Circle(at, Vec3(0.0f, 0.0f, 1.0f), 0.8f, wire, options);
            at = next("Arc");
            draw.Arc(at, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.8f, 0.0f, 0.0f), 1.5f * Pi, wire, options);
            at = next("Plane");
            draw.Plane(at, glm::normalize(Vec3(0.0f, 0.3f, 1.0f)), 0.7f, wire, options);
            at = next("Grid");
            draw.Grid(at, Fenrir::Math::AngleAxis(0.5f * Pi, Vec3(1.0f, 0.0f, 0.0f)), 4, 0.4f, wire, options);
            at = next("Axes");
            draw.Axes(glm::translate(Fenrir::Math::Mat4(1.0f), at), 0.8f, options);
            at = next("Frustum");
            draw.Frustum(FrustumOf(at + Vec3(0.0f, 0.0f, 0.9f)), wire, options, 0.25f);

            // Then the solids, alpha-blended.
            at = next("Solid box");
            draw.SolidBox(Fenrir::Math::AABB{at - Vec3(0.6f), at + Vec3(0.6f)}, solid, solidOptions);
            at = next("Solid sphere");
            draw.SolidSphere(at, 0.7f, solid, solidOptions);
            at = next("Solid capsule");
            draw.SolidCapsule(at - Vec3(0.0f, 0.4f, 0.0f), at + Vec3(0.0f, 0.4f, 0.0f), 0.4f, solid, solidOptions);
            at = next("Triangles");
            const std::array<Vec3, 6> triangles{at + Vec3(-0.8f, -0.6f, 0.0f), at + Vec3(0.0f, -0.6f, 0.0f),
                                                at + Vec3(-0.4f, 0.6f, 0.0f),  at + Vec3(0.0f, -0.6f, 0.0f),
                                                at + Vec3(0.8f, -0.6f, 0.0f),  at + Vec3(0.4f, 0.6f, 0.0f)};
            draw.Triangles(triangles, solid, solidOptions);
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const Fenrir::LoadResult<Fenrir::Mesh> cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const Fenrir::LoadResult<Fenrir::Mesh> sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                app.Logger()->Error("Debug draw: the cube or sphere did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto material = [&](std::uint32_t srgb) {
                const auto made = store.CreateMaterial(lit, scene);
                store.SetColour(made, "baseColour", Colour::FromSrgb(srgb));
                return made;
            };
            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto add = [&](Fenrir::AssetHandle<Fenrir::Mesh> mesh, const Vec3& position, const Vec3& scale,
                                 Fenrir::AssetHandle<Fenrir::Material> paint) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, paint});
                return entity;
            };

            // A floor, and a low wall in front of both rows: it hides the bottom of the depth-tested shapes, and the
            // on-top ones show through it.
            add(cube.handle, {0.0f, -0.25f, 0.0f}, {50.0f, 0.5f, 12.0f}, material(0x6E6A64FF));
            add(cube.handle, {0.0f, 0.75f, 2.0f}, {50.0f, 1.5f, 0.3f}, material(0xB05A3CFF));
            DebugDrawState state;
            state.orbiter = add(sphere.handle, {8.0f, 10.0f, 0.0f}, Vec3(0.5f), material(0xF0D040FF));

            Fenrir::Entity light = entities.CreateEntity();
            light.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-0.4f, -1.0f, -0.5f)), Vec3(0.0f, 1.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();

            scene.AddResource<DebugDrawState>(state);
        }

        /** @brief The orbiter's circle: drawn once, kept forever and following it (relativeTo), so it goes with it. */
        void DrawOrbiterCircle(DebugDraw& draw, DebugDrawState& state)
        {
            if (state.circleDrawn || !state.orbiter.IsValid())
            {
                return;
            }
            draw.Circle(
                Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.9f, Colour::FromSrgb(0xF0D040FF),
                {.group = "Orbiter", .duration = DebugDraw::Forever, .relativeTo = state.orbiter, .thickness = 3.0f});
            state.circleDrawn = true;
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            DebugDrawState* state = scene.GetResource<DebugDrawState>();
            DebugDraw* draw = scene.GetResource<DebugDraw>();
            if (state == nullptr || draw == nullptr)
            {
                return;
            }
            DrawOrbiterCircle(*draw, *state);
            state->seconds += static_cast<float>(app.GetTime().deltaTime);
            ++state->frames;

            // The orbiter circles above the rows; shapes relative to it follow it.
            const float angle = state->seconds * 0.6f;
            if (state->orbiter.IsValid())
            {
                auto& transform = state->orbiter.GetComponent<Fenrir::Transform>();
                transform.pos = Vec3(8.0f * std::cos(angle), 10.0f, 3.0f * std::sin(angle));
                transform.rot = Fenrir::Math::AngleAxis(angle * 2.0f, Vec3(0.0f, 1.0f, 0.0f));
                draw->Axes(Fenrir::Math::Mat4(1.0f), 1.5f,
                           {.group = "Orbiter", .relativeTo = state->orbiter, .thickness = 2.0f});
                draw->Box(Fenrir::Math::AABB{Vec3(-0.6f), Vec3(0.6f)}, Colour::White,
                          {.group = "Orbiter", .relativeTo = state->orbiter});
                draw->WorldText(Vec3(0.0f, 1.2f, 0.0f), "relativeTo the orbiter", Colour::FromSrgb(0xF0D040FF),
                                {.group = "Orbiter", .relativeTo = state->orbiter});
            }

            // Duration 0: drawn every frame, gone by the next.
            DrawBlock(*draw, TestedBlockX, DepthMode::Tested);
            DrawBlock(*draw, OnTopBlockX, DepthMode::OnTop);
            const float titleX = 0.5f * Spacing * static_cast<float>(Columns - 1);
            const float titleY = 1.0f + 3.0f * RowHeight;
            draw->WorldText({TestedBlockX + titleX, titleY, 0.0f}, "Depth tested: the wall hides the bottom row",
                            Colour::White, {.size = 18.0f});
            draw->WorldText({OnTopBlockX + titleX, titleY, 0.0f}, "On top: drawn through the wall", Colour::White,
                            {.size = 18.0f});

            draw->ScreenText({16.0f, 120.0f},
                             std::format("Screen text: frame {}, {:.1f} s. Accents: café, naïve, Ærø. Not Latin-1: €",
                                         state->frames, state->seconds),
                             Colour::White);
            draw->Message(1,
                          std::format("Keyed message 1: the orbiter is at {:.0f} degrees",
                                      std::fmod(angle * 180.0f / Pi, 360.0f)),
                          Colour::FromSrgb(0x80FF80FF));
            std::string groups = "Keyed message 2: groups";
            for (std::size_t i = 0; i < ToggledGroups.size(); ++i)
            {
                groups += std::format(" {}={} {}", i + 1, ToggledGroups.at(i),
                                      draw->IsGroupShown(ToggledGroups.at(i)) ? "on" : "off");
            }
            draw->Message(2, groups, Colour::FromSrgb(0x80C0FFFF));

            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            constexpr std::array<Fenrir::Key, 3> groupKeys{Fenrir::Key::Num1, Fenrir::Key::Num2, Fenrir::Key::Num3};
            for (std::size_t i = 0; i < groupKeys.size(); ++i)
            {
                if (keyboard.JustPressed(groupKeys.at(i)))
                {
                    const char* group = ToggledGroups.at(i);
                    if (draw->IsGroupShown(group))
                    {
                        draw->HideGroup(group);
                    }
                    else
                    {
                        draw->ShowGroup(group);
                    }
                    app.Logger()->Info("{}: the {} group is {}", i + 1, group,
                                       draw->IsGroupShown(group) ? "shown" : "hidden");
                }
            }
            if (keyboard.JustPressed(Fenrir::Key::T))
            {
                // A positive duration: three seconds, wherever the orbiter is now.
                const Vec3 at = state->orbiter.IsValid() ? state->orbiter.GetComponent<Fenrir::Transform>().pos
                                                         : Vec3(8.0f, 10.0f, 0.0f);
                draw->Sphere(at, 1.2f, Colour::FromSrgb(0xFF60C0FF), {.duration = 3.0f, .thickness = 3.0f});
                draw->WorldText(at + Vec3(0.0f, 1.6f, 0.0f), "3 seconds", Colour::FromSrgb(0xFF60C0FF),
                                {.duration = 3.0f});
                draw->Message("T: a sphere for three seconds (an unkeyed message, for three seconds too)",
                              Colour::FromSrgb(0xFF60C0FF), {.duration = 3.0f});
                app.Logger()->Info("T: a sphere for three seconds");
            }
            if (keyboard.JustPressed(Fenrir::Key::F))
            {
                const Vec3 at(-15.0f + 3.0f * static_cast<float>(state->foreverMarks % 11), 12.0f, -3.0f);
                ++state->foreverMarks;
                draw->Arrow(at + Vec3(0.0f, 1.0f, 0.0f), at, 0.3f, Colour::FromSrgb(0xA0FF40FF),
                            {.group = ForeverGroup, .duration = DebugDraw::Forever, .thickness = 3.0f});
                draw->WorldText(at + Vec3(0.0f, 1.3f, 0.0f), std::format("forever #{}", state->foreverMarks),
                                Colour::FromSrgb(0xA0FF40FF), {.group = ForeverGroup, .duration = DebugDraw::Forever});
                app.Logger()->Info("F: forever mark {}", state->foreverMarks);
            }
            if (keyboard.JustPressed(Fenrir::Key::X))
            {
                draw->Clear(ForeverGroup);
                state->foreverMarks = 0;
                app.Logger()->Info("X: the {} group is cleared", ForeverGroup);
            }
        }
    } // namespace

    FenrirDemo::DemoScene DebugDrawing()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Debug draw";
        scene.keyHelp = "1, 2, 3: show or hide the Wire, Solid and Labels groups. T: a sphere for three seconds. F: an "
                        "arrow kept forever. X: clear the forever arrows.";
        scene.keys = {Fenrir::Key::Num1, Fenrir::Key::Num2, Fenrir::Key::Num3, Fenrir::Key::T,    Fenrir::Key::F,
                      Fenrir::Key::F,    Fenrir::Key::X,    Fenrir::Key::Num1, Fenrir::Key::Num2, Fenrir::Key::Num3};
        scene.camera.position = {0.0f, 6.0f, 27.0f};
        scene.camera.pitch = -0.05f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }

    FenrirDemo::DemoScene DebugDrawingCSharp()
    {
        // The C++ scene's name, keys and camera; C# spawns, draws and runs everything else.
        FenrirDemo::DemoScene scene = DebugDrawing();
        scene.name = "Debug draw (C#)";
        scene.spawn = nullptr;
        scene.update = nullptr;
        scene.csharpScene = DebugDrawingCSharpScene;
        return scene;
    }
} // namespace RendererDemo
