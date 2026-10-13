#pragma once

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace Fenrir::Testing
{
    // A Catch2 assertion that carries `message`: the case fails, with the message shown, unless `condition` holds. The
    // helpers the cases share call it from anywhere inside a case, so a check reads the same wherever it is.
    inline void Check(bool condition, const std::string& message)
    {
        INFO(message);
        REQUIRE(condition);
    }
} // namespace Fenrir::Testing
