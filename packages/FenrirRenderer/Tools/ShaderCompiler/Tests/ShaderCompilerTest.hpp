#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "FenrirAssets/ShaderContainer.hpp"

namespace ShaderCompilerTest
{
    // A fixture container embedded by fenrir_add_shaders, read back. Fails the test if it does not read.
    inline Fenrir::ShaderContainer Read(std::span<const std::uint8_t> embedded)
    {
        const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(std::as_bytes(embedded));
        if (!read.Ok())
        {
            throw std::runtime_error("the fixture container does not read: " + read.diagnostic);
        }
        return *read.container;
    }

    // What a stage's SPIR-V says about one named variable: its set and binding decorations, if it has them.
    struct Decorations
    {
        std::optional<std::uint32_t> set;
        std::optional<std::uint32_t> binding;
    };

    // Reads SPIR-V the way a driver would, independently of the tool: OpName gives each id its name, OpDecorate its
    // set, binding and built-in.
    struct SpirvDecorations
    {
        std::map<std::string, Decorations> byName;
        std::vector<std::uint32_t> builtIns;
    };

    inline SpirvDecorations Decorate(const std::vector<std::uint32_t>& words)
    {
        constexpr std::uint32_t OpName = 5;
        constexpr std::uint32_t OpDecorate = 71;
        constexpr std::uint32_t BuiltIn = 11;
        constexpr std::uint32_t Binding = 33;
        constexpr std::uint32_t DescriptorSet = 34;

        std::map<std::uint32_t, std::string> names;
        std::map<std::uint32_t, Decorations> byId;
        SpirvDecorations result;
        for (std::size_t at = 5; at < words.size();)
        {
            const std::uint32_t count = words[at] >> 16u;
            const std::uint32_t op = words[at] & 0xffffu;
            if (op == OpName)
            {
                std::string name(reinterpret_cast<const char*>(&words[at + 2]));
                names[words[at + 1]] = name;
            }
            else if (op == OpDecorate)
            {
                const std::uint32_t decoration = words[at + 2];
                if (decoration == DescriptorSet)
                {
                    byId[words[at + 1]].set = words[at + 3];
                }
                else if (decoration == Binding)
                {
                    byId[words[at + 1]].binding = words[at + 3];
                }
                else if (decoration == BuiltIn)
                {
                    result.builtIns.push_back(words[at + 3]);
                }
            }
            at += count == 0 ? 1 : count;
        }
        for (const auto& [id, decorations] : byId)
        {
            result.byName[names[id]] = decorations;
        }
        return result;
    }

    // The float at `offset` of a default block image.
    inline float FloatAt(const std::vector<std::byte>& block, std::size_t offset)
    {
        float value = 0.0f;
        std::memcpy(&value, std::span(block).subspan(offset, sizeof value).data(), sizeof value);
        return value;
    }

    inline const Fenrir::ShaderBlockMember* Member(const Fenrir::ShaderVariant& variant, const std::string& name)
    {
        const auto found = std::ranges::find(variant.materialMembers, name, &Fenrir::ShaderBlockMember::name);
        return found != variant.materialMembers.end() ? &*found : nullptr;
    }

    inline const Fenrir::ShaderParameter* Parameter(const Fenrir::ShaderVariant& variant, const std::string& name)
    {
        const auto found = std::ranges::find(variant.parameters, name, &Fenrir::ShaderParameter::name);
        return found != variant.parameters.end() ? &*found : nullptr;
    }
} // namespace ShaderCompilerTest
