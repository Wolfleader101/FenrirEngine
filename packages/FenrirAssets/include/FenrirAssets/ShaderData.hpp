#pragma once

#include <cstddef>
#include <vector>

#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir
{
    /**
     * @brief A shader container as plain data (ADR-0018, ADR-0021): the bytes FenrirShaderCompiler wrote, and the
     * parameter table read from them. FenrirAssets reads nothing else of the container; FenrirRenderer parses the rest.
     *
     * The store reads the table when the data is added or replaced, and refuses bytes that are not a shader container
     * of this build's format version.
     */
    struct ShaderData
    {
        /** @brief The container's bytes, as written. */
        std::vector<std::byte> bytes;
        /**
         * @brief The shader's material parameters, each with its type and parameter default, across its variants
         * (CollectShaderParameters). Filled by the store from the bytes; anything set here before is replaced.
         */
        std::vector<ShaderParameter> parameters;
    };
} // namespace Fenrir
