#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Fenrir
{
    /**
     * @brief The entity id a scene file gives an entity: minted once, when the entity is first saved, and kept for its
     * life (ADR-0078). It is how a file names an entity, for a parent or an entity field; it is not the run-time
     * Entity, which a load reassigns. A file spells it as a string (SpellEntityId), as the block's `id`.
     */
    struct EntityId
    {
        /**
         * @brief The id's spelling, packed: below a leading 1 bit, five bits per lower-case Crockford base32
         * character, the first character highest. Zero, which no spelling packs to, means no id.
         */
        std::uint64_t value = 0;
    };

    /**
     * @brief A new entity id of five characters (25 random bits, `a7k2q`). It may repeat one already in use: a scene
     * writer re-mints an id that collides within its file.
     */
    [[nodiscard]] EntityId MintEntityId();

    /**
     * @brief The id's spelling in lower-case Crockford base32 without padding: the text ParseEntityId read it from, or
     * five characters for a minted one. Empty for no id. Only an id ParseEntityId or MintEntityId gave has a
     * spelling: any other value spells as nothing useful.
     */
    [[nodiscard]] std::string SpellEntityId(EntityId id);

    /**
     * @brief The id `text` spells: one to twelve lower-case Crockford base32 characters (`0`-`9` and `a`-`z` but `i`,
     * `l`, `o` and `u`). Any length in that range is kept, leading zeros too, so SpellEntityId gives `text` back
     * unchanged. Empty for any other text, upper case included.
     */
    [[nodiscard]] std::optional<EntityId> ParseEntityId(std::string_view text);
} // namespace Fenrir
