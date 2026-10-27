// The change buffer's rules, with no .NET: a script's structural changes are recorded, then played back together, and
// every case here does what the engine does with them. The buffer is the package's own module (it is private to
// FenrirScripting, and this executable is given its source directory), tested through its interface with a real App,
// entity list and component registry.
#include <catch2/catch_test_macros.hpp>

#include "../HeadlessApp.hpp"
#include "RulesTypes.hpp"

#include "ChangeBuffer.hpp"
#include "ComponentRegistry.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace
{
    using namespace Fenrir;
    using Fenrir::Detail::ChangeBuffer;
    using Fenrir::Detail::ChangeError;
    using Fenrir::Detail::ComponentRegistry;
    using RulesTest::Ping;
    using RulesTest::Velocity;

    constexpr const char* VelocityName = "Rules.Velocity";
    constexpr const char* PingName = "Rules.Ping";
    constexpr const char* ScriptedEventName = "Rules.ScriptedEvent";

    // What a Scripting holds around a change buffer: an App, a registry with the engine's own components (Transform and
    // Name required, Relationship read-only) and a component and an event of the test's.
    struct World
    {
        Testing::HeadlessApp headless;
        ComponentRegistry registry;
        ChangeBuffer buffer;
        std::int32_t velocity = -1;
        std::int32_t transform = -1;
        std::int32_t relationship = -1;
        std::int32_t ping = -1;

        World()
        {
            RulesTest::AddEngineComponents(registry);
            transform = registry.Components().Find("Fenrir.Transform");
            relationship = registry.Components().Find("Fenrir.Relationship");
            velocity = registry.Components().Add(MakeComponent<Velocity>(VelocityName));
            auto registration = MakeEvent<Ping>(PingName);
            REQUIRE(registration.bind(headless.Get()));
            ping = registry.Events().Add(std::move(registration));
        }

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        [[nodiscard]] const ComponentType& TypeOf(std::int32_t id) const
        {
            return registry.Components().Get(id)->type;
        }

        std::uint32_t NewEntity()
        {
            return Entities().CreateEntity().GetId();
        }

        std::size_t EntityCount()
        {
            std::size_t count = 0;
            Entities().ForEachEntity([&](auto) { ++count; });
            return count;
        }

        // A change that must be accepted: records it, or fails the case.
        void Add(std::uint32_t entity, const Velocity* value)
        {
            INFO("The add was refused when it was recorded");
            REQUIRE(buffer.Add(Entities(), registry, velocity, entity, value) == ChangeError::None);
        }

        void Remove(std::uint32_t entity)
        {
            INFO("The remove was refused when it was recorded");
            REQUIRE(buffer.Remove(Entities(), registry, velocity, entity) == ChangeError::None);
        }

        void Destroy(std::uint32_t entity)
        {
            INFO("The destroy was refused when it was recorded");
            REQUIRE(buffer.Destroy(Entities(), entity) == ChangeError::None);
        }

        [[nodiscard]] const Velocity* VelocityOf(std::uint32_t entity)
        {
            return static_cast<const Velocity*>(Entities().Get(TypeOf(velocity), entity));
        }

        auto Apply()
        {
            return buffer.Apply(headless.Get(), registry);
        }
    };
} // namespace

TEST_CASE("playback applies changes in the order they were recorded", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    const Velocity first{1, 0, 0};
    const Velocity second{3, 0, 0};
    world.Add(entity, &first);
    world.Add(entity, &second);

    const auto result = world.Apply();

    INFO("Both adds should have been applied");
    REQUIRE(result.applied == 2);
    INFO("The later add should have won");
    REQUIRE(world.VelocityOf(entity) != nullptr);
    REQUIRE(world.VelocityOf(entity)->X == 3.0F);
}

TEST_CASE("an add, a replacing add, a remove and an add leave the last one", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    const Velocity a{1, 0, 0};
    const Velocity b{2, 0, 0};
    const Velocity c{3, 0, 0};
    world.Add(entity, &a);
    world.Add(entity, &b);
    world.Remove(entity);
    world.Add(entity, &c);

    const auto result = world.Apply();

    INFO("All four changes should have been applied");
    REQUIRE(result.applied == 4);
    INFO("The component should hold the last add's value");
    REQUIRE(world.VelocityOf(entity) != nullptr);
    REQUIRE(world.VelocityOf(entity)->X == 3.0F);
}

TEST_CASE("removing a component and adding it again gives a default one", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    const Velocity before{5, 6, 7};
    world.Add(entity, &before);
    (void)world.Apply();

    world.Remove(entity);
    world.Add(entity, nullptr);
    (void)world.Apply();

    INFO("The entity should have a component again");
    REQUIRE(world.VelocityOf(entity) != nullptr);
    INFO("It should be a default one, not the old values");
    REQUIRE((world.VelocityOf(entity)->X == 0.0F && world.VelocityOf(entity)->Y == 0.0F));
}

TEST_CASE("an add copies its value when it is recorded", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    Velocity value{1, 2, 3};
    world.Add(entity, &value);
    value = Velocity{9, 9, 9}; // The caller reuses its buffer before playback.

    (void)world.Apply();

    INFO("The component should hold the values at the time of the add");
    REQUIRE(world.VelocityOf(entity) != nullptr);
    REQUIRE((world.VelocityOf(entity)->X == 1.0F && world.VelocityOf(entity)->Y == 2.0F &&
             world.VelocityOf(entity)->Z == 3.0F));
}

TEST_CASE("an add after a destroy of the same entity is skipped with a warning", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    world.Destroy(entity);
    world.Add(entity, nullptr);

    const auto result = world.Apply();

    INFO("The destroy should be applied and the add skipped");
    REQUIRE((result.applied == 1 && result.skipped == 1));
    INFO("The entity should be gone");
    REQUIRE(!world.Entities().HasEntity(entity));
    INFO("The skipped add should have been reported");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn, "skipped adding a component for entity"));
}

TEST_CASE("a skipped add never reaches a new entity in the freed slot", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    world.Destroy(entity);
    world.Add(entity, nullptr);
    (void)world.Apply();

    const auto reused = world.NewEntity();

    INFO("The new entity should not have the skipped component");
    REQUIRE(world.Entities().Has(world.TypeOf(world.velocity), reused) == false);
    INFO("No entity should have one");
    REQUIRE(world.Entities().Count(world.TypeOf(world.velocity)) == 0);
}

TEST_CASE("an add before a destroy of the same entity goes with the entity", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    world.Add(entity, nullptr);
    world.Destroy(entity);

    const auto result = world.Apply();

    INFO("Both changes should have been applied, none skipped");
    REQUIRE((result.applied == 2 && result.skipped == 0));
    INFO("The entity and its component should be gone");
    REQUIRE(!world.Entities().HasEntity(entity));
    REQUIRE(world.Entities().Count(world.TypeOf(world.velocity)) == 0);
}

TEST_CASE("destroying an entity twice is harmless and not reported", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    INFO("Both destroys should be accepted when recorded");
    world.Destroy(entity);
    world.Destroy(entity);

    const auto result = world.Apply();

    INFO("Nothing should have been skipped");
    REQUIRE(result.skipped == 0);
    INFO("The entity should be gone");
    REQUIRE(!world.Entities().HasEntity(entity));
    INFO("The second destroy should not have been warned about");
    REQUIRE(!world.headless.Logger().Contains(Testing::LogLevel::Warn, "skipped"));
}

TEST_CASE("an entity created and destroyed in one batch leaves nothing behind", "[change-buffer]")
{
    World world;
    const std::size_t before = world.EntityCount();
    const auto created = world.buffer.Create(world.Entities());
    world.Destroy(created);

    (void)world.Apply();

    INFO("The entity should be gone and the count back where it was");
    REQUIRE((!world.Entities().HasEntity(created) && world.EntityCount() == before));
}

TEST_CASE("a created entity is alive at once but has no components until playback", "[change-buffer]")
{
    World world;
    const auto created = world.buffer.Create(world.Entities());

    INFO("The id should be usable straight away");
    REQUIRE(world.Entities().HasEntity(created));
    INFO("It should have no Transform yet");
    REQUIRE(!world.Entities().GetEntity(created).HasComponent<Transform>());
}

TEST_CASE("playback gives a created entity the engine's components and a name", "[change-buffer]")
{
    World world;
    const auto created = world.buffer.Create(world.Entities());

    const auto result = world.Apply();

    INFO("The create should be applied");
    REQUIRE(result.applied == 1);
    auto entity = world.Entities().GetEntity(created);
    INFO("The entity should have Transform, Name and Relationship");
    REQUIRE((entity.HasComponent<Transform>() && entity.HasComponent<Name>() && entity.HasComponent<Relationship>()));
    INFO("Its name should be an entity name");
    REQUIRE(std::string(entity.GetComponent<Name>().Get()).starts_with("Entity "));
}

TEST_CASE("a component can be added to an entity created in the same batch", "[change-buffer]")
{
    World world;
    const auto created = world.buffer.Create(world.Entities());
    const Velocity value{4, 0, 0};
    world.Add(created, &value);

    const auto result = world.Apply();

    INFO("The create and the add should both be applied");
    REQUIRE((result.applied == 2 && result.skipped == 0));
    INFO("The new entity should have the component with its value");
    REQUIRE(world.VelocityOf(created) != nullptr);
    REQUIRE(world.VelocityOf(created)->X == 4.0F);
}

TEST_CASE("discarding drops every recorded change", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    world.Add(entity, nullptr);

    world.buffer.Discard(world.Entities());

    INFO("The buffer should be empty");
    REQUIRE((world.buffer.Empty() && world.buffer.Size() == 0));
    INFO("The dropped add should not be applied");
    REQUIRE(world.Apply().applied == 0);
    REQUIRE(world.VelocityOf(entity) == nullptr);
}

TEST_CASE("a discarded remove leaves the component alone", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    const Velocity value{5, 0, 0};
    world.Add(entity, &value);
    (void)world.Apply();
    world.Remove(entity);

    world.buffer.Discard(world.Entities());

    INFO("The entity should still have its component with its value");
    REQUIRE(world.VelocityOf(entity) != nullptr);
    REQUIRE(world.VelocityOf(entity)->X == 5.0F);
}

TEST_CASE("discarding destroys the entities created and not yet played back", "[change-buffer]")
{
    World world;
    const std::size_t before = world.EntityCount();
    const auto created = world.buffer.Create(world.Entities());
    INFO("The created entity should be alive until it is discarded");
    REQUIRE(world.Entities().HasEntity(created));

    world.buffer.Discard(world.Entities());

    INFO("The created entity should be gone and none left behind");
    REQUIRE((!world.Entities().HasEntity(created) && world.EntityCount() == before));
}

TEST_CASE("adding or removing an unknown component is refused", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    constexpr std::int32_t unknown = 999;

    INFO("An add of an unknown component should say so");
    REQUIRE(world.buffer.Add(world.Entities(), world.registry, unknown, entity, nullptr) ==
            ChangeError::UnknownComponent);
    INFO("A remove of an unknown component should say so");
    REQUIRE(world.buffer.Remove(world.Entities(), world.registry, unknown, entity) == ChangeError::UnknownComponent);
}

TEST_CASE("adding or removing a read-only component is refused", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();

    INFO("An add of the read-only Relationship should say it is read-only");
    REQUIRE(world.buffer.Add(world.Entities(), world.registry, world.relationship, entity, nullptr) ==
            ChangeError::ReadOnlyComponent);
    INFO("A remove of it should say so too");
    REQUIRE(world.buffer.Remove(world.Entities(), world.registry, world.relationship, entity) ==
            ChangeError::ReadOnlyComponent);
}

TEST_CASE("removing a component the engine requires is refused", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();

    INFO("Removing Transform should say the engine requires it");
    REQUIRE(world.buffer.Remove(world.Entities(), world.registry, world.transform, entity) ==
            ChangeError::RequiredComponent);
}

TEST_CASE("changing an entity that is not alive is refused", "[change-buffer]")
{
    World world;
    const auto stale = world.NewEntity();
    world.Entities().DestroyEntity(stale);

    INFO("An add on a dead entity should say it is not alive");
    REQUIRE(world.buffer.Add(world.Entities(), world.registry, world.velocity, stale, nullptr) ==
            ChangeError::EntityNotAlive);
    INFO("A remove on it should say so");
    REQUIRE(world.buffer.Remove(world.Entities(), world.registry, world.velocity, stale) ==
            ChangeError::EntityNotAlive);
    INFO("A destroy of it should say so");
    REQUIRE(world.buffer.Destroy(world.Entities(), stale) == ChangeError::EntityNotAlive);
}

TEST_CASE("a refused change is not recorded and changes nothing", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    (void)world.buffer.Remove(world.Entities(), world.registry, world.transform, entity);
    (void)world.buffer.Add(world.Entities(), world.registry, world.relationship, entity, nullptr);

    INFO("Nothing should be recorded");
    REQUIRE(world.buffer.Empty());
    INFO("The entity should keep its Transform");
    REQUIRE(world.Entities().GetEntity(entity).HasComponent<Transform>());
}

TEST_CASE("sending an event that is unknown or has no value is refused", "[change-buffer]")
{
    World world;
    const Ping value{1};

    INFO("An unknown event should be refused");
    REQUIRE(world.buffer.SendEvent(world.headless.Get(), world.registry, 999, &value) == ChangeError::UnknownEvent);
    INFO("A known event with no value should be refused");
    REQUIRE(world.buffer.SendEvent(world.headless.Get(), world.registry, world.ping, nullptr) ==
            ChangeError::UnknownEvent);
    INFO("Nothing should be recorded");
    REQUIRE(world.buffer.Empty());
}

TEST_CASE("an event is put in its queue at playback and not before", "[change-buffer]")
{
    World world;
    const Ping value{7};
    INFO("The send was refused when it was recorded");
    REQUIRE(world.buffer.SendEvent(world.headless.Get(), world.registry, world.ping, &value) == ChangeError::None);

    INFO("A system should not read its own send before playback");
    REQUIRE(Testing::HeldEvents<Ping>(world.headless.Get()).empty());

    const auto result = world.Apply();

    INFO("The send should be applied");
    REQUIRE(result.applied == 1);
    INFO("The event should now be in the queue with its value");
    const std::vector<Ping> events = Testing::HeldEvents<Ping>(world.headless.Get());
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().Value == 7);
}

TEST_CASE("an event whose queue has gone is skipped with a warning", "[change-buffer]")
{
    World world;
    // An event only a script defines has a queue the engine made and can drop again (as a reload does).
    REQUIRE(world.headless.Get().CreateEventQueue(ScriptedEventName, 4, 4) != nullptr);
    EventRegistration registration;
    registration.name = ScriptedEventName;
    registration.typeName = ScriptedEventName;
    registration.size = 4;
    registration.alignment = 4;
    registration.scriptDefined = true;
    const auto scripted = world.registry.Events().Add(std::move(registration));
    const Ping value{1};
    INFO("The send was refused when it was recorded");
    REQUIRE(world.buffer.SendEvent(world.headless.Get(), world.registry, scripted, &value) == ChangeError::None);
    REQUIRE(world.headless.Get().RemoveEventQueue(ScriptedEventName));

    const auto result = world.Apply();

    INFO("The send should be skipped, not applied");
    REQUIRE((result.applied == 0 && result.skipped == 1));
    INFO("The skipped send should have been reported");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn, "skipped sending an event"));
}

namespace
{
    // A scene resource and a read-only app resource, as an extension would register them.
    struct Weather
    {
        float Wind = 0.5F;
    };

    struct Clock
    {
        double Seconds = 0.0;
    };
} // namespace

template <>
struct Iris::StructMapping<Weather>
{
    static constexpr std::string_view typeName = "Rules.Weather";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Weather, Wind));
};

template <>
struct Iris::StructMapping<Clock>
{
    static constexpr std::string_view typeName = "Rules.Clock";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Clock, Seconds));
};

TEST_CASE("inserting and removing a resource play back in order, an insert with its value", "[change-buffer]")
{
    World world;
    const auto weather = world.registry.Resources().Add(MakeSceneResource<Weather>("Rules.Weather"));
    Weather value{3.0F};
    REQUIRE(world.buffer.InsertResource(world.registry, weather, &value) == ChangeError::None);
    value.Wind = 9.0F;
    REQUIRE(world.buffer.RemoveResource(world.registry, weather) == ChangeError::None);
    REQUIRE(world.buffer.InsertResource(world.registry, weather, &value) == ChangeError::None);
    REQUIRE(world.buffer.InsertResource(world.registry, weather, nullptr) == ChangeError::None);
    Scene& scene = world.headless.Get().GetActiveScene();
    INFO("A recorded insert reached the scene before playback");
    REQUIRE(scene.GetResource<Weather>() == nullptr);

    const auto result = world.Apply();

    INFO("Every change should be applied");
    REQUIRE(result.applied == 4);
    INFO("The last insert, with no value, should leave the C++ default");
    REQUIRE(scene.GetResource<Weather>() != nullptr);
    REQUIRE(scene.GetResource<Weather>()->Wind == 0.5F);
}

TEST_CASE("an insert copies its value when it is recorded", "[change-buffer]")
{
    World world;
    const auto weather = world.registry.Resources().Add(MakeSceneResource<Weather>("Rules.Weather"));
    Weather value{3.0F};
    REQUIRE(world.buffer.InsertResource(world.registry, weather, &value) == ChangeError::None);
    value.Wind = 9.0F;

    (void)world.Apply();

    INFO("The insert should carry the value as it was when recorded");
    REQUIRE(world.headless.Get().GetActiveScene().GetResource<Weather>()->Wind == 3.0F);
}

TEST_CASE("inserting or removing an unknown or read-only resource is refused", "[change-buffer]")
{
    World world;
    const auto clock = world.registry.Resources().Add(MakeReadOnlyAppResource<Clock>("Rules.Clock"));
    constexpr std::int32_t unknown = 42;

    INFO("An unknown resource should be refused as unknown");
    REQUIRE(world.buffer.InsertResource(world.registry, unknown, nullptr) == ChangeError::UnknownResource);
    REQUIRE(world.buffer.RemoveResource(world.registry, unknown) == ChangeError::UnknownResource);
    INFO("A read-only resource should be refused as read-only");
    REQUIRE(world.buffer.InsertResource(world.registry, clock, nullptr) == ChangeError::ReadOnlyResource);
    REQUIRE(world.buffer.RemoveResource(world.registry, clock) == ChangeError::ReadOnlyResource);
    INFO("A refused change should not be recorded");
    REQUIRE(world.buffer.Empty());
}

TEST_CASE("a change a native function recorded plays back in order with the rest", "[change-buffer]")
{
    World world;
    const auto entity = world.NewEntity();
    std::string order;
    world.Add(entity, nullptr);
    world.buffer.RecordNative([&](App&) {
        order += world.VelocityOf(entity) != nullptr ? "after-add;" : "before-add;";
        return true;
    });
    world.Remove(entity);
    world.buffer.RecordNative([&](App&) {
        order += world.VelocityOf(entity) == nullptr ? "after-remove;" : "before-remove;";
        return false;
    });

    const auto result = world.Apply();

    INFO("The native changes should run between the others, in the order recorded: " + order);
    REQUIRE(order == "after-add;after-remove;");
    INFO("A native change that could not apply should count as skipped");
    REQUIRE((result.applied == 3 && result.skipped == 1));
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn, "skipped a change a native function recorded"));
}

TEST_CASE("discarding drops the changes native functions recorded", "[change-buffer]")
{
    World world;
    bool ran = false;
    world.buffer.RecordNative([&](App&) {
        ran = true;
        return true;
    });
    world.buffer.Discard(world.Entities());

    const auto result = world.Apply();

    INFO("A discarded native change ran");
    REQUIRE((!ran && result.applied == 0));
}
