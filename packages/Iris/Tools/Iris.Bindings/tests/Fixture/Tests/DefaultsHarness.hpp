#pragma once

// What the fixture's generated defaults test runs in: a host with the fixture's C# assembly (the mirrors and the
// generated Defaults class) loaded. CTest names the files in the environment: IRIS_TEST_RUNTIMECONFIG, IRIS_TEST_BRIDGE
// and IRIS_TEST_COMPONENT.

#include "Iris/Iris.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace Fixture
{
    class DefaultsHarness
    {
      public:
        DefaultsHarness()
        {
            const auto initialized =
                host.Initialize({Environment("IRIS_TEST_RUNTIMECONFIG"), Environment("IRIS_TEST_BRIDGE"), {}, {}});
            {
                INFO("The host did not start: " << initialized.diagnostic);
                REQUIRE(initialized.Succeeded());
            }
            auto loaded = host.LoadComponent(Environment("IRIS_TEST_COMPONENT"));
            {
                INFO("The fixture assembly did not load: " << loaded.diagnostic);
                REQUIRE(loaded.Succeeded());
            }
            component.emplace(std::move(*loaded.value));
        }

        [[nodiscard]] const Iris::Component* Component() const
        {
            return component ? &*component : nullptr;
        }

      private:
        static std::filesystem::path Environment(const char* name)
        {
            const char* value = std::getenv(name);
            INFO("Set " << name << " (CTest does): the defaults test needs the files the build made");
            REQUIRE(value != nullptr);
            return std::filesystem::absolute(value);
        }

        Iris::Host host;
        std::optional<Iris::Component> component;
    };
} // namespace Fixture
