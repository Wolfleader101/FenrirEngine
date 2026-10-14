#pragma once

// The native types the rules tests register with scripting: a component scripts may add and remove, and an event they
// may send, and the registry setup every rules test starts from. Shared by the rules test files, so each is defined
// once.
#include "FenrirScripting/ComponentRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include "ComponentRegistry.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

namespace RulesTest
{
    // Adds the engine's own components to `registry`, as the built-in extension gives them to a Scripting.
    inline void AddEngineComponents(Fenrir::Detail::ComponentRegistry& registry)
    {
        for (auto& registration : Fenrir::Detail::EngineComponents())
        {
            REQUIRE(registry.Add(std::move(registration)) >= 0);
        }
    }

    // A native component scripts may add and remove, and a native event they may send.
    struct Velocity
    {
        float X = 0;
        float Y = 0;
        float Z = 0;
    };

    struct Ping
    {
        std::int32_t Value = 0;
    };
} // namespace RulesTest

template <>
struct Iris::StructMapping<RulesTest::Velocity>
{
    static constexpr std::string_view typeName = "Rules.Velocity";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD(RulesTest::Velocity, X), IRIS_FIELD(RulesTest::Velocity, Y), IRIS_FIELD(RulesTest::Velocity, Z));
};

template <>
struct Iris::StructMapping<RulesTest::Ping>
{
    static constexpr std::string_view typeName = "Rules.Ping";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(RulesTest::Ping, Value));
};
