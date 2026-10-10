#pragma once

#include <cstdlib>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Fenrir::Scripting::Tests
{
    inline void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct TestCase
    {
        const char* name;
        std::function<void()> run;
    };

    // Runs every case in order, reporting each by name, and keeps going after a failure.
    // Returns the process exit code: failure if any case failed.
    inline int RunCases(std::initializer_list<TestCase> cases)
    {
        int failures = 0;
        for (const auto& testCase : cases)
        {
            try
            {
                testCase.run();
                std::cout << "PASS " << testCase.name << '\n';
            }
            catch (const std::exception& exception)
            {
                ++failures;
                std::cout << "FAIL " << testCase.name << ": " << exception.what() << '\n';
            }
        }
        std::cout << (failures == 0 ? "All cases passed.\n" : std::to_string(failures) + " case(s) failed.\n");
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
} // namespace Fenrir::Scripting::Tests
