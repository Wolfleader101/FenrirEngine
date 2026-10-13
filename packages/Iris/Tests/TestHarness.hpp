#pragma once

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace Iris::Tests
{
    // A Catch2 assertion that carries `message`: the case fails, with the message shown, unless `condition` holds.
    inline void Check(bool condition, const std::string& message)
    {
        INFO(message);
        REQUIRE(condition);
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };

    // Runs each case as a Catch2 section of the test case that calls this, so a failing case is reported by its name
    // and the others still run. Call it from inside a TEST_CASE.
    inline void RunCases(std::initializer_list<TestCase> cases)
    {
        for (const auto& testCase : cases)
        {
            DYNAMIC_SECTION(testCase.name)
            {
                testCase.run();
            }
        }
    }

    // Runs Catch2 with `arguments` (the program name first). The checks take their paths ahead of Catch2's own options,
    // so they build this list themselves.
    inline int RunCatch(const std::vector<std::string>& arguments)
    {
        std::vector<const char*> pointers;
        pointers.reserve(arguments.size());
        for (const auto& argument : arguments)
        {
            pointers.push_back(argument.c_str());
        }
        return Catch::Session().run(static_cast<int>(pointers.size()), pointers.data());
    }
} // namespace Iris::Tests
