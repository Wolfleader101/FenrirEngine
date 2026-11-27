#include "FenrirECS/EntityId.hpp"

#include <bit>
#include <random>

namespace Fenrir
{
    namespace
    {
        constexpr std::string_view k_alphabet = "0123456789abcdefghjkmnpqrstvwxyz";

        constexpr int k_bitsPerCharacter = 5;
        constexpr int k_mintedCharacters = 5;
        // Twelve characters is 60 bits, which leaves room for the leading 1 bit that keeps the length.
        constexpr std::size_t k_maxCharacters = 12;

        int DigitOf(char c)
        {
            const std::size_t at = k_alphabet.find(c);
            return at == std::string_view::npos ? -1 : static_cast<int>(at);
        }
    } // namespace

    EntityId MintEntityId()
    {
        // Ids need to be readable and rarely collide within one file, not unpredictable: one generator per thread,
        // seeded once, is plenty.
        thread_local std::mt19937 generator{std::random_device{}()};
        std::uniform_int_distribution<std::uint32_t> bits(0, (1u << (k_bitsPerCharacter * k_mintedCharacters)) - 1);
        const std::uint64_t marker = std::uint64_t{1} << (k_bitsPerCharacter * k_mintedCharacters);
        return EntityId{marker | bits(generator)};
    }

    std::string SpellEntityId(EntityId id)
    {
        if (id.value == 0)
        {
            return {};
        }

        // The leading 1 bit sits just above the characters, so its position gives their count.
        const int characters = (std::bit_width(id.value) - 1) / k_bitsPerCharacter;
        std::string text(static_cast<std::size_t>(characters), '0');
        std::uint64_t rest = id.value;
        for (int i = characters - 1; i >= 0; --i)
        {
            text[static_cast<std::size_t>(i)] = k_alphabet[rest & 0x1f];
            rest >>= k_bitsPerCharacter;
        }
        return text;
    }

    std::optional<EntityId> ParseEntityId(std::string_view text)
    {
        if (text.empty() || text.size() > k_maxCharacters)
        {
            return std::nullopt;
        }

        std::uint64_t value = 1;
        for (const char c : text)
        {
            const int digit = DigitOf(c);
            if (digit < 0)
            {
                return std::nullopt;
            }
            value = (value << k_bitsPerCharacter) | static_cast<std::uint64_t>(digit);
        }
        return EntityId{value};
    }
} // namespace Fenrir
