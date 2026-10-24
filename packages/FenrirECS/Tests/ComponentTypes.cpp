#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <cstdint>
#include <set>
#include <span>
#include <vector>

using Fenrir::ComponentType;
using Fenrir::DynamicComponentType;
using Fenrir::EntityList;

namespace
{
    // Two ints: the layout both kinds of the shared tests have. Its default is all zero, as a dynamic one's is.
    struct Pair
    {
        std::int32_t First = 0;
        std::int32_t Second = 0;
    };

    // A native component whose default is not all zero, so default construction can be told from zeroing.
    struct Defaulted
    {
        std::int32_t Value = 42;
    };

    // The two ways to make a component type of Pair's layout. The tests below that take a Kind run once for each, so
    // what they check holds for native and dynamic components alike.
    struct NativeKind
    {
        static ComponentType Make()
        {
            return ComponentType::Native<Pair>();
        }
    };

    struct DynamicKind
    {
        static ComponentType Make()
        {
            return ComponentType::Dynamic(DynamicComponentType("Test.Pair", sizeof(Pair), alignof(Pair)));
        }
    };

    bool IsAligned(const void* pointer, std::size_t alignment)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
    }

    // The entities a query over these types visits, as a set so that order does not matter.
    std::set<std::uint32_t> Visited(EntityList& entities, const std::vector<ComponentType>& types)
    {
        std::set<std::uint32_t> visited;
        entities.ForEachWith(std::span<const ComponentType>(types),
                             [&](std::uint32_t entity) { visited.insert(entity); });
        return visited;
    }
} // namespace

// Behaviour every component type has, native or dynamic.

TEST_CASE("a component type reports the layout of its components", "[component-type]")
{
    const ComponentType native = ComponentType::Native<Pair>();
    INFO("a native type has its C++ type's size and alignment, and is not dynamic");
    REQUIRE((native.Size() == 8 && native.Alignment() == 4 && !native.IsDynamic()));

    const ComponentType dynamic = ComponentType::Dynamic(DynamicComponentType("Test.Wide", 32, 16));
    INFO("a dynamic type has the size and alignment it was described with, and is dynamic");
    REQUIRE((dynamic.Size() == 32 && dynamic.Alignment() == 16 && dynamic.IsDynamic()));
}

TEMPLATE_TEST_CASE("an added component is there and all zero", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();

    INFO("before anything is added the entity has no component of the type");
    REQUIRE(!entities.Has(type, entity));
    REQUIRE(entities.Get(type, entity) == nullptr);

    INFO("adding to a live entity succeeds");
    REQUIRE(entities.Add(type, entity));

    INFO("the entity then has it");
    REQUIRE(entities.Has(type, entity));

    INFO("and its bytes are zero");
    const auto* added = static_cast<const Pair*>(entities.Get(type, entity));
    REQUIRE(added != nullptr);
    REQUIRE((added->First == 0 && added->Second == 0));
}

TEMPLATE_TEST_CASE("adding a component the entity already has keeps it", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();
    INFO("the first add succeeds");
    REQUIRE(entities.Add(type, entity));
    auto* const before = static_cast<Pair*>(entities.Get(type, entity));
    before->First = 7;

    INFO("adding again succeeds");
    REQUIRE(entities.Add(type, entity));

    INFO("and the component is the same one, in the same place, with the value written before");
    REQUIRE(entities.Get(type, entity) == before);
    REQUIRE(before->First == 7);
    REQUIRE(entities.Count(type) == 1);
}

TEMPLATE_TEST_CASE("removing a component takes it away", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();
    INFO("the add succeeds");
    REQUIRE(entities.Add(type, entity));

    INFO("removing from a live entity succeeds");
    REQUIRE(entities.Remove(type, entity));

    INFO("afterwards the entity has no component of the type");
    REQUIRE(!entities.Has(type, entity));
    REQUIRE(entities.Get(type, entity) == nullptr);
}

TEMPLATE_TEST_CASE("removing a component a live entity does not have succeeds", "[component-type]", NativeKind,
                   DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();

    INFO("removing what was never added is not an error");
    REQUIRE(entities.Remove(type, entity));
}

TEMPLATE_TEST_CASE("a dead entity has no component", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();
    INFO("the add succeeds");
    REQUIRE(entities.Add(type, entity));
    entities.DestroyEntity(entity);

    INFO("a destroyed entity has no component, and get gives null");
    REQUIRE(!entities.Has(type, entity));
    REQUIRE(entities.Get(type, entity) == nullptr);

    INFO("asking does not bring the entity back");
    REQUIRE(!entities.HasEntity(entity));
}

TEMPLATE_TEST_CASE("a component goes with its entity", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();
    INFO("the add succeeds");
    REQUIRE(entities.Add(type, entity));
    REQUIRE(entities.Count(type) == 1);

    INFO("destroying the entity removes its component");
    entities.DestroyEntity(entity);
    REQUIRE(entities.Count(type) == 0);
}

TEMPLATE_TEST_CASE("a dead entity cannot be given or lose a component", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto entity = entities.CreateEntity().GetId();
    entities.DestroyEntity(entity);

    INFO("adding to a destroyed entity fails");
    REQUIRE(!entities.Add(type, entity));

    INFO("removing from a destroyed entity fails");
    REQUIRE(!entities.Remove(type, entity));

    INFO("neither brings the entity back");
    REQUIRE(!entities.HasEntity(entity));
}

TEMPLATE_TEST_CASE("count says how many entities have the component", "[component-type]", NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto first = entities.CreateEntity().GetId();
    const auto second = entities.CreateEntity().GetId();
    (void)entities.CreateEntity(); // Has none.

    INFO("no entity has one yet");
    REQUIRE(entities.Count(type) == 0);

    INFO("two entities are given one");
    REQUIRE(entities.Add(type, first));
    REQUIRE(entities.Add(type, second));
    REQUIRE(entities.Count(type) == 2);

    INFO("one is removed again");
    REQUIRE(entities.Remove(type, first));
    REQUIRE(entities.Count(type) == 1);
}

TEMPLATE_TEST_CASE("clearing a type removes it from every entity and reports how many there were", "[component-type]",
                   NativeKind, DynamicKind)
{
    EntityList entities;
    const ComponentType type = TestType::Make();
    const auto first = entities.CreateEntity().GetId();
    const auto second = entities.CreateEntity().GetId();
    INFO("both adds succeed");
    REQUIRE(entities.Add(type, first));
    REQUIRE(entities.Add(type, second));

    INFO("clearing reports the two components it removed");
    REQUIRE(entities.Clear(type) == 2);

    INFO("afterwards no entity has one");
    REQUIRE(entities.Count(type) == 0);
    REQUIRE(!entities.Has(type, first));
    REQUIRE(!entities.Has(type, second));

    INFO("and the entities themselves remain");
    REQUIRE(entities.HasEntity(first));
    REQUIRE(entities.HasEntity(second));

    INFO("clearing a type nobody has reports none");
    REQUIRE(entities.Clear(type) == 0);
}

TEST_CASE("a query over component types visits exactly the entities that have all of them, native and dynamic",
          "[component-type]")
{
    EntityList entities;
    const ComponentType native = ComponentType::Native<Pair>();
    const ComponentType dynamic = ComponentType::Dynamic(DynamicComponentType("Test.Tag", 4, 4));
    const auto both = entities.CreateEntity().GetId();
    const auto nativeOnly = entities.CreateEntity().GetId();
    const auto dynamicOnly = entities.CreateEntity().GetId();
    const auto alsoBoth = entities.CreateEntity().GetId();
    INFO("every add succeeds");
    for (const auto entity : {both, alsoBoth, nativeOnly})
    {
        REQUIRE(entities.Add(native, entity));
    }
    for (const auto entity : {both, alsoBoth, dynamicOnly})
    {
        REQUIRE(entities.Add(dynamic, entity));
    }

    INFO("only the two entities that have both components are visited");
    REQUIRE(Visited(entities, {native, dynamic}) == std::set<std::uint32_t>{both, alsoBoth});
}

TEST_CASE("a query over no component types visits nothing", "[component-type]")
{
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();
    INFO("the add succeeds");
    REQUIRE(entities.Add(ComponentType::Native<Pair>(), entity));

    INFO("with nothing to match, no entity is visited");
    REQUIRE(Visited(entities, {}).empty());
}

// Native component types only.

TEST_CASE("adding a native component default-constructs it", "[component-type]")
{
    EntityList entities;
    const ComponentType type = ComponentType::Native<Defaulted>();
    const auto entity = entities.CreateEntity().GetId();
    INFO("the add succeeds");
    REQUIRE(entities.Add(type, entity));

    INFO("the component has its default member value, 42, not zero");
    const auto* added = static_cast<const Defaulted*>(entities.Get(type, entity));
    REQUIRE(added != nullptr);
    REQUIRE(added->Value == 42);
}

TEST_CASE("asking about a native component never gives an entity the components every entity has", "[component-type]")
{
    EntityList entities;
    const ComponentType transform = ComponentType::Native<Fenrir::Transform>();
    const ComponentType name = ComponentType::Native<Fenrir::Name>();
    const ComponentType relationship = ComponentType::Native<Fenrir::Relationship>();
    const auto reserved = entities.ReserveEntity(); // Alive, with no components yet.

    INFO("a reserved entity has no Transform, Name or Relationship, and get gives null for each");
    for (const ComponentType& type : {transform, name, relationship})
    {
        REQUIRE(!entities.Has(type, reserved));
        REQUIRE(entities.Get(type, reserved) == nullptr);
    }

    INFO("asking did not give it any of them");
    for (const ComponentType& type : {transform, name, relationship})
    {
        REQUIRE(entities.Count(type) == 0);
    }
}

// Dynamic component types only.

TEST_CASE("asking about a dynamic type nobody added makes no storage", "[component-type]")
{
    EntityList entities;
    const ComponentType unused = ComponentType::Dynamic(DynamicComponentType("Test.Unused", 8, 4));
    const auto entity = entities.CreateEntity().GetId();
    const std::size_t storages = entities.ComponentStorageCount();

    INFO("get, has, count, remove, clear and a query all find nothing");
    REQUIRE(entities.Get(unused, entity) == nullptr);
    REQUIRE(!entities.Has(unused, entity));
    REQUIRE(entities.Count(unused) == 0);
    REQUIRE(entities.Remove(unused, entity));
    REQUIRE(entities.Clear(unused) == 0);
    REQUIRE(Visited(entities, {unused}).empty());

    INFO("and none of them made storage for the type");
    REQUIRE(entities.ComponentStorageCount() == storages);
}

TEST_CASE("an impossible dynamic type cannot be added", "[component-type]")
{
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    INFO("a size that is not a multiple of the alignment is refused");
    REQUIRE(!entities.Add(ComponentType::Dynamic(DynamicComponentType("Test.Bad", 7, 4)), entity));

    INFO("an alignment that is not a power of two is refused");
    REQUIRE(!entities.Add(ComponentType::Dynamic(DynamicComponentType("Test.Bad", 8, 3)), entity));

    INFO("a type with no name is refused");
    REQUIRE(!entities.Add(ComponentType::Dynamic(DynamicComponentType("", 8, 4)), entity));

    INFO("a type four bytes over the maximum size is refused");
    REQUIRE(!entities.Add(
        ComponentType::Dynamic(DynamicComponentType("Test.Huge", DynamicComponentType::MaxSize + 4, 4)), entity));
}

TEST_CASE("a dynamic name cannot take another shape while components of it exist", "[component-type]")
{
    EntityList entities;
    const ComponentType small = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 8, 4));
    const ComponentType wider = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 16, 8));
    const auto entity = entities.CreateEntity().GetId();
    INFO("the first shape is added");
    REQUIRE(entities.Add(small, entity));
    void* const where = entities.Get(small, entity);

    INFO("the name cannot be added with another size and alignment");
    REQUIRE(!entities.Add(wider, entity));

    INFO("and the existing component is untouched");
    REQUIRE(entities.Get(small, entity) == where);
}

TEST_CASE("a cleared dynamic name takes a new shape", "[component-type]")
{
    EntityList entities;
    const ComponentType small = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 8, 4));
    const ComponentType wider = ComponentType::Dynamic(DynamicComponentType("Test.Shape", 16, 8));
    const auto entity = entities.CreateEntity().GetId();
    INFO("the first shape is added and then cleared");
    REQUIRE(entities.Add(small, entity));
    REQUIRE(entities.Clear(small) == 1);

    INFO("the name is then added with the new shape, aligned to it");
    REQUIRE(entities.Add(wider, entity));
    REQUIRE(IsAligned(entities.Get(wider, entity), 8));
}

// A dynamic name whose id equals a native component's storage id is refused the same way (the ECS checks the storage
// under the id is its own), but no test here builds one: that would mean naming the ECS library's type hash.
TEST_CASE("a dynamic name whose id another name already uses is refused and leaves the owner alone", "[component-type]")
{
    // Found by brute force against the name hash: these two names have the same id.
    const DynamicComponentType ownerType("Test.Collide.39238", 8, 4);
    const DynamicComponentType intruderType("Test.Collide.644126", 8, 4);
    INFO("the two names share an id");
    REQUIRE(ownerType.Id() == intruderType.Id());
    const ComponentType owner = ComponentType::Dynamic(ownerType);
    const ComponentType intruder = ComponentType::Dynamic(intruderType);

    EntityList entities;
    const auto holder = entities.CreateEntity().GetId();
    INFO("the owner is added");
    REQUIRE(entities.Add(owner, holder));

    INFO("the intruder cannot be added and finds nothing");
    REQUIRE(!entities.Add(intruder, holder));
    REQUIRE(!entities.Has(intruder, holder));
    REQUIRE(entities.Get(intruder, holder) == nullptr);
    REQUIRE(entities.Count(intruder) == 0);

    INFO("removing and clearing through the intruder leaves the owner's component");
    (void)entities.Remove(intruder, holder);
    REQUIRE(entities.Clear(intruder) == 0);
    REQUIRE(entities.Has(owner, holder));
    REQUIRE(entities.Count(owner) == 1);
}

TEST_CASE("dynamic components honour their alignment and do not move when more are added", "[component-type]")
{
    EntityList entities;
    const ComponentType type = ComponentType::Dynamic(DynamicComponentType("Test.Aligned", 32, 16));
    const ComponentType other = ComponentType::Dynamic(DynamicComponentType("Test.Other", 8, 8));
    std::vector<std::uint32_t> ids;
    std::vector<void*> addresses;
    for (int index = 0; index < 300; ++index)
    {
        ids.push_back(entities.CreateEntity().GetId());
        INFO("component " << index << " is added and aligned to 16");
        REQUIRE(entities.Add(type, ids.back()));
        addresses.push_back(entities.Get(type, ids.back()));
        REQUIRE(IsAligned(addresses.back(), 16));
    }

    // More of this type and of another, past the first page. (Removing one does move the last into its place: the
    // documented pointer is good only until a component of the type is removed.)
    for (int index = 0; index < 500; ++index)
    {
        const auto entity = entities.CreateEntity().GetId();
        INFO("a later entity " << index << " is given both components");
        REQUIRE(entities.Add(type, entity));
        REQUIRE(entities.Add(other, entity));
    }

    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        INFO("component " << index << " is where it was before more were added");
        REQUIRE(entities.Get(type, ids[index]) == addresses[index]);
    }

    INFO("the first 300 components each have an address of their own, and all 800 are counted");
    REQUIRE(std::set<void*>(addresses.begin(), addresses.end()).size() == 300);
    REQUIRE(entities.Count(type) == 800);
}
