// FenrirRagdoll reaches physics only through FenrirPhysics's public headers (ADR-0010, ADR-0053). This file includes
// FenrirRagdoll's public headers before anything else, so if one of them ever reaches for Jolt, or Jolt's include path
// or definitions reach a user of FenrirRagdoll (this test target is one), the build fails here.
#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Description.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"

#ifdef JPH_VERSION_MAJOR
#error "A FenrirRagdoll public header includes Jolt."
#endif

#if __has_include(<Jolt/Jolt.h>)
#error "Jolt's include path reaches FenrirRagdoll's users: FenrirPhysics must link it PRIVATE."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The components' defaults are the spec's", "[ragdoll][seam]")
{
    const Fenrir::Ragdoll ragdoll;
    INFO("a Ragdoll follows, at full strength, following the root, in no group, with no root body");
    REQUIRE(ragdoll.mode == Fenrir::RagdollMode::Following);
    REQUIRE(ragdoll.strength == 1.0f);
    REQUIRE(ragdoll.followRoot);
    REQUIRE(ragdoll.group == 0);
    REQUIRE_FALSE(ragdoll.rootBody.HasEntityList());
    REQUIRE_FALSE(ragdoll.state.anyAwake);
    REQUIRE(ragdoll.state.bodies == 0);
    REQUIRE_FALSE(ragdoll.state.inactive);

    const Fenrir::RagdollBody body;
    INFO("a RagdollBody inherits its mode at full strength, its state Following on no bone");
    REQUIRE(body.mode == Fenrir::RagdollBodyMode::Inherit);
    REQUIRE(body.strengthScale == 1.0f);
    REQUIRE(body.state.mode == Fenrir::RagdollMode::Following);
    REQUIRE(body.state.bone == Fenrir::NoBone);
    REQUIRE(body.bone.Get() == std::string_view(""));
}
