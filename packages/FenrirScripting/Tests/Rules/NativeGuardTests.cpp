// The native guard's rules, with no .NET: through NativeCallHarness, a native's context names the native, and the
// guard's entity-alive rule, requirement check and warning each warn once per place and message under that name.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace
{
    using namespace Fenrir;

    // The extension prefix is the record's constructor argument; the harness's Warnings matches the line after it.
    constexpr const char* Extension = "rules scripting";
    constexpr const char* Native = "Gadgets.Nudge";

    // An entity with no list, as C# holds one it named by id or zeroed: its value, then a null list (the layout the
    // built-in binding's mappings assert). Entity(id, list) would create components, so it is not used here.
    Entity Ownerless(std::uint32_t id)
    {
        static_assert(sizeof(Entity) == 16, "an Entity is its value and its list");
        Entity entity;
        std::memcpy(&entity, &id, sizeof id);
        return entity;
    }
} // namespace

TEST_CASE("NativeGuard::Alive is empty for a dead entity and warns once per call site and message across two calls",
          "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    WarnOnceRecord warnings(Extension);
    const CallContext context = harness.Context(Native);
    const NativeGuard guard(context, warnings);
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const std::uint32_t live = entities.CreateEntity().GetId();
    const std::uint32_t dead = entities.CreateEntity().GetId();
    entities.DestroyEntity(dead);

    {
        const std::optional<Entity> found = guard.Alive(entities.GetEntity(live));
        INFO("A live entity of the active scene was not given back");
        REQUIRE(found.has_value());
        REQUIRE(found->GetId() == live);
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, Native));
    }
    for (int call = 0; call < 2; ++call)
    {
        INFO("A dead entity was given back");
        REQUIRE_FALSE(guard.Alive(entities.HandleOf(dead)).has_value());
    }
    {
        INFO("Two calls from one place did not warn exactly once, under the native's name");
        REQUIRE(harness.Warnings(Native, NativeGuard::NotAlive) == harness.once);
    }
    {
        INFO("The warning was not the whole line: the extension's prefix, then the native's name and the message");
        REQUIRE(harness.Logger().Contains(Testing::LogLevel::Warn,
                                          "[" + std::string(Extension) + "] " +
                                              WarnOnceRecord::Line(Native, NativeGuard::NotAlive)) ==
                (harness.once == 1));
    }
    REQUIRE_FALSE(guard.Alive(entities.HandleOf(dead)).has_value());
    {
        INFO("A call from a second place did not warn again");
        REQUIRE(harness.Warnings(Native, NativeGuard::NotAlive) == 2 * harness.once);
    }
}

TEST_CASE("NativeGuard::Alive looks an entity with no owner up by value and finds one of another scene not alive",
          "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    WarnOnceRecord warnings(Extension);
    const CallContext context = harness.Context(Native);
    const NativeGuard guard(context, warnings);
    EntityList& entities = harness.GetApp().GetActiveScene().GetEntityList();
    const Entity live = entities.CreateEntity();
    Scene other("other");
    EntityList& others = other.GetEntityList();
    // The same value in another scene's list: a link stored by that scene, read while this one is active.
    const Entity foreign = others.HandleOf(live.GetId());

    {
        const std::optional<Entity> found = guard.Alive(Ownerless(live.GetId()));
        INFO("An entity with no owner, named by its id as a test names one, was not looked up in the active scene");
        REQUIRE(found.has_value());
        REQUIRE(*found == live);
        REQUIRE(NativeGuard::ValueOf(Ownerless(live.GetId()), entities) == live.GetId());
    }
    {
        INFO("An entity the active scene's list owns did not resolve to its value");
        REQUIRE(NativeGuard::ValueOf(live, entities) == live.GetId());
    }
    {
        INFO("An entity another scene's list owns did not resolve to the null value");
        REQUIRE(NativeGuard::ValueOf(foreign, entities) == Entity::Null);
    }
    for (int call = 0; call < 2; ++call)
    {
        INFO("An entity of another scene was given back as alive in this one");
        REQUIRE_FALSE(guard.Alive(foreign).has_value());
    }
    {
        INFO("Two calls on an entity of another scene did not warn exactly once, as a dead entity does");
        REQUIRE(harness.Warnings(Native, NativeGuard::NotAlive) == harness.once);
    }
}

TEST_CASE(
    "NativeGuard::Require passes a live pointer and an engaged optional through and warns once for null and empty",
    "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    WarnOnceRecord warnings(Extension);
    const CallContext context = harness.Context(Native);
    const NativeGuard guard(context, warnings);
    constexpr const char* NullMessage = "the active scene has no physics world yet, so it does nothing";
    constexpr const char* EmptyMessage = "the handle is stale, so it does nothing";

    int value = 5;
    {
        INFO("A live pointer did not pass through as it was");
        REQUIRE(guard.Require(&value, NullMessage) == &value);
    }
    {
        const std::optional<int> some = guard.Require(std::optional<int>(3), EmptyMessage);
        INFO("An engaged optional did not pass through as it was");
        REQUIRE(some == 3);
    }
    {
        INFO("A requirement that held warned");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, Native));
    }
    for (int call = 0; call < 2; ++call)
    {
        INFO("A null pointer did not pass through as null");
        REQUIRE(guard.Require(static_cast<int*>(nullptr), NullMessage) == nullptr);
    }
    {
        INFO("Two null requirements from one place did not warn exactly once, with the message given");
        REQUIRE(harness.Warnings(Native, NullMessage) == harness.once);
    }
    for (int call = 0; call < 2; ++call)
    {
        INFO("An empty optional did not pass through empty");
        REQUIRE_FALSE(guard.Require(std::optional<int>(), EmptyMessage).has_value());
    }
    {
        INFO("Two empty requirements from one place did not warn exactly once, with the message given");
        REQUIRE(harness.Warnings(Native, EmptyMessage) == harness.once);
    }
}

TEST_CASE("NativeGuard::Warn logs once for two calls from one place and twice from two places", "[rules][natives]")
{
    Testing::NativeCallHarness harness;
    WarnOnceRecord warnings(Extension);
    const CallContext context = harness.Context(Native);
    const NativeGuard guard(context, warnings);
    constexpr const char* Message = "the entity has no Joint, so it reads zeroed";

    for (int call = 0; call < 2; ++call)
    {
        guard.Warn(Message);
    }
    {
        INFO("Two warnings from one place were not logged exactly once");
        REQUIRE(harness.Warnings(Native, Message) == harness.once);
    }
    guard.Warn(Message);
    {
        INFO("A warning from a second place was not logged again");
        REQUIRE(harness.Warnings(Native, Message) == 2 * harness.once);
    }
    guard.Warn("the entity has no Camera, so it does nothing");
    {
        INFO("A second message from the same native was not logged under its name");
        REQUIRE(harness.Warnings(Native, "the entity has no Camera, so it does nothing") == harness.once);
    }
}
