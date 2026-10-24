#pragma once

#include <string>
#include <vector>

#include "Diagnostics.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief One `#pragma fenrir default(<name>, <value>, ...)` that survived a variant's preprocessing. */
    struct DefaultPragma
    {
        /** @brief The file and line it is written at. */
        std::string file;
        int line = 0;
        /** @brief The member or sampler it names. */
        std::string name;
        /** @brief Its values as written: float literals, or one of `white`, `black` and `normal`. */
        std::vector<std::string> values;
    };

    /**
     * @brief Every `#pragma fenrir` in a stage's preprocessed source, so only those its variant's `#define`s keep.
     * Each is parsed from the line it is written on, found through the `#line` directives the preprocessor leaves;
     * `mainFile` is the stage's own file, which has none before its first `#include`. A malformed one is reported and
     * left out.
     */
    std::vector<DefaultPragma> ReadDefaultPragmas(const std::string& preprocessed, const std::string& mainFile,
                                                  Diagnostics& diagnostics);

    /** @brief A float literal as a pragma value takes it (`1`, `0.5`, `-2.0e-3`, `1.0f`), or false if it is not one. */
    bool ParseFloatValue(const std::string& text, float& value);
} // namespace Fenrir::ShaderCompiler
