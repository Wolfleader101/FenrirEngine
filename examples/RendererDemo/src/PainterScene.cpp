#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/StopTexture.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirScene/Scene.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Rect2D;

        constexpr Fenrir::WindowId MainWindow{1};
        constexpr float Pi = std::numbers::pi_v<float>;

        /** @brief What the scene paints with, kept as a scene resource; the painter itself is made each frame. */
        struct PainterState
        {
            Fenrir::AssetHandle<Fenrir::Shader> shader;
            Fenrir::AssetHandle<Fenrir::Image> picture;
            std::optional<Fenrir::StopTexture> stops;
            Fenrir::BlendSpace2D blendSpace = Fenrir::BlendSpace2D::Gamma;
            float time = 0.0f;
            bool paused = false;
        };

        Colour Srgb(std::uint32_t rgba)
        {
            return Colour::FromSrgb(rgba);
        }

        // Row 1: a rectangle, a rounded rectangle with four radii, a border with four colours, and an outset and an
        // inset shadow, each on a light card so the shadows read.
        void PaintBoxes(Fenrir::Painter& painter, Fenrir::DebugDraw& draw)
        {
            constexpr float top = 80.0f;
            painter.DrawRect({40, top, 120, 80}, Srgb(0xE04848FF));
            painter.DrawRoundedRect({190, top, 120, 80}, {4, 16, 32, 0}, Srgb(0x3A86FFFF));
            Fenrir::Border border;
            border.widths = {2, 6, 10, 4};
            border.top = Srgb(0xFFBE0BFF);
            border.right = Srgb(0xFB5607FF);
            border.bottom = Srgb(0xFF006EFF);
            border.left = Srgb(0x8338ECFF);
            painter.DrawRoundedRect({340, top, 120, 80}, Fenrir::CornerRadii::All(14), Srgb(0x20242CFF));
            painter.DrawBorder({340, top, 120, 80}, Fenrir::CornerRadii::All(14), border);

            Fenrir::Shadow outset;
            outset.offset = {6, 8};
            outset.blur = 18;
            outset.spread = 2;
            outset.colour = Colour{0.0f, 0.0f, 0.0f, 0.6f};
            painter.DrawShadow({490, top, 120, 80}, Fenrir::CornerRadii::All(12), outset);
            painter.DrawRoundedRect({490, top, 120, 80}, Fenrir::CornerRadii::All(12), Srgb(0xF2F2F2FF));
            painter.DrawRoundedRect({640, top, 120, 80}, Fenrir::CornerRadii::All(12), Srgb(0xF2F2F2FF));
            Fenrir::Shadow inset = outset;
            inset.inset = true;
            painter.DrawShadow({640, top, 120, 80}, Fenrir::CornerRadii::All(12), inset);
            draw.ScreenText({40, top - 24}, "Rectangle, rounded, border, outset and inset shadow", Colour::White);
        }

        // Row 2: a circle, an arc whose sweep breathes, a ring, and three gradients: two stops, five stops through the
        // stop texture, and a radial gradient with its focal point off centre.
        void PaintRound(Fenrir::Painter& painter, Fenrir::DebugDraw& draw, float time)
        {
            constexpr float top = 240.0f;
            painter.DrawCircle({100, top + 50}, 45, Srgb(0x06D6A0FF));
            const float sweep = Pi * (1.0f + 0.8f * std::sin(time));
            painter.DrawArc({250, top + 50}, 38, -Pi / 2.0f, sweep, 12, Srgb(0xFFD166FF));
            painter.DrawRing({400, top + 50}, 45, 30, Srgb(0xEF476FFF));

            Fenrir::LinearGradient two;
            two.start = {490, 0};
            two.end = {610, 0};
            two.stops = {{0.0f, Srgb(0x118AB2FF)}, {1.0f, Srgb(0xEF476FFF)}};
            painter.DrawRoundedRect({490, top, 120, 100}, Fenrir::CornerRadii::All(10), two);

            Fenrir::LinearGradient rainbow;
            rainbow.start = {640, top};
            rainbow.end = {760, top + 100};
            rainbow.stops = {{0.0f, Srgb(0xFF0000FF)},
                             {0.25f, Srgb(0xFFFF00FF)},
                             {0.5f, Srgb(0x00FF00FF)},
                             {0.75f, Srgb(0x0000FFFF)},
                             {1.0f, Srgb(0xFF00FFFF)}};
            painter.DrawRoundedRect({640, top, 120, 100}, Fenrir::CornerRadii::All(10), rainbow);

            Fenrir::RadialGradient radial;
            radial.centre = {850, top + 50};
            radial.radius = 50;
            radial.focal = {850 + 25.0f * std::cos(time), top + 50 - 25.0f * std::sin(time)};
            radial.stops = {{0.0f, Colour::White}, {1.0f, Srgb(0x3A0CA3FF)}};
            painter.DrawCircle({850, top + 50}, 50, radial);
            draw.ScreenText({40, top - 24}, "Circle, arc, ring; two-stop, five-stop and focal radial gradients",
                            Colour::White);
        }

        // Row 3: the icon stretched, nine-sliced into a wide panel, and as a repeating pattern in a circle; then a
        // rotating rounded clip over a gradient.
        void PaintImagesAndClips(Fenrir::Painter& painter, Fenrir::DebugDraw& draw, const PainterState& state)
        {
            constexpr float top = 420.0f;
            Fenrir::ImageDraw icon;
            icon.image = state.picture;
            painter.DrawImage({40, top, 100, 100}, icon);
            Fenrir::ImageDraw sliced = icon;
            sliced.nineSlice = Fenrir::NineSlice{Fenrir::Sides::All(64), {256, 256}};
            painter.DrawImage({170, top, 260, 100}, sliced);
            Fenrir::ImagePattern pattern;
            pattern.image = state.picture;
            pattern.rect = {0, 0, 32, 32};
            pattern.sampler.wrapU = Fenrir::SamplerWrap::Repeat;
            pattern.sampler.wrapV = Fenrir::SamplerWrap::Repeat;
            painter.DrawCircle({510, top + 50}, 50, pattern);

            painter.Save();
            painter.Translate({700, top + 50});
            painter.Rotate(state.time * 0.5f);
            painter.ClipRoundedRect({-60, -40, 120, 80}, Fenrir::CornerRadii::All(20));
            painter.Rotate(-state.time * 0.5f);
            Fenrir::LinearGradient behind;
            behind.start = {-80, 0};
            behind.end = {80, 0};
            behind.stops = {{0.0f, Srgb(0xFF9F1CFF)}, {1.0f, Srgb(0x2EC4B6FF)}};
            painter.DrawRect({-80, -80, 160, 160}, behind);
            painter.Restore();
            draw.ScreenText({40, top - 24}, "Image, nine-slice, image pattern; a rotating rounded clip", Colour::White);
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            PainterState state;
            // Registered once for the app by main(), through Painting::Install.
            state.shader = store.Load<Fenrir::Shader>("builtin:painter", scene).handle;
            const Fenrir::LoadResult<Fenrir::Image> picture = store.Load<Fenrir::Image>("icons/fenrir.png", scene);
            if (!picture.Ok())
            {
                app.Logger()->Error("Painter: the icon did not load from the asset root");
            }
            state.picture = picture.handle;
            constexpr std::uint32_t rows = 8;
            state.stops.emplace(store.Add<Fenrir::Image>(Fenrir::StopTexture::Blank(rows), scene), rows);
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.08f, 0.09f, 0.11f, 1.0f}});
            scene.AddResource<PainterState>(std::move(state));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            PainterState* state = scene.GetResource<PainterState>();
            auto* overlay = app.TryGetResource<Fenrir::Overlay>();
            auto* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || overlay == nullptr || draw == nullptr)
            {
                return;
            }
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::G))
            {
                const bool gamma = state->blendSpace == Fenrir::BlendSpace2D::Gamma;
                state->blendSpace = gamma ? Fenrir::BlendSpace2D::Linear : Fenrir::BlendSpace2D::Gamma;
                app.Logger()->Info("G: the painter blends in {}", gamma ? "linear space" : "gamma space");
            }
            if (keyboard.JustPressed(Fenrir::Key::Space))
            {
                state->paused = !state->paused;
                app.Logger()->Info("Space: the animation is {}", state->paused ? "paused" : "on");
            }
            if (!state->paused)
            {
                state->time += static_cast<float>(app.GetTime().deltaTime);
            }

            Fenrir::DrawList2D list;
            Fenrir::PainterConfig config;
            config.shader = state->shader;
            config.blendSpace = state->blendSpace;
            config.stops = &*state->stops;
            Fenrir::Painter painter(list, config);
            PaintBoxes(painter, *draw);
            PaintRound(painter, *draw, state->time);
            PaintImagesAndClips(painter, *draw, *state);
            // The gradients' rows, written before the list is drawn; the stop texture starts the next frame empty.
            app.GetResource<Fenrir::AssetStore>().Replace(state->stops->GetImage(), state->stops->TakeImageData());
            overlay->Submit(MainWindow, 0, std::move(list));
            draw->ScreenText({40, 580},
                             state->blendSpace == Fenrir::BlendSpace2D::Gamma
                                 ? "Blending in gamma space (G for linear)"
                                 : "Blending in linear space (G for gamma)",
                             Colour::White);
        }
    } // namespace

    FenrirDemo::DemoScene PainterScene()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Painter";
        scene.keyHelp =
            "G: blend in gamma or linear space. Space: pause or resume the arc, the focal point and the clip.";
        scene.keys = {Fenrir::Key::G, Fenrir::Key::Space, Fenrir::Key::G, Fenrir::Key::Space};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
