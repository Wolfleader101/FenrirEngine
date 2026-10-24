#include "FenrirAssets/ShaderContainer.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <utility>

// The format, version 1. Every number is a little-endian u32 (a float is its bits); a string is its byte count then
// its UTF-8 bytes; a list is its count then its items.
//
//   magic "FSHC", version, program name, material set, keys (strings)
//   variants, each:
//     keys (strings)
//     stages: stage, GL SPIR-V (words), SPIR-V (words)
//     vertex inputs: location, name, value type
//     bindings: set, binding, kind, name, size
//     material block: present (0 or 1), binding
//     material members: name, value type, offset, size
//     parameters: name, parameter type, four floats, default texture
//     default block image (bytes)
//
// Nothing follows the last variant.

namespace Fenrir
{
    namespace
    {
        class Writer
        {
          public:
            void U32(std::uint32_t value)
            {
                for (std::uint32_t i = 0; i < 4; ++i)
                {
                    m_bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffu));
                }
            }

            void Count(std::size_t count)
            {
                U32(static_cast<std::uint32_t>(count));
            }

            void String(std::string_view text)
            {
                Count(text.size());
                for (const char c : text)
                {
                    m_bytes.push_back(static_cast<std::byte>(c));
                }
            }

            void Strings(const std::vector<std::string>& texts)
            {
                Count(texts.size());
                for (const std::string& text : texts)
                {
                    String(text);
                }
            }

            void Words(const std::vector<std::uint32_t>& words)
            {
                Count(words.size());
                for (const std::uint32_t word : words)
                {
                    U32(word);
                }
            }

            void Raw(std::span<const std::byte> bytes)
            {
                m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
            }

            void Bytes(std::span<const std::byte> bytes)
            {
                Count(bytes.size());
                Raw(bytes);
            }

            std::vector<std::byte> Take()
            {
                return std::move(m_bytes);
            }

          private:
            std::vector<std::byte> m_bytes;
        };

        // Reads the format back. The first failure is kept and every later read returns zero, so the parse can run to
        // its end and report only that first problem.
        class Reader
        {
          public:
            explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes)
            {
            }

            [[nodiscard]] bool Failed() const noexcept
            {
                return !m_error.empty();
            }

            [[nodiscard]] const std::string& Error() const noexcept
            {
                return m_error;
            }

            [[nodiscard]] bool AtEnd() const noexcept
            {
                return m_at == m_bytes.size();
            }

            void Fail(std::string message)
            {
                if (m_error.empty())
                {
                    m_error = std::move(message);
                }
            }

            std::uint32_t U32()
            {
                if (!Has(4))
                {
                    return 0;
                }
                std::uint32_t value = 0;
                for (std::uint32_t i = 0; i < 4; ++i)
                {
                    value |= std::to_integer<std::uint32_t>(m_bytes[m_at + i]) << (8 * i);
                }
                m_at += 4;
                return value;
            }

            float Float()
            {
                return std::bit_cast<float>(U32());
            }

            // A count of items at least `itemSize` bytes each, refused if the bytes left cannot hold them, so a
            // corrupt count never makes a huge allocation.
            std::size_t Count(std::size_t itemSize)
            {
                const std::size_t count = U32();
                if (!Failed() && count * itemSize > m_bytes.size() - m_at)
                {
                    Fail(std::format("it claims {} items at byte {}, more than the {} bytes left", count, m_at - 4,
                                     m_bytes.size() - m_at));
                    return 0;
                }
                return Failed() ? 0 : count;
            }

            std::string String()
            {
                const std::size_t size = Count(1);
                std::string text(size, '\0');
                for (std::size_t i = 0; i < size; ++i)
                {
                    text[i] = static_cast<char>(m_bytes[m_at + i]);
                }
                m_at += size;
                return text;
            }

            std::vector<std::string> Strings()
            {
                std::vector<std::string> texts(Count(4));
                for (std::string& text : texts)
                {
                    text = String();
                }
                return texts;
            }

            std::vector<std::uint32_t> Words()
            {
                std::vector<std::uint32_t> words(Count(4));
                for (std::uint32_t& word : words)
                {
                    word = U32();
                }
                return words;
            }

            std::vector<std::byte> Bytes()
            {
                const std::size_t size = Count(1);
                std::vector<std::byte> bytes(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_at),
                                             m_bytes.begin() + static_cast<std::ptrdiff_t>(m_at + size));
                m_at += size;
                return bytes;
            }

            // An enumerator stored as a u32, refused when it is past `last`.
            template <typename E>
            E Enum(E last, std::string_view what)
            {
                const std::uint32_t value = U32();
                if (value > static_cast<std::uint32_t>(last))
                {
                    Fail(std::format("its {} {} is not one this build knows", what, value));
                    return E{};
                }
                return static_cast<E>(value);
            }

          private:
            bool Has(std::size_t size)
            {
                if (Failed())
                {
                    return false;
                }
                if (m_bytes.size() - m_at < size)
                {
                    Fail(std::format("it ends early, at byte {} of {}", m_bytes.size(), m_at + size));
                    return false;
                }
                return true;
            }

            std::span<const std::byte> m_bytes;
            std::size_t m_at = 0;
            std::string m_error;
        };

        void WriteVariant(Writer& out, const ShaderVariant& variant)
        {
            out.Strings(variant.keys);
            out.Count(variant.stages.size());
            for (const ShaderStageCode& stage : variant.stages)
            {
                out.U32(static_cast<std::uint32_t>(stage.stage));
                out.Words(stage.glSpirv);
                out.Words(stage.spirv);
            }
            out.Count(variant.vertexInputs.size());
            for (const ShaderVertexInput& input : variant.vertexInputs)
            {
                out.U32(input.location);
                out.String(input.name);
                out.U32(static_cast<std::uint32_t>(input.type));
            }
            out.Count(variant.bindings.size());
            for (const ShaderBinding& binding : variant.bindings)
            {
                out.U32(binding.set);
                out.U32(binding.binding);
                out.U32(static_cast<std::uint32_t>(binding.kind));
                out.String(binding.name);
                out.U32(binding.size);
            }
            out.U32(variant.materialBlockBinding.has_value() ? 1 : 0);
            out.U32(variant.materialBlockBinding.value_or(0));
            out.Count(variant.materialMembers.size());
            for (const ShaderBlockMember& member : variant.materialMembers)
            {
                out.String(member.name);
                out.U32(static_cast<std::uint32_t>(member.type));
                out.U32(member.offset);
                out.U32(member.size);
            }
            out.Count(variant.parameters.size());
            for (const ShaderParameter& parameter : variant.parameters)
            {
                out.String(parameter.name);
                out.U32(static_cast<std::uint32_t>(parameter.type));
                for (const float value : parameter.value)
                {
                    out.U32(std::bit_cast<std::uint32_t>(value));
                }
                out.U32(static_cast<std::uint32_t>(parameter.texture));
            }
            out.Bytes(variant.defaultBlock);
        }

        ShaderVariant ReadVariant(Reader& in)
        {
            ShaderVariant variant;
            variant.keys = in.Strings();
            variant.stages.resize(in.Count(12));
            for (ShaderStageCode& stage : variant.stages)
            {
                stage.stage = in.Enum(ShaderStage::Fragment, "stage");
                stage.glSpirv = in.Words();
                stage.spirv = in.Words();
            }
            variant.vertexInputs.resize(in.Count(12));
            for (ShaderVertexInput& input : variant.vertexInputs)
            {
                input.location = in.U32();
                input.name = in.String();
                input.type = in.Enum(ShaderValueType::Other, "value type");
            }
            variant.bindings.resize(in.Count(20));
            for (ShaderBinding& binding : variant.bindings)
            {
                binding.set = in.U32();
                binding.binding = in.U32();
                binding.kind = in.Enum(ShaderBindingKind::Texture, "binding kind");
                binding.name = in.String();
                binding.size = in.U32();
            }
            const bool hasBlock = in.U32() != 0;
            const std::uint32_t blockBinding = in.U32();
            if (hasBlock)
            {
                variant.materialBlockBinding = blockBinding;
            }
            variant.materialMembers.resize(in.Count(16));
            for (ShaderBlockMember& member : variant.materialMembers)
            {
                member.name = in.String();
                member.type = in.Enum(ShaderValueType::Other, "value type");
                member.offset = in.U32();
                member.size = in.U32();
            }
            variant.parameters.resize(in.Count(28));
            for (ShaderParameter& parameter : variant.parameters)
            {
                parameter.name = in.String();
                parameter.type = in.Enum(ShaderParameterType::Texture, "parameter type");
                for (float& value : parameter.value)
                {
                    value = in.Float();
                }
                parameter.texture = in.Enum(DefaultTexture::Normal, "default texture");
            }
            variant.defaultBlock = in.Bytes();
            return variant;
        }
    } // namespace

    const ShaderVariant* ShaderContainer::FindVariant(std::span<const std::string_view> wanted) const
    {
        std::size_t index = 0;
        for (std::size_t k = 0; k < keys.size(); ++k)
        {
            if (std::ranges::find(wanted, std::string_view(keys[k])) != wanted.end())
            {
                index |= std::size_t{1} << k;
            }
        }
        return index < variants.size() ? &variants[index] : nullptr;
    }

    std::vector<std::byte> WriteShaderContainer(const ShaderContainer& container)
    {
        Writer out;
        out.Raw(ShaderContainerMagic);
        out.U32(ShaderContainerVersion);
        out.String(container.name);
        out.U32(container.materialSet);
        out.Strings(container.keys);
        out.Count(container.variants.size());
        for (const ShaderVariant& variant : container.variants)
        {
            WriteVariant(out, variant);
        }
        return out.Take();
    }

    ShaderContainerResult ReadShaderContainer(std::span<const std::byte> bytes)
    {
        ShaderContainerResult result;
        if (bytes.size() < ShaderContainerMagic.size() ||
            !std::equal(ShaderContainerMagic.begin(), ShaderContainerMagic.end(), bytes.begin()))
        {
            result.diagnostic = "not a shader container: it does not start with the magic 'FSHC'";
            return result;
        }
        Reader in(bytes.subspan(ShaderContainerMagic.size()));
        const std::uint32_t version = in.U32();
        if (!in.Failed() && version != ShaderContainerVersion)
        {
            result.diagnostic = std::format("a shader container of format version {}, but this build reads version {}; "
                                            "rebuild it with this engine's FenrirShaderCompiler",
                                            version, ShaderContainerVersion);
            return result;
        }

        ShaderContainer container;
        container.name = in.String();
        container.materialSet = in.U32();
        container.keys = in.Strings();
        container.variants.resize(in.Count(4));
        for (ShaderVariant& variant : container.variants)
        {
            variant = ReadVariant(in);
        }
        if (!in.Failed() && !in.AtEnd())
        {
            in.Fail("it runs on past its last variant");
        }
        if (in.Failed())
        {
            result.diagnostic = "a broken shader container: " + in.Error();
            return result;
        }
        result.container = std::move(container);
        return result;
    }

    std::vector<ShaderParameter> CollectShaderParameters(const ShaderContainer& container)
    {
        std::vector<ShaderParameter> parameters;
        for (const ShaderVariant& variant : container.variants)
        {
            for (const ShaderParameter& parameter : variant.parameters)
            {
                const bool listed = std::ranges::any_of(
                    parameters, [&](const ShaderParameter& seen) { return seen.name == parameter.name; });
                if (!listed)
                {
                    parameters.push_back(parameter);
                }
            }
        }
        return parameters;
    }
} // namespace Fenrir
