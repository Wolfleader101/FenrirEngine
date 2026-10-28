// Queries from C#: writes through rows landing natively, a fixed number of native calls, structural changes
// deferred during iteration and ForEach, empty and invalid queries, and the widest query.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    void SetVelocityX(Scripting& scripting, EntityList& entities, std::uint32_t id, float x)
    {
        auto* values = static_cast<float*>(entities.Get(scripting.FindComponent(VelocityName)->type, id));
        {
            INFO("The entity had no Velocity");
            REQUIRE(values != nullptr);
        }
        values[0] = x;
        values[1] = 0;
        values[2] = 0;
    }

    void WritesThroughQueryRowsAreVisibleNatively()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        // Two entities with a Transform (native) and a Velocity (script), one with only a Velocity, one with only a
        // Transform.
        const auto first = EntityWithVelocity(scripting, entities);
        const auto second = EntityWithVelocity(scripting, entities);
        const auto velocityOnly = entities.CreateEntity().GetId();
        {
            INFO("Velocity was not added");
            REQUIRE(entities.Add(scripting.FindComponent(VelocityName)->type, velocityOnly));
        }
        entities.GetEntity(velocityOnly).RemoveComponent<Transform>();
        const auto transformOnly = entities.CreateEntity().GetId();
        SetVelocityX(scripting, entities, first, 1);
        SetVelocityX(scripting, entities, second, 10);
        SetVelocityX(scripting, entities, velocityOnly, 100);
        {
            INFO("A single-component query missed an entity");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 111.0F);
        }

        // A query over a native and a script component visits exactly the entities that have both, and its writes land
        // in the engine's own storage.
        {
            INFO("The two-component query visited the wrong entities");
            REQUIRE(Queries<std::int32_t>(scripting, "Integrate") == 2);
        }
        {
            INFO("The query was not repeatable");
            REQUIRE(Queries<std::int32_t>(scripting, "Integrate") == 2);
        }
        {
            INFO("Writes through query rows did not reach the native Transform");
            REQUIRE((entities.GetEntity(first).GetComponent<Transform>().pos.x == 2.0F &&
                     entities.GetEntity(second).GetComponent<Transform>().pos.x == 20.0F));
        }
        {
            INFO("A query visited an entity it should not");
            REQUIRE(entities.GetEntity(transformOnly).GetComponent<Transform>().pos.x == 0.0F);
        }

        // Each row's entity owns the row's components.
        for (const auto id : {first, second})
        {
            {
                INFO("Counter was not added");
                REQUIRE(entities.Add(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, id));
            }
        }
        {
            INFO("A row's entity and its components disagreed");
            REQUIRE(Queries<bool>(scripting, "RowsAgree"));
        }

        // The callback form works the same.
        {
            INFO("ForEach visited the wrong entities");
            REQUIRE(Queries<std::int32_t>(scripting, "CountUp") == 2);
        }
        const auto* hits = static_cast<const std::int32_t*>(
            entities.Get(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, first));
        {
            INFO("A write through ForEach did not reach the component");
            REQUIRE((hits != nullptr && hits[0] == 1));
        }
    }

    void AQueryMakesTheSameNumberOfNativeCallsForAnyNumberOfEntities()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        int total = 0;
        const auto callsFor = [&](int more) {
            for (int index = 0; index < more; ++index)
            {
                SetVelocityX(scripting, entities, EntityWithVelocity(scripting, entities), 1);
            }
            total += more;
            const int count = total;
            const auto before = ScriptingProbe::QueryCalls(scripting);
            const auto start = std::chrono::steady_clock::now();
            const auto visited = Queries<std::int32_t>(scripting, "Integrate");
            const auto microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
            {
                INFO("The query did not visit every entity");
                REQUIRE(visited == count);
            }
            std::cout << "  query over " << count
                      << " entities (Transform + Velocity): " << (ScriptingProbe::QueryCalls(scripting) - before)
                      << " native calls, " << microseconds.count() << " us\n";
            return ScriptingProbe::QueryCalls(scripting) - before;
        };
        const auto small = callsFor(10);
        const auto large = callsFor(10000 - 10);
        {
            INFO("A query's native calls depended on the entity count");
            REQUIRE((small == large && small == 2));
        }
    }

    void StructuralChangesDuringIterationAreDeferred()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto* counter = scripting.FindComponent("Fenrir.ScriptFixture.Counter");
        std::vector<std::uint32_t> originals;
        for (int index = 0; index < 5; ++index)
        {
            originals.push_back(EntityWithVelocity(scripting, entities));
        }

        // The script removes each entity's Velocity, adds Counter, creates a new Velocity entity and destroys one
        // entity, all inside the loop. The loop still visits exactly the five it started with.
        {
            INFO("Iteration was disturbed by changes made inside it");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeWhileIterating") == 5);
        }
        {
            INFO("A change reached the storage during iteration");
            REQUIRE((entities.Count(velocity->type) == 5 && entities.Count(counter->type) == 0));
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A change was skipped: the destroy came last, so nothing named a dead entity");
            REQUIRE(summary.skipped == 0);
        }
        {
            INFO("Velocity should now be on the five new entities");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        {
            INFO("Counter should be on the four survivors");
            REQUIRE(entities.Count(counter->type) == 4);
        }
        for (const auto id : originals)
        {
            {
                INFO("An original kept its Velocity");
                REQUIRE(!entities.Has(velocity->type, id));
            }
        }

        // Nested queries each see the whole set.
        {
            INFO("Nested queries did not each see every entity");
            REQUIRE(Queries<std::int32_t>(scripting, "Nested") == 5 * 4);
        }
        {
            INFO("Leaving a loop early failed");
            REQUIRE(Queries<std::int32_t>(scripting, "StopEarly") == 2);
        }
        {
            INFO("A query after breaking out of one was wrong");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 500.0F);
        }
    }

    void ForEachDefersStructuralChangesToo()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        for (int index = 0; index < 5; ++index)
        {
            (void)EntityWithVelocity(scripting, entities);
        }
        // Five visited, however many the loop creates; the new ones appear (and the old ones go) at playback.
        {
            INFO("ForEach was disturbed by changes made inside it");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeInsideForEach") == 5);
        }
        {
            INFO("A change reached the storage during ForEach");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("ForEach's deferred changes were not applied");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        {
            INFO("The replacements were not queryable");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeInsideForEach") == 5);
        }
    }

    void EmptyAndInvalidQueriesBehave()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        entities.CreateEntity();

        // Nothing has the component, and nothing has been added: empty, with no storage made for it.
        const auto storages = entities.ComponentStorageCount();
        {
            INFO("An empty query visited something");
            REQUIRE(Queries<std::int32_t>(scripting, "VisitEmpty") == 0);
        }
        {
            INFO("An empty sum was not zero");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 0.0F);
        }
        {
            INFO("An empty query made storage");
            REQUIRE(entities.ComponentStorageCount() == storages);
        }

        // A component the engine never registered, and the same component twice: clear errors.
        const auto unregistered = Queries<std::optional<std::string>>(scripting, "QueryUnregistered").value_or("");
        {
            INFO("Querying an unregistered component said: " + unregistered);
            REQUIRE(unregistered.find("Test.Nothing") != std::string::npos);
        }
        const auto twice = Queries<std::optional<std::string>>(scripting, "QueryTwice").value_or("");
        {
            INFO("Querying a component twice said: " + twice);
            REQUIRE(twice.find("twice") != std::string::npos);
        }
    }

    void TheWidestQueryHasEightComponents()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::string prefix = "Fenrir.ScriptFixture.Q";

        // Three entities have all eight, with V = 1 in each; one has only seven of them.
        for (int entityIndex = 0; entityIndex < 4; ++entityIndex)
        {
            const auto id = entities.CreateEntity().GetId();
            for (int component = 1; component <= (entityIndex == 3 ? 7 : 8); ++component)
            {
                const auto* registration = scripting.FindComponent(prefix + std::to_string(component));
                {
                    INFO("A component was not added");
                    REQUIRE((registration != nullptr && entities.Add(registration->type, id)));
                }
                *static_cast<std::int32_t*>(entities.Get(registration->type, id)) = 1;
            }
        }
        {
            INFO("The eight-component query was wrong");
            REQUIRE(Queries<std::int32_t>(scripting, "AllEight") == 3 * 8);
        }
    }

} // namespace

TEST_CASE("writes through query rows are visible natively", "[scripting]")
{
    WritesThroughQueryRowsAreVisibleNatively();
}

TEST_CASE("a query makes the same number of native calls for any number of entities", "[scripting]")
{
    AQueryMakesTheSameNumberOfNativeCallsForAnyNumberOfEntities();
}

TEST_CASE("structural changes during iteration are deferred", "[scripting]")
{
    StructuralChangesDuringIterationAreDeferred();
}

TEST_CASE("ForEach defers structural changes too", "[scripting]")
{
    ForEachDefersStructuralChangesToo();
}

TEST_CASE("empty and invalid queries behave", "[scripting]")
{
    EmptyAndInvalidQueriesBehave();
}

TEST_CASE("the widest query has eight components", "[scripting]")
{
    TheWidestQueryHasEightComponents();
}
