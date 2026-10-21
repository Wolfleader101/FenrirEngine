#include "ShaderContainer.hpp"

namespace Fenrir::Render
{
    namespace
    {
        struct Reader
        {
            std::span<const std::uint8_t> bytes;
            std::size_t at = 0;
            bool ok = true;

            std::uint32_t U32()
            {
                if (at + 4 > bytes.size())
                {
                    ok = false;
                    return 0;
                }
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
                at += 4;
                return v;
            }

            std::string String()
            {
                const std::uint32_t n = U32();
                if (!ok || at + n > bytes.size())
                {
                    ok = false;
                    return {};
                }
                std::string s(reinterpret_cast<const char*>(bytes.data() + at), n);
                at += n;
                return s;
            }
        };
    } // namespace

    std::optional<ShaderContainer> ReadShaderContainer(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < 8 || bytes[0] != 'F' || bytes[1] != 'S' || bytes[2] != 'C' || bytes[3] != '1')
        {
            return std::nullopt;
        }
        Reader r{bytes, 4};
        if (r.U32() != 1)
        {
            return std::nullopt;
        }
        ShaderContainer c;
        const std::uint32_t stages = r.U32();
        for (std::uint32_t s = 0; s < stages && r.ok; ++s)
        {
            const std::uint32_t stage = r.U32();
            const std::uint32_t words = r.U32();
            std::vector<std::uint32_t>& out = stage == 0 ? c.vertex : c.fragment;
            out.resize(words);
            for (std::uint32_t w = 0; w < words && r.ok; ++w)
                out[w] = r.U32();
        }
        const std::uint32_t count = r.U32();
        for (std::uint32_t i = 0; i < count && r.ok; ++i)
        {
            ReflectedBinding b;
            b.set = r.U32();
            b.binding = r.U32();
            b.kind = static_cast<Gpu::BindingKind>(r.U32());
            b.name = r.String();
            b.blockSize = r.U32();
            const std::uint32_t members = r.U32();
            for (std::uint32_t m = 0; m < members && r.ok; ++m)
            {
                ReflectedMember member;
                member.name = r.String();
                member.offset = r.U32();
                member.size = r.U32();
                b.members.push_back(std::move(member));
            }
            c.bindings.push_back(std::move(b));
        }
        if (!r.ok)
        {
            return std::nullopt;
        }
        return c;
    }
} // namespace Fenrir::Render
