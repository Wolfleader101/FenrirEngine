#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using Fenrir::ComponentType;
using Fenrir::DynamicComponentType;
using Fenrir::EntityList;

// What only dynamic component types do. What every component type does, and the rest of the dynamic-only behaviour
// (impossible types, shape conflicts, colliding names, storage made only on add, alignment), is in ComponentTypes.cpp.

namespace
{
    bool IsAligned(const void* pointer, std::size_t alignment)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
    }

    bool AllZero(const void* bytes, std::size_t size)
    {
        const auto* begin = static_cast<const std::uint8_t*>(bytes);
        return std::all_of(begin, begin + size, [](std::uint8_t byte) { return byte == 0; });
    }
} // namespace

TEST_CASE("a dynamic type is valid only with a name, a power-of-two alignment and a size that fits", "[dynamic]")
{
    INFO("a name, a size that is a multiple of a power-of-two alignment, and at most the maximum size are valid");
    REQUIRE(DynamicComponentType("Test.Ok", 8, 4).IsValid());
    REQUIRE(DynamicComponentType("Test.Tag", 1, 1).IsValid());
    REQUIRE(DynamicComponentType("Test.Largest", DynamicComponentType::MaxSize, 16).IsValid());

    INFO("no name, no size, a size not a multiple of the alignment, a bad alignment or too big a size are not");
    REQUIRE(!DynamicComponentType("", 8, 4).IsValid());
    REQUIRE(!DynamicComponentType("Test.Empty", 0, 1).IsValid());
    REQUIRE(!DynamicComponentType("Test.NotMultiple", 7, 4).IsValid());
    REQUIRE(!DynamicComponentType("Test.AlignThree", 6, 3).IsValid());
    REQUIRE(!DynamicComponentType("Test.AlignZero", 8, 0).IsValid());
    REQUIRE(!DynamicComponentType("Test.Huge", DynamicComponentType::MaxSize + 4, 4).IsValid());
}

TEST_CASE("the same name gives the same id", "[dynamic]")
{
    INFO("two descriptions with one name have one id, whatever their shapes");
    REQUIRE(DynamicComponentType("Test.Same", 8, 4).Id() == DynamicComponentType("Test.Same", 16, 8).Id());
}

TEST_CASE("a dynamic component is zeroed every time it is added", "[dynamic]")
{
    EntityList entities;
    const ComponentType type = ComponentType::Dynamic(DynamicComponentType("Test.Zeroed", 16, 8));
    const auto entity = entities.CreateEntity().GetId();
    INFO("the first add succeeds");
    REQUIRE(entities.Add(type, entity));
    auto* bytes = static_cast<std::uint8_t*>(entities.Get(type, entity));
    std::fill(bytes, bytes + 16, std::uint8_t{0xFF});

    INFO("removed and added again, it is all zero, not the bytes written before");
    REQUIRE(entities.Remove(type, entity));
    REQUIRE(entities.Add(type, entity));
    REQUIRE(AllZero(entities.Get(type, entity), 16));
}

TEST_CASE("the largest allowed component is aligned and does not overlap its neighbours", "[dynamic]")
{
    EntityList entities;
    const ComponentType big =
        ComponentType::Dynamic(DynamicComponentType("Test.Big", DynamicComponentType::MaxSize, 16));

    std::vector<std::uint8_t*> starts;
    for (int index = 0; index < 3; ++index)
    {
        const auto id = entities.CreateEntity().GetId();
        INFO("component " << index << " is added and aligned to 16");
        REQUIRE(entities.Add(big, id));
        starts.push_back(static_cast<std::uint8_t*>(entities.Get(big, id)));
        REQUIRE(starts.back() != nullptr);
        REQUIRE(IsAligned(starts.back(), 16));
    }
    std::sort(starts.begin(), starts.end());

    INFO("each component starts at least the maximum size after the one before it");
    REQUIRE(starts[1] - starts[0] >= static_cast<std::ptrdiff_t>(DynamicComponentType::MaxSize));
    REQUIRE(starts[2] - starts[1] >= static_cast<std::ptrdiff_t>(DynamicComponentType::MaxSize));
}

TEST_CASE("two dynamic names keep separate storage", "[dynamic]")
{
    EntityList entities;
    const ComponentType first = ComponentType::Dynamic(DynamicComponentType("Test.First", 8, 4));
    const ComponentType second = ComponentType::Dynamic(DynamicComponentType("Test.Second", 8, 4));
    const auto entity = entities.CreateEntity().GetId();
    const auto other = entities.CreateEntity().GetId();
    INFO("each name is added to a different entity");
    REQUIRE(entities.Add(first, entity));
    REQUIRE(entities.Add(second, other));

    INFO("each entity has only the component it was given");
    REQUIRE(entities.Has(first, entity));
    REQUIRE(!entities.Has(second, entity));
    REQUIRE(entities.Has(second, other));
    REQUIRE(!entities.Has(first, other));

    INFO("each name counts one");
    REQUIRE(entities.Count(first) == 1);
    REQUIRE(entities.Count(second) == 1);
}

TEST_CASE("clearing one dynamic type leaves the others", "[dynamic]")
{
    EntityList entities;
    const ComponentType cleared = ComponentType::Dynamic(DynamicComponentType("Test.Cleared", 8, 4));
    const ComponentType kept = ComponentType::Dynamic(DynamicComponentType("Test.Kept", 8, 4));
    const auto entity = entities.CreateEntity().GetId();
    INFO("the entity is given both");
    REQUIRE(entities.Add(cleared, entity));
    REQUIRE(entities.Add(kept, entity));

    INFO("clearing one removes only that one");
    REQUIRE(entities.Clear(cleared) == 1);
    REQUIRE(!entities.Has(cleared, entity));
    REQUIRE(entities.Has(kept, entity));
}

TEST_CASE("a dynamic name takes a new shape once its last component is removed", "[dynamic]")
{
    EntityList entities;
    const ComponentType small = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 8, 4));
    const ComponentType wider = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 16, 8));
    const auto entity = entities.CreateEntity().GetId();
    INFO("the first shape is added and then removed");
    REQUIRE(entities.Add(small, entity));
    REQUIRE(entities.Remove(small, entity));

    INFO("with none left, the name is added with the new shape, aligned to it");
    REQUIRE(entities.Add(wider, entity));
    REQUIRE(IsAligned(entities.Get(wider, entity), 8));
}
