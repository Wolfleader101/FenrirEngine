#pragma once

#include <cstddef>
#include <vector>

namespace Fenrir
{
    /**
     * @brief A font as plain CPU-side data (ADR-0021, ADR-0062): the bytes of a TrueType or OpenType file (`.ttf`,
     * `.otf`, or a `.ttc` collection), as they are on disk. FenrirText opens faces from it; the store only checks that
     * the bytes start as a font file does, and never parses them further.
     */
    struct FontData
    {
        /** @brief The file's bytes, starting with its sfnt version tag (`00 01 00 00`, `OTTO`, `true` or `ttcf`). */
        std::vector<std::byte> bytes;
    };
} // namespace Fenrir
