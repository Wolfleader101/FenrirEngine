// The query module's rules, with no .NET: CountQueryRows and FillQueryRows over an EntityList the case fills and a
// ComponentRegistry it registers its components in, through Queries.hpp. The module is private to FenrirScripting
// (this executable is given its source directory) and is tested through its interface.
#include <catch2/catch_test_macros.hpp>

#include "HeadlessApp.hpp"
#include "RulesTypes.hpp"

#include "ComponentRegistry.hpp"
#include "Queries.hpp"

#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    using namespace Fenrir;
    using Fenrir::Detail::ComponentRegistry;
    using Fenrir::Detail::CountQueryRows;
    using Fenrir::Detail::FillQueryRows;
    using RulesTest::Velocity;

    // A second native component, so a query can list two.
    struct Mass
    {
        float Value = 1;
    };
} // namespace

template <>
struct Iris::StructMapping<Mass>
{
    static constexpr std::string_view typeName = "Rules.Mass";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Mass, Value));
};

namespace
{
    // A word a row was never written over: the rows a case hands in are filled with it first.
    constexpr std::uintptr_t Untouched = std::numeric_limits<std::uintptr_t>::max();

    // An App with no window whose active scene's entities are queried, and a registry holding Velocity then Mass.
    struct World
    {
        Testing::HeadlessApp headless;
        ComponentRegistry registry;
        std::int32_t velocity = registry.Components().Add(MakeComponent<Velocity>("Rules.Velocity"));
        std::int32_t mass = registry.Components().Add(MakeComponent<Mass>("Rules.Mass"));

        World()
        {
            REQUIRE((velocity == 0 && mass == 1));
        }

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        const ComponentType& TypeOf(std::int32_t id)
        {
            return registry.Components().Get(id)->type;
        }

        // A new entity holding each listed component.
        std::uint32_t Spawn(std::initializer_list<std::int32_t> ids)
        {
            const std::uint32_t entity = Entities().CreateEntity().GetId();
            for (const std::int32_t id : ids)
            {
                REQUIRE(Entities().Add(TypeOf(id), entity));
            }
            return entity;
        }

        std::uintptr_t AddressOf(std::int32_t id, std::uint32_t entity)
        {
            return reinterpret_cast<std::uintptr_t>(Entities().Get(TypeOf(id), entity));
        }

        std::int32_t Count(std::initializer_list<std::int32_t> ids)
        {
            return CountQueryRows(Entities(), registry, std::vector<std::int32_t>(ids));
        }

        std::int32_t Fill(std::initializer_list<std::int32_t> ids, std::vector<std::uintptr_t>& rows,
                          std::int32_t capacity)
        {
            return FillQueryRows(Entities(), registry, std::vector<std::int32_t>(ids), rows.data(), capacity);
        }
    };

    // The entity values of the rows written for `ids`, in the order they were written.
    std::vector<std::uintptr_t> EntitiesOf(const std::vector<std::uintptr_t>& rows, std::size_t idCount,
                                           std::int32_t written)
    {
        std::vector<std::uintptr_t> entities;
        for (std::int32_t row = 0; row < written; ++row)
        {
            entities.push_back(rows[static_cast<std::size_t>(row) * (idCount + 1)]);
        }
        return entities;
    }
} // namespace

TEST_CASE("A query matches the entities that have every listed component, and only those", "[queries]")
{
    World world;
    const std::uint32_t both = world.Spawn({world.velocity, world.mass});
    world.Spawn({world.velocity});
    const std::uint32_t bothAgain = world.Spawn({world.velocity, world.mass});
    world.Spawn({world.mass});
    world.Spawn({});

    INFO("The count should be the entities holding every listed component, not any of them");
    REQUIRE(world.Count({world.velocity, world.mass}) == 2);
    REQUIRE(world.Count({world.velocity}) == 3);
    REQUIRE(world.Count({world.mass}) == 3);

    std::vector<std::uintptr_t> rows(3 * 5, Untouched);
    const std::int32_t written = world.Fill({world.velocity, world.mass}, rows, 5);
    INFO("Fill should write a row for each match and no other");
    REQUIRE(written == 2);
    const auto entities = EntitiesOf(rows, 2, written);
    REQUIRE((entities == std::vector<std::uintptr_t>{both, bothAgain} ||
             entities == std::vector<std::uintptr_t>{bothAgain, both}));
    REQUIRE(rows[2 * 3] == Untouched);
}

TEST_CASE("An id no component is registered under makes a count -1 and a fill negative", "[queries]")
{
    World world;
    world.Spawn({world.velocity, world.mass});
    const std::int32_t unregistered = world.mass + 1;

    INFO("A count over an unregistered id should be -1, however many entities match the rest");
    REQUIRE(world.Count({world.velocity, unregistered}) == -1);
    REQUIRE(world.Count({unregistered}) == -1);
    REQUIRE(world.Count({-1}) == -1);

    std::vector<std::uintptr_t> rows(3, Untouched);
    INFO("A fill over an unregistered id should be negative and write nothing");
    REQUIRE(world.Fill({world.velocity, unregistered}, rows, 1) < 0);
    REQUIRE(world.Fill({-1}, rows, 1) < 0);
    REQUIRE(rows == std::vector<std::uintptr_t>(3, Untouched));
}

TEST_CASE("A fill writes no more rows than its capacity, and returns how many it wrote", "[queries]")
{
    World world;
    for (int entity = 0; entity < 4; ++entity)
    {
        world.Spawn({world.velocity});
    }
    REQUIRE(world.Count({world.velocity}) == 4);

    std::vector<std::uintptr_t> rows(2 * 4, Untouched);
    INFO("A capacity below the match count should truncate the rows to it");
    REQUIRE(world.Fill({world.velocity}, rows, 2) == 2);
    INFO("The rows past the capacity should be left as they were");
    REQUIRE((rows[4] == Untouched && rows[5] == Untouched && rows[6] == Untouched && rows[7] == Untouched));
    INFO("A capacity of zero should write nothing");
    std::vector<std::uintptr_t> none(2, Untouched);
    REQUIRE(world.Fill({world.velocity}, none, 0) == 0);
    REQUIRE(none == std::vector<std::uintptr_t>(2, Untouched));
    INFO("A capacity above the match count should write every match");
    std::vector<std::uintptr_t> all(2 * 6, Untouched);
    REQUIRE(world.Fill({world.velocity}, all, 6) == 4);
}

TEST_CASE("A row is the entity value, then the address of its component for each id in order", "[queries]")
{
    World world;
    const std::uint32_t entity = world.Spawn({world.velocity, world.mass});

    std::vector<std::uintptr_t> rows(3, Untouched);
    REQUIRE(world.Fill({world.mass, world.velocity}, rows, 1) == 1);
    INFO("The first word should be the entity value");
    REQUIRE(rows[0] == entity);
    INFO("The addresses should follow in the order the ids were listed, as the EntityList hands them out");
    REQUIRE(rows[1] == world.AddressOf(world.mass, entity));
    REQUIRE(rows[2] == world.AddressOf(world.velocity, entity));

    INFO("Listing the ids the other way round should swap the addresses");
    REQUIRE(world.Fill({world.velocity, world.mass}, rows, 1) == 1);
    REQUIRE((rows[0] == entity && rows[1] == world.AddressOf(world.velocity, entity) &&
             rows[2] == world.AddressOf(world.mass, entity)));
}

TEST_CASE("An empty id list counts no rows and fills none", "[queries]")
{
    World world;
    world.Spawn({world.velocity, world.mass});
    world.Spawn({});

    INFO("With nothing to match, a count should be 0, not every entity and not an error");
    REQUIRE(world.Count({}) == 0);
    std::vector<std::uintptr_t> rows(2, Untouched);
    INFO("With nothing to match, a fill should write nothing and return 0");
    REQUIRE(world.Fill({}, rows, 2) == 0);
    REQUIRE(rows == std::vector<std::uintptr_t>(2, Untouched));
}

TEST_CASE("Ids resolve against the registry as it is at each call, so a change between a count and a fill is seen",
          "[queries]")
{
    World world;
    const std::uint32_t entity = world.Spawn({world.velocity, world.mass});
    const std::uint32_t generation = world.registry.Generation();
    REQUIRE(world.Count({world.velocity, world.mass}) == 1);

    SECTION("a registration added in between leaves the ids meaning what they did")
    {
        REQUIRE(world.registry.Components().Add(MakeComponent<RulesTest::Velocity>("Rules.Later")) == 2);
        REQUIRE(world.registry.Generation() != generation);

        std::vector<std::uintptr_t> rows(3, Untouched);
        INFO("The fill should still find the row the count promised");
        REQUIRE(world.Fill({world.velocity, world.mass}, rows, 1) == 1);
        REQUIRE((rows[0] == entity && rows[1] == world.AddressOf(world.velocity, entity) &&
                 rows[2] == world.AddressOf(world.mass, entity)));
    }
    SECTION("a registration removed in between makes an id past the table unregistered")
    {
        world.registry.Components().RemoveWhere(
            [](const ComponentRegistration& registration) { return registration.name == "Rules.Velocity"; });
        REQUIRE(world.registry.Generation() != generation);

        std::vector<std::uintptr_t> rows(3, Untouched);
        INFO("The id Mass had is past the table now, so the fill should refuse it rather than guess");
        REQUIRE(world.Fill({world.velocity, world.mass}, rows, 1) < 0);
        REQUIRE(rows == std::vector<std::uintptr_t>(3, Untouched));
        INFO("Mass's registration moved to the id Velocity had, which is why C# re-resolves on a new generation");
        REQUIRE(world.Count({world.velocity}) == 1);
        REQUIRE(world.registry.Components().Get(world.velocity)->name == "Rules.Mass");
    }
}
