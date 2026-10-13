#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using Fenrir::DynamicComponentType;
using Fenrir::EntityList;

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
    CHECK(DynamicComponentType("Test.Ok", 8, 4).IsValid());
    CHECK(DynamicComponentType("Test.Tag", 1, 1).IsValid());
    CHECK(DynamicComponentType("Test.Largest", DynamicComponentType::MaxSize, 16).IsValid());

    CHECK_FALSE(DynamicComponentType("", 8, 4).IsValid());
    CHECK_FALSE(DynamicComponentType("Test.Empty", 0, 1).IsValid());
    CHECK_FALSE(DynamicComponentType("Test.NotMultiple", 7, 4).IsValid());
    CHECK_FALSE(DynamicComponentType("Test.AlignThree", 6, 3).IsValid());
    CHECK_FALSE(DynamicComponentType("Test.AlignZero", 8, 0).IsValid());
    CHECK_FALSE(DynamicComponentType("Test.Huge", DynamicComponentType::MaxSize + 4, 4).IsValid());
}

TEST_CASE("the same name gives the same id", "[dynamic]")
{
    CHECK(DynamicComponentType("Test.Same", 8, 4).Id() == DynamicComponentType("Test.Same", 16, 8).Id());
}

TEST_CASE("an impossible description is refused when added", "[dynamic]")
{
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    CHECK_FALSE(entities.AddDynamic(DynamicComponentType("Test.Bad", 7, 4), entity));
    CHECK_FALSE(entities.AddDynamic(DynamicComponentType("Test.Bad", 8, 3), entity));
    CHECK_FALSE(entities.AddDynamic(DynamicComponentType("", 8, 4), entity));
    CHECK_FALSE(entities.AddDynamic(DynamicComponentType("Test.Huge", DynamicComponentType::MaxSize + 4, 4), entity));
}

TEST_CASE("asking about a dynamic type nobody added finds nothing and makes no storage", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType unused("Test.Unused", 8, 4);
    const auto entity = entities.CreateEntity().GetId();
    const std::size_t storages = entities.ComponentStorageCount();

    CHECK(entities.GetDynamic(unused, entity) == nullptr);
    CHECK_FALSE(entities.HasDynamic(unused, entity));
    CHECK(entities.CountDynamic(unused) == 0);
    CHECK(entities.RemoveDynamic(unused, entity));
    CHECK(entities.ClearDynamic(unused) == 0);
    CHECK(entities.ComponentStorageCount() == storages);
}

TEST_CASE("a dynamic component is zeroed when added, every time", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Zeroed", 16, 8);
    const auto entity = entities.CreateEntity().GetId();

    REQUIRE(entities.AddDynamic(type, entity));
    auto* bytes = static_cast<std::uint8_t*>(entities.GetDynamic(type, entity));
    REQUIRE(bytes != nullptr);
    CHECK(AllZero(bytes, 16));

    std::fill(bytes, bytes + 16, std::uint8_t{0xFF});
    REQUIRE(entities.RemoveDynamic(type, entity));
    REQUIRE(entities.AddDynamic(type, entity));
    CHECK(AllZero(entities.GetDynamic(type, entity), 16));
}

TEST_CASE("adding a dynamic component twice keeps the one it has", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Twice", 8, 4);
    const auto entity = entities.CreateEntity().GetId();

    REQUIRE(entities.AddDynamic(type, entity));
    auto* bytes = static_cast<std::uint8_t*>(entities.GetDynamic(type, entity));
    bytes[0] = 7;

    CHECK(entities.AddDynamic(type, entity));
    CHECK(entities.GetDynamic(type, entity) == bytes);
    CHECK(bytes[0] == 7);
    CHECK(entities.CountDynamic(type) == 1);
}

TEST_CASE("a dynamic component goes with its entity, and a dead entity cannot be given one", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Mortal", 8, 4);
    const auto entity = entities.CreateEntity().GetId();
    REQUIRE(entities.AddDynamic(type, entity));
    REQUIRE(entities.CountDynamic(type) == 1);

    entities.DestroyEntity(entity);

    CHECK_FALSE(entities.HasDynamic(type, entity));
    CHECK(entities.GetDynamic(type, entity) == nullptr);
    CHECK_FALSE(entities.RemoveDynamic(type, entity));
    CHECK_FALSE(entities.AddDynamic(type, entity));
    CHECK(entities.CountDynamic(type) == 0);
}

TEST_CASE("components honour their alignment and do not move when more are added", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Aligned", 32, 16);
    constexpr int Count = 300;

    std::vector<std::uint32_t> ids;
    std::vector<void*> addresses;
    for (int index = 0; index < Count; ++index)
    {
        ids.push_back(entities.CreateEntity().GetId());
        REQUIRE(entities.AddDynamic(type, ids.back()));
        addresses.push_back(entities.GetDynamic(type, ids.back()));
        CHECK(IsAligned(addresses.back(), 16));
    }
    CHECK(std::set<void*>(addresses.begin(), addresses.end()).size() == static_cast<std::size_t>(Count));

    // Adding for more entities, of this type and of another, moves nobody's component. (Removing one does: the
    // documented pointer is good only until a component of the type is removed.)
    const DynamicComponentType other("Test.Other", 8, 8);
    for (int index = 0; index < 500; ++index)
    {
        const auto id = entities.CreateEntity().GetId();
        REQUIRE(entities.AddDynamic(type, id));
        REQUIRE(entities.AddDynamic(other, id));
    }
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        CHECK(entities.GetDynamic(type, ids[index]) == addresses[index]);
    }
    CHECK(entities.CountDynamic(type) == static_cast<std::size_t>(Count + 500));
}

TEST_CASE("the largest allowed component is aligned and does not overlap its neighbours", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType big("Test.Big", DynamicComponentType::MaxSize, 16);

    std::vector<std::uint8_t*> starts;
    for (int index = 0; index < 3; ++index)
    {
        const auto id = entities.CreateEntity().GetId();
        REQUIRE(entities.AddDynamic(big, id));
        starts.push_back(static_cast<std::uint8_t*>(entities.GetDynamic(big, id)));
        REQUIRE(starts.back() != nullptr);
        CHECK(IsAligned(starts.back(), 16));
    }
    std::sort(starts.begin(), starts.end());
    CHECK(starts[1] - starts[0] >= static_cast<std::ptrdiff_t>(DynamicComponentType::MaxSize));
    CHECK(starts[2] - starts[1] >= static_cast<std::ptrdiff_t>(DynamicComponentType::MaxSize));
}

TEST_CASE("two names keep separate storage", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType first("Test.First", 8, 4);
    const DynamicComponentType second("Test.Second", 8, 4);
    const auto entity = entities.CreateEntity().GetId();
    const auto other = entities.CreateEntity().GetId();

    REQUIRE(entities.AddDynamic(first, entity));
    REQUIRE(entities.AddDynamic(second, other));

    CHECK(entities.HasDynamic(first, entity));
    CHECK_FALSE(entities.HasDynamic(second, entity));
    CHECK(entities.HasDynamic(second, other));
    CHECK_FALSE(entities.HasDynamic(first, other));
    CHECK(entities.CountDynamic(first) == 1);
    CHECK(entities.CountDynamic(second) == 1);
}

TEST_CASE("a name cannot take another shape while components of the first shape exist", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType small("Test.Shape", 8, 4);
    const DynamicComponentType wider("Test.Shape", 16, 8);
    const auto entity = entities.CreateEntity().GetId();
    REQUIRE(entities.AddDynamic(small, entity));
    void* const where = entities.GetDynamic(small, entity);

    CHECK_FALSE(entities.AddDynamic(wider, entity));
    CHECK(entities.GetDynamic(small, entity) == where);

    // Once none are left, the name takes the new shape.
    REQUIRE(entities.RemoveDynamic(small, entity));
    REQUIRE(entities.AddDynamic(wider, entity));
    CHECK(IsAligned(entities.GetDynamic(wider, entity), 8));
}

TEST_CASE("two names with the same id do not share storage", "[dynamic]")
{
    // Found by brute force against the name hash: these two names have the same 32-bit id.
    const DynamicComponentType owner("Test.Collide.39238", 8, 4);
    const DynamicComponentType intruder("Test.Collide.644126", 8, 4);
    REQUIRE(owner.Id() == intruder.Id());
    REQUIRE(owner.Name() != intruder.Name());

    EntityList entities;
    const auto holder = entities.CreateEntity().GetId();
    REQUIRE(entities.AddDynamic(owner, holder));

    CHECK_FALSE(entities.AddDynamic(intruder, holder));
    CHECK_FALSE(entities.HasDynamic(intruder, holder));
    CHECK(entities.GetDynamic(intruder, holder) == nullptr);
    CHECK(entities.CountDynamic(intruder) == 0);

    // Removing and clearing through the intruder leaves the owner alone.
    (void)entities.RemoveDynamic(intruder, holder);
    CHECK(entities.ClearDynamic(intruder) == 0);
    CHECK(entities.HasDynamic(owner, holder));
    CHECK(entities.CountDynamic(owner) == 1);
}

TEST_CASE("clearing a type removes it from every entity and reports how many", "[dynamic]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Cleared", 8, 4);
    const DynamicComponentType kept("Test.Kept", 8, 4);
    std::vector<std::uint32_t> ids;
    for (int index = 0; index < 5; ++index)
    {
        ids.push_back(entities.CreateEntity().GetId());
        REQUIRE(entities.AddDynamic(type, ids.back()));
    }
    REQUIRE(entities.AddDynamic(kept, ids.front()));

    CHECK(entities.ClearDynamic(type) == 5);

    CHECK(entities.CountDynamic(type) == 0);
    for (const auto id : ids)
    {
        CHECK_FALSE(entities.HasDynamic(type, id));
        CHECK(entities.HasEntity(id));
    }
    CHECK(entities.HasDynamic(kept, ids.front()));

    // Afterwards the name may be used with a different shape.
    CHECK(entities.AddDynamic(DynamicComponentType("Test.Cleared", 32, 16), ids.front()));
}
