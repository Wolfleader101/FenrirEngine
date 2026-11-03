// Which render target images each view may read (ADR-0026): after every view drawing into them, or, where a view would
// wait on itself, directly or in a cycle, the default texture instead.

#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/ImageReads.hpp"

using Fenrir::Render::AssetKey;
using Fenrir::Render::ImageReads;
using Fenrir::Render::PlanImageReads;
using Fenrir::Render::ReplacedBecause;
using Fenrir::Render::ReplacedImage;

namespace
{
    constexpr AssetKey Window{};
    constexpr AssetKey A{1, 1};
    constexpr AssetKey B{2, 1};
    constexpr AssetKey C{3, 1};
} // namespace

TEST_CASE("A view sampling its own target image gets the default texture for it", "[renderer][cameras]")
{
    const std::vector<AssetKey> targets{A};
    const std::vector<ImageReads> plan = PlanImageReads(targets, {A});
    INFO("image A replaced as its own target, nothing read");
    REQUIRE(plan[0].replaced == std::vector<ReplacedImage>{{A, ReplacedBecause::OwnTarget}});
    REQUIRE(plan[0].reads.empty());
}

TEST_CASE("A view reads an image another view draws into", "[renderer][cameras]")
{
    const std::vector<AssetKey> targets{A, Window};
    const std::vector<ImageReads> plan = PlanImageReads(targets, {A});
    INFO("the window's view reads image A");
    REQUIRE(plan[1].reads == std::vector<AssetKey>{A});
    REQUIRE(plan[1].replaced.empty());
}

TEST_CASE("Views in a chain each read the image of the view before", "[renderer][cameras]")
{
    // A view into C sampling B, one into B sampling A, and one into A.
    const std::vector<AssetKey> targets{C, B, A};
    const std::vector<ImageReads> plan = PlanImageReads(targets, {A, B});
    INFO("the view into C reads B, and A too, which it may wait on through B");
    REQUIRE(plan[0].reads == std::vector<AssetKey>{A, B});
    INFO("the view into B reads A, and B, its own, is replaced");
    REQUIRE(plan[1].reads == std::vector<AssetKey>{A});
    REQUIRE(plan[1].replaced == std::vector<ReplacedImage>{{B, ReplacedBecause::OwnTarget}});
    INFO("the view into A would wait on the view into B through B, which waits on it, so B is replaced");
    REQUIRE(plan[2].replaced ==
            std::vector<ReplacedImage>{{A, ReplacedBecause::OwnTarget}, {B, ReplacedBecause::Cycle}});
}

TEST_CASE("The later of two views that would wait on each other gets the default texture", "[renderer][cameras]")
{
    const std::vector<AssetKey> targets{A, B};
    const std::vector<ImageReads> plan = PlanImageReads(targets, {A, B});
    INFO("the first view reads B, and its own A is replaced");
    REQUIRE(plan[0].reads == std::vector<AssetKey>{B});
    REQUIRE(plan[0].replaced == std::vector<ReplacedImage>{{A, ReplacedBecause::OwnTarget}});
    INFO("the second would wait on the first through A, a cycle, so A is replaced, as is its own B");
    REQUIRE(plan[1].reads.empty());
    REQUIRE(plan[1].replaced ==
            std::vector<ReplacedImage>{{A, ReplacedBecause::Cycle}, {B, ReplacedBecause::OwnTarget}});
}

TEST_CASE("An image no view draws into this frame is read with nothing to wait on", "[renderer][cameras]")
{
    const std::vector<AssetKey> targets{Window};
    const std::vector<ImageReads> plan = PlanImageReads(targets, {A});
    INFO("image A, drawn in an earlier frame, is read");
    REQUIRE(plan[0].reads == std::vector<AssetKey>{A});
}
