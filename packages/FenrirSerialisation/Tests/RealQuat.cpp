// ToTree over FenrirMath's real Quat (ticket 10, the spec's "Gaps settled here"): stored w first, it visits and writes
// as x, y, z, w, the order of the goldens, glTF and C#'s Quaternion.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "FenrirMath/Generated/FenrirMathTypes.hpp"
#include "FenrirMath/Math.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>

using namespace Fenrir::Serialisation;

namespace
{
    // A struct holding FenrirMath's real Quat, at identity by default.
    struct Pose
    {
        Fenrir::Math::Quat rotation = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    const Fenrir::FieldInfo PoseFields[] = {
        {.name = "rotation",
         .type = {.kind = Fenrir::Kind::Struct, .type = &Fenrir::Describe<Fenrir::Math::Quat>::Description()},
         .offset = offsetof(Pose, rotation)},
    };

    const Fenrir::TypeDescription PoseDescription{
        .name = "BridgeTest::Pose",
        .package = "FenrirSerialisationTests",
        .role = Fenrir::Role::Component,
        .size = sizeof(Pose),
        .alignment = alignof(Pose),
        .construct = Fenrir::ConstructValue<Pose>,
        .destroy = Fenrir::DestroyValue<Pose>,
        .fields = PoseFields,
    };
} // namespace

TEST_CASE("A struct holding FenrirMath's real Quat writes its rotation as x, y, z, w", "[serialisation][totree]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered =
            Fenrir::Generated::RegisterFenrirMathTypes(registry) && registry.Register(PoseDescription);
        INFO("FenrirMath's types and the struct holding a Quat register without a refusal");
        REQUIRE(registered);
    }

    Pose pose;
    pose.rotation.x = -0.3826834f;
    pose.rotation.y = 0.0f;
    pose.rotation.z = 0.0f;
    pose.rotation.w = 0.9238795f;

    Array rotation;
    rotation.Add(-0.3826834f);
    rotation.Add(0.0f);
    rotation.Add(0.0f);
    rotation.Add(0.9238795f);
    Object expected;
    expected.Add("rotation", std::move(rotation));

    INFO("Quat is stored w first, but its description visits x, y, z, w, so the rotation is [x, y, z, w]");
    REQUIRE(ToTree(&pose, PoseDescription, registry, Resolvers{}) == Value(std::move(expected)));
}
