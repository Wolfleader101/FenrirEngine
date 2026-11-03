// Shadow pools (ADR-0050) on the null device, through their own interface: the budget's warning, given once, the maps
// made only when a light casts, made again when the budget changes, and the map that lights everything bound
// otherwise.

#include <array>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LightShadows.hpp"
#include "Core/PointShadows.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SpotShadows.hpp"
#include "Device/GraphicsDevice.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::CreateNullDevice;
using Fenrir::Graphics::TextureDesc;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Graphics::TextureUsage;
using Fenrir::Math::Vec3;
using Fenrir::Render::LightKind;
using Fenrir::Render::PointShadowKind;
using Fenrir::Render::PunctualLight;
using Fenrir::Render::RenderGraph;
using Fenrir::Render::ShadowPool;
using Fenrir::Render::SpotShadowKind;
using RendererTest::LoggedMessages;
using RendererTest::RecordingLogger;

namespace
{
    PunctualLight Caster(LightKind kind, Vec3 position)
    {
        PunctualLight light;
        light.kind = kind;
        light.position = position;
        light.direction = {0.0f, -1.0f, 0.0f};
        light.colour = Vec3(1.0f);
        light.range = 20.0f;
        light.outerAngle = 0.6f;
        light.castShadows = true;
        return light;
    }

    TextureHandle MakeFallback(Fenrir::Graphics::GraphicsDevice& device)
    {
        TextureDesc desc;
        desc.format = TextureFormat::Depth32Float;
        desc.usage = TextureUsage::Sampled;
        return device.CreateTexture(desc, "No shadow");
    }
} // namespace

TEST_CASE("A shadow pool warns once when its budget leaves lights unshadowed", "[renderer][shadows][shadow-pools]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    auto messages = std::make_shared<LoggedMessages>();
    RecordingLogger logger(messages);
    const TextureHandle fallback = MakeFallback(*device);
    ShadowPool<SpotShadowKind> pool(*device, logger, {1, 64}, fallback);
    const std::vector<PunctualLight> lights{Caster(LightKind::Spot, {1.0f, 0.0f, 0.0f}),
                                            Caster(LightKind::Spot, {2.0f, 0.0f, 0.0f}),
                                            Caster(LightKind::Spot, {3.0f, 0.0f, 0.0f})};
    const std::array<Vec3, 1> origin{Vec3(0.0f)};

    const Fenrir::Render::ShadowChoice first = pool.Choose(lights, origin);
    INFO("the nearest light keeps its shadow");
    REQUIRE(first.lights == std::vector<std::size_t>{0});
    INFO("one warning, naming the kind, how many asked and the budget");
    REQUIRE(messages->warnings.size() == 1);
    REQUIRE(messages->warnings[0] ==
            "FenrirRenderer: 3 spot lights cast shadows, past the spot shadow budget of 1, so those farthest from the "
            "cameras light unshadowed");
    (void)pool.Choose(lights, origin);
    INFO("no second warning the next frame");
    REQUIRE(messages->warnings.size() == 1);
    device->Destroy(fallback);
}

TEST_CASE("A shadow pool makes its maps only while a light casts, and binds the fallback otherwise",
          "[renderer][shadows][shadow-pools]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    auto messages = std::make_shared<LoggedMessages>();
    RecordingLogger logger(messages);
    const TextureHandle fallback = MakeFallback(*device);
    ShadowPool<SpotShadowKind> pool(*device, logger, {4, 64}, fallback);
    const std::vector<PunctualLight> lights{Caster(LightKind::Spot, {1.0f, 0.0f, 0.0f}),
                                            Caster(LightKind::Point, {2.0f, 0.0f, 0.0f})};
    const std::array<Vec3, 1> origin{Vec3(0.0f)};
    const std::size_t textures = RendererProbe::LiveTextures(*device);

    SECTION("with nothing casting, no light keeps a shadow and no map is made")
    {
        RenderGraph graph;
        const auto frame = pool.Prepare(pool.Choose(lights, origin), 0, graph, lights, false);
        INFO("no light keeps a shadow");
        REQUIRE(frame.lights.empty());
        REQUIRE(pool.Count() == 0);
        INFO("no map, none imported, and the fallback bound");
        REQUIRE(RendererProbe::LiveTextures(*device) == textures);
        REQUIRE(pool.Map().IsEmpty());
        REQUIRE(pool.Bound() == fallback);
    }
    SECTION("with casters, the spot light's shadow in a map of the budget's grid")
    {
        RenderGraph graph;
        const auto frame = pool.Prepare(pool.Choose(lights, origin), 0, graph, lights, true);
        INFO("only the spot light keeps a shadow");
        REQUIRE(frame.lights.size() == 1);
        REQUIRE(frame.lights[0] == 0);
        REQUIRE(frame.shadows.size() == 1);
        INFO("one map made, and bound");
        REQUIRE(RendererProbe::LiveTextures(*device) == textures + 1);
        REQUIRE(!pool.Map().IsEmpty());
        REQUIRE(pool.Bound() == pool.Map());

        const TextureHandle map = pool.Map();
        RenderGraph next;
        (void)pool.Prepare(pool.Choose(lights, origin), 1, next, lights, true);
        INFO("the next frame keeps it");
        REQUIRE(pool.Map() == map);
        REQUIRE(RendererProbe::LiveTextures(*device) == textures + 1);

        pool.SetBudget(9);
        INFO("a new budget lets the map go at once");
        REQUIRE(RendererProbe::LiveTextures(*device) == textures);
        RenderGraph after;
        (void)pool.Prepare(pool.Choose(lights, origin), 2, after, lights, true);
        INFO("and the next frame with a caster makes it again");
        REQUIRE(RendererProbe::LiveTextures(*device) == textures + 1);
        REQUIRE(!pool.Map().IsEmpty());
        REQUIRE(pool.Map() != map);
    }
    device->Destroy(fallback);
}

TEST_CASE("A shadow pool with a budget of 0 never makes its maps", "[renderer][shadows][shadow-pools]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    auto messages = std::make_shared<LoggedMessages>();
    RecordingLogger logger(messages);
    ShadowPool<PointShadowKind> pool(*device, logger, {0, 64});
    const std::vector<PunctualLight> lights{Caster(LightKind::Point, {1.0f, 0.0f, 0.0f})};
    const std::array<Vec3, 1> origin{Vec3(0.0f)};
    const std::size_t textures = RendererProbe::LiveTextures(*device);

    RenderGraph graph;
    const auto frame = pool.Prepare(pool.Choose(lights, origin), 0, graph, lights, true);
    INFO("no light keeps a shadow, and no map is made");
    REQUIRE(frame.lights.empty());
    REQUIRE(RendererProbe::LiveTextures(*device) == textures);
    INFO("the point pool's own cube that lights everything is bound");
    REQUIRE(!pool.Fallback().IsEmpty());
    REQUIRE(pool.Bound() == pool.Fallback());
}

TEST_CASE("The point shadow pool makes and destroys its own fallback cube", "[renderer][shadows][shadow-pools]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    auto messages = std::make_shared<LoggedMessages>();
    RecordingLogger logger(messages);
    const std::size_t textures = RendererProbe::LiveTextures(*device);
    {
        ShadowPool<PointShadowKind> pool(*device, logger, {4, 64});
        INFO("one cube made with the pool");
        REQUIRE(RendererProbe::LiveTextures(*device) == textures + 1);
    }
    INFO("and destroyed with it");
    REQUIRE(RendererProbe::LiveTextures(*device) == textures);
}
