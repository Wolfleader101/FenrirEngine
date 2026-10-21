// SPIKE (prototype/fenrir-renderer-spike, throwaway): a lit mesh, a camera, a skybox and debug lines through the
// proposed seam. Game code here sees only the public facades: no GL, SDL, device or core header.
//
// Hold the right mouse button to look, WASD to move, Q/E down/up, Shift faster. F1 toggles debug lines, F2 the
// picture-in-picture camera, V cycles vsync, Esc quits.
//
// FENRIR_SPIKE_BACKEND=null      the null backend, no window (a headless check)
// FENRIR_SPIKE_FRAMES=N          run N frames of 1/60 s, then print the renderer's stats and exit
// FENRIR_SPIKE_SCREENSHOT=a.bmp  with FENRIR_SPIKE_FRAMES, write the last frame's backbuffer

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirWindow/Window.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

using namespace Fenrir;

namespace
{
    Math::Vec3 SkyRadiance(const Math::Vec3& d, const Math::Vec3& toSun)
    {
        const float up = d.y;
        Math::Vec3 colour;
        if (up >= 0.0f)
            colour = glm::mix(Math::Vec3(0.75f, 0.85f, 0.95f), Math::Vec3(0.20f, 0.40f, 0.80f), std::sqrt(up));
        else
            colour = glm::mix(Math::Vec3(0.35f, 0.32f, 0.28f), Math::Vec3(0.15f, 0.12f, 0.10f), std::sqrt(-up));
        const float sun = glm::dot(d, toSun);
        if (sun > 0.9995f)
            colour = Math::Vec3(4.0f);
        else if (sun > 0.99f)
            colour += Math::Vec3(1.0f, 0.9f, 0.6f) * ((sun - 0.99f) / 0.0095f);
        // A red band towards +X and a green one towards -Z, so the cube's orientation can be checked by eye.
        if (std::abs(up) < 0.02f && d.x > 0.95f)
            colour = Math::Vec3(0.9f, 0.1f, 0.1f);
        if (std::abs(up) < 0.02f && d.z < -0.95f)
            colour = Math::Vec3(0.1f, 0.9f, 0.1f);
        return colour;
    }

    std::uint8_t ToSrgb8(float linear)
    {
        linear = std::clamp(linear, 0.0f, 1.0f);
        const float s = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::lround(s * 255.0f));
    }

    // Faces +X -X +Y -Y +Z -Z, each row from the top: the cube-map convention every API shares.
    ImageData SkyCubemap(int size, const Math::Vec3& toSun)
    {
        ImageData image;
        image.width = image.height = size;
        image.cube = true;
        image.format = ImageFormat::Rgba8Srgb;
        image.pixels.resize(static_cast<std::size_t>(size * size * 4 * 6));
        std::size_t at = 0;
        for (int face = 0; face < 6; ++face)
        {
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const float sc = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
                    const float tc = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
                    Math::Vec3 d;
                    switch (face)
                    {
                    case 0: d = {1, -tc, -sc}; break;
                    case 1: d = {-1, -tc, sc}; break;
                    case 2: d = {sc, 1, tc}; break;
                    case 3: d = {sc, -1, -tc}; break;
                    case 4: d = {sc, -tc, 1}; break;
                    default: d = {-sc, -tc, -1}; break;
                    }
                    const Math::Vec3 c = SkyRadiance(glm::normalize(d), toSun);
                    image.pixels[at++] = ToSrgb8(c.r);
                    image.pixels[at++] = ToSrgb8(c.g);
                    image.pixels[at++] = ToSrgb8(c.b);
                    image.pixels[at++] = 255;
                }
            }
        }
        return image;
    }

    ImageData Checker(int size, int squares)
    {
        ImageData image;
        image.width = image.height = size;
        image.pixels.resize(static_cast<std::size_t>(size * size * 4));
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const bool light = ((x * squares / size) + (y * squares / size)) % 2 == 0;
                const std::uint8_t v = light ? 200 : 90;
                std::uint8_t* p = &image.pixels[static_cast<std::size_t>((y * size + x) * 4)];
                p[0] = v;
                p[1] = v;
                p[2] = v;
                p[3] = 255;
            }
        }
        // Mark the texture's top-left corner red, so its orientation shows on the ground.
        for (int y = 0; y < size / 8; ++y)
            for (int x = 0; x < size / 8; ++x)
            {
                std::uint8_t* p = &image.pixels[static_cast<std::size_t>((y * size + x) * 4)];
                p[0] = 220;
                p[1] = 40;
                p[2] = 40;
            }
        return image;
    }

    void WriteBmp(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h)
    {
        std::ofstream out(path, std::ios::binary);
        const int rowBytes = (w * 3 + 3) & ~3;
        const int dataSize = rowBytes * h;
        auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
        out.write("BM", 2);
        u32(static_cast<std::uint32_t>(54 + dataSize));
        u32(0);
        u32(54);
        u32(40);
        u32(static_cast<std::uint32_t>(w));
        u32(static_cast<std::uint32_t>(h));
        u16(1);
        u16(24);
        u32(0);
        u32(static_cast<std::uint32_t>(dataSize));
        u32(2835);
        u32(2835);
        u32(0);
        u32(0);
        std::vector<char> row(static_cast<std::size_t>(rowBytes), 0);
        for (int y = h - 1; y >= 0; --y) // BMP rows run bottom up
        {
            for (int x = 0; x < w; ++x)
            {
                const std::uint8_t* p = &rgba[static_cast<std::size_t>((y * w + x) * 4)];
                row[static_cast<std::size_t>(x * 3 + 0)] = static_cast<char>(p[2]);
                row[static_cast<std::size_t>(x * 3 + 1)] = static_cast<char>(p[1]);
                row[static_cast<std::size_t>(x * 3 + 2)] = static_cast<char>(p[0]);
            }
            out.write(row.data(), rowBytes);
        }
    }

    Entity Spawn(Scene& scene, const Math::Vec3& pos, AssetHandle<MeshData> mesh, AssetHandle<MaterialData> material,
                 const Math::Vec3& scale = Math::Vec3(1.0f))
    {
        Entity e = scene.GetEntityList().CreateEntity();
        e.AddComponent<Transform>(pos, Math::Quat(1, 0, 0, 0), scale);
        e.AddComponent<MeshRenderer>(MeshRenderer{mesh, material, true});
        return e;
    }

    AssetHandle<MaterialData> Lit(Assets& assets, Colour colour, float metallic, float roughness)
    {
        MaterialData m;
        m.params["baseColour"] = colour;
        m.params["metallic"] = metallic;
        m.params["roughness"] = roughness;
        return assets.Add(m);
    }

    struct FlyCamera
    {
        Entity entity;
        float yaw = 0.0f;
        float pitch = -0.25f;
        bool looking = false;
    };
} // namespace

int main()
{
    const char* backendEnv = std::getenv("FENRIR_SPIKE_BACKEND");
    const bool null = backendEnv != nullptr && std::string(backendEnv) == "null";
    const char* framesEnv = std::getenv("FENRIR_SPIKE_FRAMES");
    const int frames = framesEnv != nullptr ? std::atoi(framesEnv) : 0;
    const char* screenshot = std::getenv("FENRIR_SPIKE_SCREENSHOT");

    App app(std::make_unique<ConsoleLogger>());
    Input::Install(app);
    DebugDraw::Install(app);
    Assets& assets = Assets::Install(app);
    std::optional<Window> window;
    if (!null)
    {
        WindowConfig config;
        config.title = "Fenrir renderer spike";
        window = Window::Install(app, config);
    }
    RendererConfig rendererConfig;
    rendererConfig.backend = null ? GraphicsBackend::Null : GraphicsBackend::OpenGL;
    Renderer renderer = Renderer::Install(app, rendererConfig);
    if (!renderer.IsValid())
    {
        return 1;
    }

    Scene& scene = app.GetActiveScene();
    EntityList& entities = scene.GetEntityList();

    // Light first: the sky's sun is drawn where it comes from.
    Entity sun = entities.CreateEntity();
    const Math::Quat sunWorld = glm::angleAxis(0.6f, Math::Vec3(0, 1, 0)) * glm::angleAxis(-0.9f, Math::Vec3(1, 0, 0));
    sun.AddComponent<Transform>(Math::Vec3(0, 10, 0), sunWorld, Math::Vec3(1));
    sun.AddComponent<DirectionalLight>(DirectionalLight{{1.0f, 0.95f, 0.85f, 1}, 3.0f});
    const Math::Vec3 toSun = -(sunWorld * Math::Vec3(0, 0, -1));

    scene.AddResource<AmbientLight>(AmbientLight{{0.25f, 0.3f, 0.4f, 1}, 0.4f});
    Sky& sky = scene.AddResource<Sky>();
    sky.cubemap = assets.Add(SkyCubemap(256, glm::normalize(toSun)));
    scene.AddResource<DebugDraw>();

    const AssetHandle<MeshData> cube = assets.Add(Primitives::Cube(0.5f));
    const AssetHandle<MeshData> sphere = assets.Add(Primitives::Sphere(0.5f, 48, 24));
    const AssetHandle<MeshData> plane = assets.Add(Primitives::Plane(12.0f, 12.0f));

    MaterialData groundMaterial;
    groundMaterial.params["baseColourTexture"] = assets.Add(Checker(256, 8));
    groundMaterial.params["roughness"] = 0.9f;
    Spawn(scene, {0, 0, 0}, plane, assets.Add(groundMaterial));

    // Metallic across, roughness up.
    for (int i = 0; i < 5; ++i)
    {
        const float t = static_cast<float>(i) / 4.0f;
        Spawn(scene, {-4.0f + 2.0f * static_cast<float>(i), 0.5f, 0}, sphere,
              Lit(assets, {1.0f, 0.78f, 0.35f, 1}, 1.0f, 0.1f + 0.8f * t));
        Spawn(scene, {-4.0f + 2.0f * static_cast<float>(i), 0.5f, 2.0f}, sphere,
              Lit(assets, {0.8f, 0.1f, 0.1f, 1}, 0.0f, 0.1f + 0.8f * t));
    }

    // A spinning parent with an orbiting child: drawn through GetWorldMatrix.
    Entity pivot = entities.CreateEntity();
    pivot.AddComponent<Transform>(Math::Vec3(0, 1.5f, -3.0f), Math::Quat(1, 0, 0, 0), Math::Vec3(1));
    Entity hub = Spawn(scene, {0, 1.5f, -3.0f}, cube, Lit(assets, {0.2f, 0.5f, 0.9f, 1}, 0.0f, 0.4f));
    Entity orbiter = Spawn(scene, {2.0f, 0, 0}, cube, Lit(assets, {0.9f, 0.9f, 0.9f, 1}, 0.0f, 0.3f), Math::Vec3(0.5f));
    pivot.AddChild(orbiter);

    MaterialData unlit;
    unlit.shader = "builtin:unlit";
    unlit.params["baseColour"] = Colour{0.1f, 0.9f, 0.3f, 1};
    Spawn(scene, {4.0f, 0.5f, -3.0f}, cube, assets.Add(unlit));
    Spawn(scene, {-4.0f, 0.5f, -3.0f}, cube, {}); // no material: magenta fallback

    MaterialData glass;
    glass.blend = BlendMode::AlphaBlend;
    glass.params["baseColour"] = Colour{0.3f, 0.6f, 1.0f, 0.35f};
    glass.params["roughness"] = 0.1f;
    Spawn(scene, {0, 1.0f, 4.0f}, sphere, assets.Add(glass), Math::Vec3(2.0f));

    FlyCamera fly;
    fly.entity = entities.CreateEntity();
    fly.entity.AddComponent<Transform>(Math::Vec3(0, 3.5f, 11.0f), Math::Quat(1, 0, 0, 0), Math::Vec3(1));
    fly.entity.AddComponent<Camera>();

    Entity pip = entities.CreateEntity();
    pip.AddComponent<Transform>(Math::Vec3(0, 18, 0.01f), glm::angleAxis(-1.5707963f, Math::Vec3(1, 0, 0)),
                                Math::Vec3(1));
    Camera pipCamera;
    pipCamera.order = 1;
    pipCamera.fovY = 0.9f;
    pipCamera.viewport[0] = 0.72f;
    pipCamera.viewport[1] = 0.03f;
    pipCamera.viewport[2] = 0.25f;
    pipCamera.viewport[3] = 0.25f;
    pipCamera.drawDebug = false;
    pip.AddComponent<Camera>(pipCamera);

    bool drawDebug = true;
    int vsync = 1;
    Renderer* rendererPtr = &renderer;
    std::vector<Entity> drawn;
    entities.ForEach<MeshRenderer>(
        [&](auto id, MeshRenderer&) { drawn.push_back(entities.GetEntity(static_cast<std::uint32_t>(id))); });

    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& a) {
        const float dt = static_cast<float>(a.GetTime().deltaTime);
        const Keyboard* keys = a.GetResource<Keyboard>();
        const Mouse* mouse = a.GetResource<Mouse>();
        if (keys != nullptr && mouse != nullptr)
        {
            if (keys->JustPressed(Key::Escape))
                a.Stop();
            if (keys->JustPressed(Key::F1))
                drawDebug = !drawDebug;
            if (keys->JustPressed(Key::F2))
                pip.GetComponent<Camera>().enabled = !pip.GetComponent<Camera>().enabled;
            if (keys->JustPressed(Key::V))
            {
                vsync = (vsync + 1) % 3;
                rendererPtr->SetVSync(static_cast<VSync>(vsync));
                std::printf("vsync %s\n", vsync == 0 ? "Off" : vsync == 1 ? "On" : "Adaptive");
            }
            const bool look = mouse->buttons[1];
            if (look != fly.looking && window)
            {
                Window::SetRelativeMouseMode(a, Window::MainWindow(), look);
                fly.looking = look;
            }
            if (look)
            {
                fly.yaw -= mouse->deltaX * 0.003f;
                fly.pitch = std::clamp(fly.pitch - mouse->deltaY * 0.003f, -1.5f, 1.5f);
            }
            Transform& t = fly.entity.GetComponent<Transform>();
            t.rot = glm::angleAxis(fly.yaw, Math::Vec3(0, 1, 0)) * glm::angleAxis(fly.pitch, Math::Vec3(1, 0, 0));
            const Math::Vec3 forward = t.rot * Math::Vec3(0, 0, -1);
            const Math::Vec3 right = t.rot * Math::Vec3(1, 0, 0);
            Math::Vec3 move(0.0f);
            if (keys->IsDown(Key::W)) move += forward;
            if (keys->IsDown(Key::S)) move -= forward;
            if (keys->IsDown(Key::D)) move += right;
            if (keys->IsDown(Key::A)) move -= right;
            if (keys->IsDown(Key::E)) move.y += 1.0f;
            if (keys->IsDown(Key::Q)) move.y -= 1.0f;
            const float speed = keys->IsDown(Key::LeftShift) ? 12.0f : 4.0f;
            t.pos += move * speed * dt;
        }

        static float time = 0.0f;
        time += dt;
        pivot.GetComponent<Transform>().rot = glm::angleAxis(time * 0.8f, Math::Vec3(0, 1, 0));
        hub.GetComponent<Transform>().rot = glm::angleAxis(time * 0.5f, glm::normalize(Math::Vec3(1, 1, 0)));

        if (!drawDebug)
            return;
        DebugDraw& debug = *scene.GetResource<DebugDraw>();
        debug.Grid(12.0f, 1.0f, {0.25f, 0.25f, 0.25f, 1});
        debug.Axes(Math::Mat4(1.0f), 2.0f);
        for (const Entity& e : drawn)
        {
            Math::Vec3 lo, hi;
            if (rendererPtr->GetWorldBounds(e, lo, hi))
                debug.WireBox(lo, hi, {1.0f, 0.85f, 0.2f, 1});
        }
        debug.WireSphere(Math::Vec3(GetWorldMatrix(orbiter)[3]), 0.6f, {0.2f, 1.0f, 1.0f, 1});
        debug.Line(Math::Vec3(0, 4, 0), Math::Vec3(0, 4, 0) + (sunWorld * Math::Vec3(0, 0, -1)) * 2.0f,
                   {1.0f, 1.0f, 0.0f, 1});
    }});

    if (frames <= 0)
    {
        app.Run();
        return 0;
    }
    for (int i = 0; i < frames && app.IsRunning(); ++i)
    {
        app.RunFrame(1.0 / 60.0);
    }
    const RendererStats stats = renderer.Stats();
    std::printf("items %u draws %u passes %u culled %u pipelines %u buffers %u textures %u\n", stats.items,
                stats.draws, stats.passes, stats.culledPasses, stats.pipelines, stats.liveBuffers, stats.liveTextures);
    for (const std::string& pass : stats.passOrder)
        std::printf("  %s\n", pass.c_str());
    if (screenshot != nullptr)
    {
        int w = 0, h = 0;
        const std::vector<std::uint8_t> pixels = renderer.ReadBackbuffer(w, h);
        if (!pixels.empty())
        {
            WriteBmp(screenshot, pixels, w, h);
            std::printf("wrote %s (%dx%d)\n", screenshot, w, h);
        }
    }
    app.RunExit();
    return 0;
}
