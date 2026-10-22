#pragma once

#include <cstddef>
#include <vector>

namespace Fenrir
{
    /**
     * @brief A shader container as plain data (ADR-0018, ADR-0021): the bytes FenrirShaderCompiler wrote, opaque to
     * FenrirAssets. FenrirRenderer parses them.
     */
    struct ShaderData
    {
        /** @brief The container's bytes, as written. */
        std::vector<std::byte> bytes;
    };
} // namespace Fenrir
