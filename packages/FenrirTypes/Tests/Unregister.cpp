#include "RecordingRegistry.hpp"
#include "TestTypes.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using Fenrir::Role;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;

using TypesTest::Contains;
using TypesTest::RecordingRegistry;

namespace
{
    struct LabelTag
    {
        using Value = std::string;
    };

    struct CountTag
    {
        using Value = int;
    };
} // namespace

TEST_CASE("An unregistered type is found by none of its names, and its attachments are gone", "[types][unregister]")
{
    RecordingRegistry recording;
    INFO("setup: Transform registers and carries an attachment");
    REQUIRE(recording.registry.Register(TypesTest::TransformDescription));
    REQUIRE(recording.registry.Attach<LabelTag>("TypesTest::Transform", "placed"));

    INFO("unregistering a registered type succeeds");
    REQUIRE(recording.registry.Unregister("TypesTest::Transform"));

    INFO("its canonical name finds nothing");
    REQUIRE(recording.registry.Find("TypesTest::Transform") == nullptr);

    INFO("its former names find nothing");
    REQUIRE(recording.registry.Find("TypesTest::Placement") == nullptr);
    REQUIRE(recording.registry.Find("TypesTest::Pose") == nullptr);

    INFO("it is listed in its role no more");
    REQUIRE(recording.registry.Types(Role::Component).empty());

    INFO("nothing was reported");
    REQUIRE(recording.diagnostics.empty());

    INFO("registered again, it carries none of the attachments it had");
    REQUIRE(recording.registry.Register(TypesTest::TransformDescription));
    REQUIRE(recording.registry.Attachment<LabelTag>("TypesTest::Transform") == nullptr);
}

TEST_CASE("A type is unregistered by a former name", "[types][unregister]")
{
    TypeRegistry registry;
    INFO("setup: Transform registers");
    REQUIRE(registry.Register(TypesTest::TransformDescription));

    INFO("a former name unregisters the type");
    REQUIRE(registry.Unregister("TypesTest::Pose"));

    INFO("its canonical name finds nothing after");
    REQUIRE(registry.Find("TypesTest::Transform") == nullptr);
}

TEST_CASE("Unregistering one type leaves the others and their attachments", "[types][unregister]")
{
    TypeRegistry registry;
    INFO("setup: three types register, each with an attachment");
    REQUIRE(registry.Register(TypesTest::Vec3Description));
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Register(TypesTest::QuatDescription));
    REQUIRE(registry.Attach<CountTag>("TypesTest::Vec3", 3));
    REQUIRE(registry.Attach<CountTag>("TypesTest::Quat", 4));
    const int* quatCount = registry.Attachment<CountTag>("TypesTest::Quat");

    INFO("the middle type unregisters");
    REQUIRE(registry.Unregister("TypesTest::Transform"));

    INFO("the types before and after it are still found");
    REQUIRE(registry.Find("TypesTest::Vec3") == &TypesTest::Vec3Description);
    REQUIRE(registry.Find("TypesTest::Quat") == &TypesTest::QuatDescription);

    INFO("their attachments keep their values and their addresses");
    REQUIRE(*registry.Attachment<CountTag>("TypesTest::Vec3") == 3);
    REQUIRE(registry.Attachment<CountTag>("TypesTest::Quat") == quatCount);
    REQUIRE(*quatCount == 4);

    INFO("the rest stay listed in registration order");
    const std::vector<const TypeDescription*> listed = registry.Types(Role::BoundType);
    REQUIRE(listed.size() == 2);
    REQUIRE(listed[0] == &TypesTest::Vec3Description);
    REQUIRE(listed[1] == &TypesTest::QuatDescription);

    INFO("a type registered after the removal is found too");
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Find("TypesTest::Pose") == &TypesTest::TransformDescription);
}

TEST_CASE("Unregistering a type another registered type extends is refused, until that type goes",
          "[types][unregister]")
{
    RecordingRegistry recording;
    INFO("setup: WeaponData and Sword, which extends it, register");
    REQUIRE(recording.registry.Register(TypesTest::WeaponDataDescription));
    REQUIRE(recording.registry.Register(TypesTest::SwordDescription));

    INFO("unregistering the base is refused");
    REQUIRE_FALSE(recording.registry.Unregister("TypesTest::WeaponData"));

    INFO("one diagnostic names the type extending it: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.back(), "`TypesTest::Sword`"));

    INFO("the base is still registered, and Sword still is one");
    REQUIRE(recording.registry.Find("TypesTest::WeaponData") == &TypesTest::WeaponDataDescription);
    REQUIRE(recording.registry.IsA("TypesTest::Sword", "TypesTest::WeaponData"));

    INFO("once Sword is unregistered, the base unregisters");
    REQUIRE(recording.registry.Unregister("TypesTest::Sword"));
    REQUIRE(recording.registry.Unregister("TypesTest::WeaponData"));
}

TEST_CASE("Unregistering a type another registered type's field holds is refused, until that type goes",
          "[types][unregister]")
{
    RecordingRegistry recording;
    INFO("setup: Gem, and a data asset holding a list of Gems, register");
    REQUIRE(recording.registry.Register(TypesTest::GemDescription));
    TypeDescription purse = TypesTest::SwordDescription;
    purse.name = "TypesTest::Purse";
    purse.base = {};
    REQUIRE(recording.registry.Register(purse));

    INFO("unregistering Gem is refused while Purse's field holds it");
    REQUIRE_FALSE(recording.registry.Unregister("TypesTest::Gem"));

    INFO("one diagnostic names the holder and its field: "
         << (recording.diagnostics.empty() ? std::string("none") : recording.diagnostics.back()));
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.back(), "`TypesTest::Purse`"));
    REQUIRE(Contains(recording.diagnostics.back(), "`gems`"));

    INFO("Gem is still registered");
    REQUIRE(recording.registry.Find("TypesTest::Gem") == &TypesTest::GemDescription);

    INFO("once Purse is unregistered, Gem unregisters");
    REQUIRE(recording.registry.Unregister("TypesTest::Purse"));
    REQUIRE(recording.registry.Unregister("TypesTest::Gem"));
}

TEST_CASE("Unregistering a name nothing registered is refused with a diagnostic", "[types][unregister]")
{
    RecordingRegistry recording;

    INFO("an unknown name is refused");
    REQUIRE_FALSE(recording.registry.Unregister("TypesTest::Nothing"));

    INFO("the diagnostic names it");
    REQUIRE(recording.diagnostics.size() == 1);
    REQUIRE(Contains(recording.diagnostics.back(), "`TypesTest::Nothing`"));
}

TEST_CASE("Detaching removes one attachment and leaves the type's others", "[types][attachments]")
{
    TypeRegistry registry;
    INFO("setup: Transform registers and carries two attachments");
    REQUIRE(registry.Register(TypesTest::TransformDescription));
    REQUIRE(registry.Attach<LabelTag>("TypesTest::Transform", "placed"));
    REQUIRE(registry.Attach<CountTag>("TypesTest::Transform", 2));

    INFO("detaching a hung attachment says it removed one");
    REQUIRE(registry.Detach<LabelTag>("TypesTest::Transform"));

    INFO("it is absent after");
    REQUIRE(registry.Attachment<LabelTag>("TypesTest::Transform") == nullptr);

    INFO("the type's other attachment stays");
    REQUIRE(*registry.Attachment<CountTag>("TypesTest::Transform") == 2);

    INFO("detaching it again, or from a type nothing registered, removes nothing");
    REQUIRE_FALSE(registry.Detach<LabelTag>("TypesTest::Transform"));
    REQUIRE_FALSE(registry.Detach<LabelTag>("TypesTest::Nothing"));
}
