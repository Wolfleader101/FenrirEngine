// Screen-space reflections' trace (ADR-0056, lighting ticket 30) on the null backend: the CPU mirror of
// builtin:reflection-trace's and Fenrir/Lighting.glsl's maths, checked against worked examples (the mirror ray's ends
// on screen, a Hi-Z march over a known pyramid, the fades, the reprojection into last frame's view and the history mip
// a roughness picks); and each reflecting view's ReflectionTrace pass after its HiZ passes, whose hit target and the
// history its Opaque pass binds in the view set, with 1 x 1 stand-ins everywhere else; and the two debug views'
// Output passes (lighting ticket 31).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/ReflectionTrace.hpp"
#include "Core/SceneFrame.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::PixelSize;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Render::HiZPyramid;
using Fenrir::Render::ScreenRay;
using RendererTest::Labelled;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief The worked examples' view: 64 x 64 pixels. */
    constexpr PixelSize Size{64, 64};
    /** @brief Their camera's near plane. */
    constexpr float Near = 0.1f;
    /** @brief A quarter turn, the worked examples' vertical field of view: focal length 1. */
    constexpr float QuarterTurn = 1.57079633f;

    Mat4 Projection()
    {
        return Fenrir::Render::ReversedZPerspective(QuarterTurn, 1.0f, Near);
    }

    /** @brief The reversed-Z depth the worked examples' camera gives a point `distance` in front of it. */
    float DepthAt(float distance)
    {
        return Near / distance;
    }

    bool Close(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }

    /**
     * @brief The depths of a 64 x 64 view of a wall 4 in front of the camera, with a block 1 in front of it over rows
     * `blockTop` to `blockBottom` of the centre columns when they are not equal.
     */
    HiZPyramid WallPyramid(std::uint32_t blockTop = 0, std::uint32_t blockBottom = 0)
    {
        std::vector<float> depth(std::size_t{Size.width} * Size.height, DepthAt(4.0f));
        for (std::uint32_t y = blockTop; y < blockBottom; ++y)
        {
            for (std::uint32_t x = 28; x < 36; ++x)
            {
                depth[std::size_t{y} * Size.width + x] = DepthAt(1.0f);
            }
        }
        return HiZPyramid::Of(depth, Size);
    }

    /** @brief The worked examples' floor ray: from a floor point 2 in front and 1 below, `maxDistance` long. */
    ScreenRay FloorRay(float maxDistance)
    {
        return Fenrir::Render::ReflectedRay(Projection(), {0.0f, -1.0f, -2.0f}, {0.0f, 1.0f, 0.0f}, Size, maxDistance);
    }

    /** @brief The labels of `passes`, in order. */
    std::vector<std::string> Labels(const std::vector<PassRecord>& passes)
    {
        std::vector<std::string> labels;
        for (const PassRecord& pass : passes)
        {
            labels.push_back(pass.pass.label);
        }
        return labels;
    }

    /** @brief Whether `pass` samples `texture`. */
    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief The texture the group a pass bound at `set` binds at `binding`. */
    TextureHandle GroupTexture(const RendererTestApp& test, const PassRecord& pass, std::size_t set,
                               std::uint32_t binding)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(set));
        const auto found =
            std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.binding == binding; });
        REQUIRE(found != entries.end());
        return found->texture;
    }

    /** @brief The texture the view group a pass bound at set 1 binds at `binding`. */
    TextureHandle ViewTexture(const RendererTestApp& test, const PassRecord& pass, std::uint32_t binding)
    {
        return GroupTexture(test, pass, 1, binding);
    }

    /** @brief The float at byte `offset` of the view block a pass bound at set 1. */
    float ViewFloat(const RendererTestApp& test, const PassRecord& pass, std::size_t offset)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        const auto block = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return e.binding == 0; });
        REQUIRE(block != entries.end());
        return RendererTest::FloatAt(RendererProbe::Bytes(test.Device(), block->buffer), block->offset + offset);
    }

    /** @brief One camera over the 1280 x 720 window with reflections and nothing else of the post effects. */
    Fenrir::Camera& AddReflectingCamera(RendererTestApp& test)
    {
        Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
        camera.look.bloom.intensity = 0.0f;
        camera.reflections.enabled = true;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }

    /** @brief Where view set binding `fenrirReflectionHits` and `fenrirSceneHistory` are. */
    constexpr std::uint32_t HitsBinding = 4;
    constexpr std::uint32_t HistoryBinding = 5;
    /** @brief Where ViewData::reflectionInfo starts in the view block. */
    constexpr std::size_t ReflectionInfoOffset = 640;
    /** @brief Where the Output block's grey channel is: the third word, after the exposure and the tonemapper. */
    constexpr std::size_t GreyChannelOffset = 8;

    /** @brief The texture the material group an Output pass bound at set 2 binds at `binding`. */
    TextureHandle OutputTexture(const RendererTestApp& test, const PassRecord& pass, std::uint32_t binding)
    {
        return GroupTexture(test, pass, 2, binding);
    }

    /** @brief The word at byte `offset` of the Output block an Output pass bound at set 1. */
    std::uint32_t OutputUint(const RendererTestApp& test, const PassRecord& pass, std::size_t offset)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        const auto block = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return e.binding == 0; });
        REQUIRE(block != entries.end());
        return RendererTest::UintAt(RendererProbe::Bytes(test.Device(), block->buffer), block->offset + offset);
    }
} // namespace

TEST_CASE("Reflections are on only when enabled with an intensity and a max distance above 0", "[renderer][ssr]")
{
    using Fenrir::Render::HasReflections;
    INFO("off by default");
    REQUIRE_FALSE(HasReflections({}));
    Fenrir::Render::ReflectionSettings settings;
    settings.enabled = true;
    INFO("on when enabled with the defaults");
    REQUIRE(HasReflections(settings));
    INFO("off at intensity 0");
    settings.intensity = 0.0f;
    REQUIRE_FALSE(HasReflections(settings));
    INFO("off at max distance 0");
    settings.intensity = 1.0f;
    settings.maxDistance = 0.0f;
    REQUIRE_FALSE(HasReflections(settings));
}

TEST_CASE("A camera's reflections default to intensity 1, max distance 50, thickness 0.5 and cutoff 0.6",
          "[renderer][ssr]")
{
    const Fenrir::Reflections reflections;
    INFO("the ticket's defaults, off");
    REQUIRE_FALSE(reflections.enabled);
    REQUIRE(reflections.intensity == 1.0f);
    REQUIRE(reflections.maxDistance == 50.0f);
    REQUIRE(reflections.thickness == 0.5f);
    REQUIRE(reflections.roughnessCutoff == 0.6f);
}

TEST_CASE("A floor's mirror ray runs on screen from its pixel up to where the reflected ray ends", "[renderer][ssr]")
{
    // From the camera the floor point (0, -1, -2) is seen along (0, -1, -2); about the floor's normal it reflects to
    // (0, 1, -2) / sqrt(5), and one unit along it ends at (0, -0.5528, -2.8944).
    const ScreenRay ray = FloorRay(1.0f);
    INFO("it starts at the floor point's pixel, (32, 48), at depth 0.1 / 2");
    REQUIRE(Close(ray.start.pixel.x, 32.0f, 1e-4f));
    REQUIRE(Close(ray.start.pixel.y, 48.0f, 1e-4f));
    REQUIRE(Close(ray.start.depth, 0.05f, 1e-6f));
    INFO("and ends at its end's: row 32 + 32 x 0.5528 / 2.8944, depth 0.1 / 2.8944");
    REQUIRE(Close(ray.end.pixel.x, 32.0f, 1e-4f));
    REQUIRE(Close(ray.end.pixel.y, 38.1115f, 1e-3f));
    REQUIRE(Close(ray.end.depth, 0.034549f, 1e-5f));
    INFO("its direction is the reflection");
    REQUIRE(Close(ray.direction.y, 1.0f / std::sqrt(5.0f), 1e-5f));
    REQUIRE(Close(ray.direction.z, -2.0f / std::sqrt(5.0f), 1e-5f));
}

TEST_CASE("A mirror ray that leaves the screen ends at its edge", "[renderer][ssr]")
{
    // The point straight ahead at 2, its normal turned half left: the view ray reflects to (-1, 0, 0), along the
    // depth-2 plane, which leaves the view at x = -2 (the focal length is 1).
    const Vec3 normal = glm::normalize(Vec3(-1.0f, 0.0f, 1.0f));
    const ScreenRay ray = Fenrir::Render::ReflectedRay(Projection(), {0.0f, 0.0f, -2.0f}, normal, Size, 10.0f);
    INFO("it ends on the left edge, row 32, at its depth: " << ray.end.pixel.x << ", " << ray.end.pixel.y);
    REQUIRE(Close(ray.end.pixel.x, 0.0f, 1e-3f));
    REQUIRE(Close(ray.end.pixel.y, 32.0f, 1e-3f));
    REQUIRE(Close(ray.end.depth, 0.05f, 1e-6f));
}

TEST_CASE("A Hi-Z march finds the wall a floor's mirror ray reaches", "[renderer][ssr]")
{
    // The floor ray reaches the wall 4 in front at (0, 0, -4) after sqrt(5): the view's centre, pixel (32, 32).
    const HiZPyramid pyramid = WallPyramid();
    const auto hit = Fenrir::Render::MarchHiZ(pyramid, FloorRay(10.0f), glm::inverse(Projection()), 0.5f);
    INFO("a hit, at the centre's pixel row, at the wall's depth: " << hit.point.pixel.x << ", " << hit.point.pixel.y
                                                                   << " after " << hit.iterations << " steps");
    REQUIRE(hit.hit);
    REQUIRE(Close(hit.point.pixel.x, 32.0f, 0.5f));
    REQUIRE(Close(hit.point.pixel.y, 32.0f, 1.0f));
    REQUIRE(Close(hit.point.depth, DepthAt(4.0f), 1e-4f));
    INFO("within the march's steps");
    REQUIRE(hit.iterations <= Fenrir::Render::ReflectionMaxIterations);
}

TEST_CASE("A Hi-Z march misses a wall past the camera's max distance", "[renderer][ssr]")
{
    const HiZPyramid pyramid = WallPyramid();
    INFO("the ray ends 2 along, short of the wall at sqrt(5)");
    const auto hit = Fenrir::Render::MarchHiZ(pyramid, FloorRay(2.0f), glm::inverse(Projection()), 0.5f);
    REQUIRE_FALSE(hit.hit);
}

TEST_CASE("A Hi-Z march steps over a block the ray passes behind by more than the thickness", "[renderer][ssr]")
{
    // A block 1 in front of the camera over rows 38 to 41, which the ray, 2.5 or so away there, passes far behind.
    const HiZPyramid pyramid = WallPyramid(38, 42);
    const Mat4 inverse = glm::inverse(Projection());
    const auto thin = Fenrir::Render::MarchHiZ(pyramid, FloorRay(10.0f), inverse, 0.5f);
    INFO("with thickness 0.5 it steps over the block to the wall behind: row " << thin.point.pixel.y);
    REQUIRE(thin.hit);
    REQUIRE(Close(thin.point.depth, DepthAt(4.0f), 1e-4f));
    const auto thick = Fenrir::Render::MarchHiZ(pyramid, FloorRay(10.0f), inverse, 10.0f);
    INFO("with thickness 10 the block catches it: row " << thick.point.pixel.y);
    REQUIRE(thick.hit);
    REQUIRE(thick.point.pixel.y < 42.0f);
    REQUIRE(thick.point.pixel.y >= 38.0f);
}

TEST_CASE("A Hi-Z march gives up when its steps run out", "[renderer][ssr]")
{
    const HiZPyramid pyramid = WallPyramid();
    INFO("two steps do not reach the wall");
    const auto hit = Fenrir::Render::MarchHiZ(pyramid, FloorRay(10.0f), glm::inverse(Projection()), 0.5f, 2);
    REQUIRE_FALSE(hit.hit);
    REQUIRE(hit.iterations == 2);
}

TEST_CASE("A hit's confidence fades out over the last tenth of the view at each edge", "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionEdgeFadeAt;
    INFO("1 in the middle");
    REQUIRE(ReflectionEdgeFadeAt({0.5f, 0.5f}) == 1.0f);
    REQUIRE(ReflectionEdgeFadeAt({0.1f, 0.9f}) == 1.0f);
    INFO("half way through the fade, at each edge");
    REQUIRE(Close(ReflectionEdgeFadeAt({0.05f, 0.5f}), 0.5f, 1e-5f));
    REQUIRE(Close(ReflectionEdgeFadeAt({0.5f, 0.97f}), 0.3f, 1e-5f));
    INFO("0 at the edge and off it");
    REQUIRE(ReflectionEdgeFadeAt({1.0f, 0.5f}) == 0.0f);
    REQUIRE(ReflectionEdgeFadeAt({-0.01f, 0.5f}) == 0.0f);
}

TEST_CASE("A hit's confidence fades out over the last fifth of the max distance", "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionDistanceFadeAt;
    INFO("over the last fifth of a max distance of 50");
    REQUIRE(ReflectionDistanceFadeAt(40.0f, 50.0f) == 1.0f);
    REQUIRE(Close(ReflectionDistanceFadeAt(45.0f, 50.0f), 0.5f, 1e-5f));
    REQUIRE(ReflectionDistanceFadeAt(50.0f, 50.0f) == 0.0f);
}

TEST_CASE("A hit's confidence fades out as its ray turns towards the camera", "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionTowardsCameraFadeAt;
    INFO("a ray away from the camera keeps it; one across the view half; one towards the camera none");
    REQUIRE(ReflectionTowardsCameraFadeAt({0.0f, 0.0f, -1.0f}) == 1.0f);
    REQUIRE(Close(ReflectionTowardsCameraFadeAt(glm::normalize(Vec3(1.0f, 0.0f, -0.05f))), 0.5f, 1e-3f));
    REQUIRE(ReflectionTowardsCameraFadeAt({0.0f, 0.0f, 1.0f}) == 0.0f);
}

TEST_CASE("A hit is reprojected into the previous frame's view", "[renderer][ssr]")
{
    const Mat4 projection = Projection();
    const Vec3 hit{0.0f, 0.0f, -2.0f};
    INFO("a still camera sees it where it is now, the view's centre");
    const auto still = Fenrir::Render::ReprojectToPrevious(projection, hit);
    REQUIRE(Close(still.uv.x, 0.5f, 1e-5f));
    REQUIRE(Close(still.uv.y, 0.5f, 1e-5f));
    REQUIRE(Close(still.depth, 0.05f, 1e-6f));
    // Last frame the camera stood 1 to the right: the point was 1 to its left, half way to the view's left edge.
    const Mat4 previousView = glm::translate(Mat4(1.0f), Vec3(-1.0f, 0.0f, 0.0f));
    INFO("a camera that has moved left saw it to the left of centre");
    const auto moved = Fenrir::Render::ReprojectToPrevious(projection * previousView, hit);
    REQUIRE(Close(moved.uv.x, 0.25f, 1e-5f));
    REQUIRE(Close(moved.uv.y, 0.5f, 1e-5f));
    INFO("and a point above the centre is above it, +y down");
    REQUIRE(Fenrir::Render::ReprojectToPrevious(projection, {0.0f, 1.0f, -2.0f}).uv.y < 0.5f);
    INFO("all of them on last frame's screen");
    REQUIRE(still.onScreen);
    REQUIRE(moved.onScreen);
}

TEST_CASE("A hit behind the previous frame's camera was off its screen", "[renderer][ssr]")
{
    // Straight behind the camera, the divide by a negative w would put it at the view's centre.
    const auto behind = Fenrir::Render::ReprojectToPrevious(Projection(), {0.0f, 0.0f, 2.0f});
    INFO("off screen, though its UV lands at " << behind.uv.x << ", " << behind.uv.y);
    REQUIRE_FALSE(behind.onScreen);
}

TEST_CASE("A hit hidden last frame loses its confidence by how far in front the surface that hid it was",
          "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionDisocclusion;
    const Mat4 inverse = glm::inverse(Projection());
    INFO("the same surface last frame: kept");
    REQUIRE(Close(ReflectionDisocclusion(inverse, DepthAt(4.0f), DepthAt(4.0f), 0.5f), 1.0f, 1e-4f));
    INFO("something farther away there: kept");
    REQUIRE(ReflectionDisocclusion(inverse, DepthAt(4.0f), DepthAt(6.0f), 0.5f) == 1.0f);
    INFO("a surface a quarter in front, half the thickness: half");
    REQUIRE(Close(ReflectionDisocclusion(inverse, DepthAt(4.0f), DepthAt(3.75f), 0.5f), 0.5f, 1e-3f));
    INFO("one a whole unit in front: none");
    REQUIRE(ReflectionDisocclusion(inverse, DepthAt(4.0f), DepthAt(3.0f), 0.5f) == 0.0f);
}

TEST_CASE("Reflections fade out over the last tenth of roughness below the camera's cutoff", "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionRoughnessFadeAt;
    INFO("a mirror and a surface 0.1 below the cutoff 0.6 reflect fully");
    REQUIRE(ReflectionRoughnessFadeAt(0.04f, 0.6f) == 1.0f);
    REQUIRE(ReflectionRoughnessFadeAt(0.5f, 0.6f) == 1.0f);
    INFO("half way through the fade, half");
    REQUIRE(Close(ReflectionRoughnessFadeAt(0.55f, 0.6f), 0.5f, 1e-5f));
    INFO("at the cutoff and above, none");
    REQUIRE(ReflectionRoughnessFadeAt(0.6f, 0.6f) == 0.0f);
    REQUIRE(ReflectionRoughnessFadeAt(0.9f, 0.6f) == 0.0f);
}

TEST_CASE("A rougher surface or a longer ray samples a blurrier level of the history", "[renderer][ssr]")
{
    using Fenrir::Render::ReflectionMip;
    INFO("a near mirror samples level 0");
    REQUIRE(ReflectionMip(0.04f, 10.0f, 10.0f, 1.0f, 64.0f, false, 6.0f) == 0.0f);
    // Roughness 0.6: a = 0.36, Phong power 2 / a^2 - 2 = 13.43, so Uludag's cone is acos(0.244^(1 / 14.43)) = 0.4361
    // rad; ten units along it spans 2 x 10 x tan(0.4361) = 9.320 units, seen 10 + 10 units away on a 64-pixel view with
    // focal length 1 (1.6 pixels a unit), 14.91 pixels: level log2(14.91) = 3.898.
    const float rough = ReflectionMip(0.6f, 10.0f, 10.0f, 1.0f, 64.0f, false, 6.0f);
    INFO("roughness 0.6 ten units along samples level 3.9: " << rough);
    REQUIRE(Close(rough, 3.898f, 0.01f));
    INFO("a longer ray a blurrier one");
    REQUIRE(ReflectionMip(0.6f, 15.0f, 10.0f, 1.0f, 64.0f, false, 6.0f) > rough);
    INFO("never past the last level");
    REQUIRE(ReflectionMip(0.6f, 10.0f, 10.0f, 1.0f, 64.0f, false, 2.0f) == 2.0f);
    INFO("an orthographic view leaves the distance out: one pixel a unit, 9.32 pixels");
    REQUIRE(Close(ReflectionMip(0.6f, 10.0f, 10.0f, 2.0f / 64.0f, 64.0f, true, 6.0f), 3.220f, 0.01f));
}

TEST_CASE("A reflecting camera's ReflectionTrace pass follows its HiZ passes and comes before Opaque",
          "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddReflectingCamera(test);
    camera.ambientOcclusion.enabled = true;

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    const auto at = [&](const char* label) {
        return static_cast<std::size_t>(std::find(labels.begin(), labels.end(), label) - labels.begin());
    };
    INFO("one ReflectionTrace pass");
    REQUIRE(std::count(labels.begin(), labels.end(), "ReflectionTrace") == 1);
    INFO("after the last HiZ pass, so after GTAO's");
    REQUIRE(static_cast<std::size_t>(std::find(labels.rbegin(), labels.rend(), "HiZ").base() - labels.begin()) <=
            at("ReflectionTrace"));
    INFO("and before Opaque");
    REQUIRE(at("ReflectionTrace") < at("Opaque"));
}

TEST_CASE("The ReflectionTrace pass reads the pyramids and normals into an RGBA16F hit target at the view's size",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord trace = Labelled(passes, "ReflectionTrace").at(0);
    const PassRecord prepass = Labelled(passes, "DepthPrepass").at(0);
    const auto kept = RendererProbe::Reflections(test.renderer).at(0);
    INFO("it samples this frame's pyramid, last frame's and SceneNormals");
    REQUIRE(Samples(trace, kept.pyramid));
    REQUIRE(Samples(trace, kept.previousPyramid));
    REQUIRE(Samples(trace, prepass.pass.colour.at(0).texture));
    INFO("into one RGBA16F target at 1280 x 720");
    REQUIRE(trace.pass.colour.size() == 1);
    const auto desc = RendererProbe::Desc(test.Device(), trace.pass.colour[0].texture);
    REQUIRE(
        (desc.format == TextureFormat::Rgba16Float && desc.width == 1280 && desc.height == 720 && desc.samples == 1));
}

TEST_CASE("Opaque binds the reflection hits and the history in the view set; every other pass stand-ins",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const TextureHandle hits = Labelled(passes, "ReflectionTrace").at(0).pass.colour.at(0).texture;
    const TextureHandle history = RendererProbe::Reflections(test.renderer).at(0).history;
    const PassRecord opaque = Labelled(passes, "Opaque").at(0);
    INFO("Opaque samples both");
    REQUIRE(Samples(opaque, hits));
    REQUIRE(Samples(opaque, history));
    INFO("and binds them at set 1 bindings 4 and 5");
    REQUIRE(ViewTexture(test, opaque, HitsBinding) == hits);
    REQUIRE(ViewTexture(test, opaque, HistoryBinding) == history);
    INFO("the prepass binds 1 x 1 stand-ins");
    const PassRecord prepass = Labelled(passes, "DepthPrepass").at(0);
    const TextureHandle standIn = ViewTexture(test, prepass, HitsBinding);
    REQUIRE(standIn != hits);
    const auto desc = RendererProbe::Desc(test.Device(), standIn);
    REQUIRE((desc.width == 1 && desc.height == 1));
    REQUIRE(ViewTexture(test, prepass, HistoryBinding) != history);
}

TEST_CASE("The view block's reflectionInfo holds whether the view reflects, its intensity, cutoff and top mip",
          "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddReflectingCamera(test);
    camera.reflections.intensity = 0.75f;
    camera.reflections.roughnessCutoff = 0.5f;

    test.Frame();
    const PassRecord opaque = Labelled(test.Passes(), "Opaque").at(0);
    INFO("on, at intensity 0.75 and cutoff 0.5, over 11 levels");
    REQUIRE(ViewFloat(test, opaque, ReflectionInfoOffset) == 1.0f);
    REQUIRE(ViewFloat(test, opaque, ReflectionInfoOffset + 4) == 0.75f);
    REQUIRE(ViewFloat(test, opaque, ReflectionInfoOffset + 8) == 0.5f);
    REQUIRE(ViewFloat(test, opaque, ReflectionInfoOffset + 12) == 10.0f);
}

TEST_CASE("A camera without reflections has no trace, and its reflectionInfo says so", "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test).reflections.enabled = false;

    test.Frame();
    const std::vector<PassRecord> off = test.Passes();
    INFO("no ReflectionTrace pass");
    REQUIRE(Labelled(off, "ReflectionTrace").empty());
    INFO("and reflectionInfo.x 0");
    REQUIRE(ViewFloat(test, Labelled(off, "Opaque").at(0), ReflectionInfoOffset) == 0.0f);
}

TEST_CASE("A camera with reflections at intensity 0 has no reflection pass", "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test).reflections.intensity = 0.0f;

    test.Frame();
    INFO("enabled, but at intensity 0: no trace, no pyramid, nothing kept");
    REQUIRE(Labelled(test.Passes(), "ReflectionTrace").empty());
    REQUIRE(Labelled(test.Passes(), "HiZ").empty());
    REQUIRE(RendererProbe::Reflections(test.renderer).empty());
}

TEST_CASE("A camera showing its reflections has Output draw last frame's history at its hits, before SceneHistory "
          "replaces it",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test).debugView = Fenrir::CameraDebugView::Reflections;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const TextureHandle hits = Labelled(passes, "ReflectionTrace").at(0).pass.colour.at(0).texture;
    const TextureHandle history = RendererProbe::Reflections(test.renderer).at(0).history;
    const PassRecord output = Labelled(passes, "Output").at(0);
    INFO("Output samples the hits and the history, bound at set 2 bindings 0 and 1");
    REQUIRE(Samples(output, hits));
    REQUIRE(Samples(output, history));
    REQUIRE(OutputTexture(test, output, 0) == hits);
    REQUIRE(OutputTexture(test, output, 1) == history);
    const std::vector<std::string> labels = Labels(passes);
    const auto outputAt = std::find(labels.begin(), labels.end(), "Output");
    const auto historyAt = std::find(labels.begin(), labels.end(), "SceneHistory");
    INFO("the frame has SceneHistory passes");
    REQUIRE(historyAt != labels.end());
    INFO("and Output runs before them, before the history is overwritten with this frame's colour");
    REQUIRE(outputAt < historyAt);
    INFO("no error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A camera showing its reflection confidence has Output draw the hit target's blue as grey", "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test).debugView = Fenrir::CameraDebugView::ReflectionConfidence;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const TextureHandle hits = Labelled(passes, "ReflectionTrace").at(0).pass.colour.at(0).texture;
    const PassRecord output = Labelled(passes, "Output").at(0);
    INFO("Output samples the hits, bound at set 2 binding 0");
    REQUIRE(Samples(output, hits));
    REQUIRE(OutputTexture(test, output, 0) == hits);
    INFO("and its block picks channel 2, where the confidence is");
    REQUIRE(OutputUint(test, output, GreyChannelOffset) == 2u);
}

TEST_CASE("A camera showing its ambient occlusion has Output draw its red channel", "[renderer][ssr][gtao]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddReflectingCamera(test);
    camera.ambientOcclusion.enabled = true;
    camera.debugView = Fenrir::CameraDebugView::AmbientOcclusion;

    test.Frame();
    INFO("its block picks channel 0, where the occlusion is");
    REQUIRE(OutputUint(test, Labelled(test.Passes(), "Output").at(0), GreyChannelOffset) == 0u);
}

TEST_CASE("A camera showing either reflection view with reflections off draws Output black", "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddReflectingCamera(test);
    camera.reflections.enabled = false;
    for (const Fenrir::CameraDebugView view :
         {Fenrir::CameraDebugView::Reflections, Fenrir::CameraDebugView::ReflectionConfidence})
    {
        camera.debugView = view;
        test.Frame();
        const std::vector<PassRecord> passes = test.Passes();
        INFO("view " << static_cast<int>(view) << ": no trace");
        REQUIRE(Labelled(passes, "ReflectionTrace").empty());
        INFO("and Output binds the 1 x 1 black texture");
        const TextureHandle shown = OutputTexture(test, Labelled(passes, "Output").at(0), 0);
        const auto desc = RendererProbe::Desc(test.Device(), shown);
        REQUIRE((desc.width == 1 && desc.height == 1));
    }
    INFO("no error was logged");
    REQUIRE(test.messages->errors.empty());
}
