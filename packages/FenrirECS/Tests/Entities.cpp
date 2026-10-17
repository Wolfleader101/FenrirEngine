#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

using Fenrir::Entity;
using Fenrir::EntityList;

// FenrirPhysics tells a joint's body in another scene by this: a handle is its own list's only if it equals the
// handle that list gives for its id.
TEST_CASE("entities with the same id in different lists are different entities", "[entity]")
{
    EntityList first;
    EntityList second;
    const Entity a = first.CreateEntity();
    const Entity b = second.CreateEntity();
    REQUIRE(a.GetId() == b.GetId());

    INFO("the same id in another list is a different entity");
    REQUIRE(a != b);
    REQUIRE_FALSE(a == b);
    INFO("each list's handle for the id is its own entity, not the other list's");
    REQUIRE(first.HandleOf(a.GetId()) == a);
    REQUIRE(second.HandleOf(a.GetId()) != a);
}
