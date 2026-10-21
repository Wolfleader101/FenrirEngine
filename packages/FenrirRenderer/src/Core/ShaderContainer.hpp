#pragma once

// SPIKE: reads FenrirShaderCompiler's container (one program: GL SPIR-V per stage, then reflection of every binding).

#include "Device/Device.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Fenrir::Render
{
    struct ReflectedMember
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    struct ReflectedBinding
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        Gpu::BindingKind kind = Gpu::BindingKind::UniformBuffer;
        std::string name; // block type name, or the texture's name
        std::uint32_t blockSize = 0;
        std::vector<ReflectedMember> members;
    };

    struct ShaderContainer
    {
        std::vector<std::uint32_t> vertex;
        std::vector<std::uint32_t> fragment;
        std::vector<ReflectedBinding> bindings;

        [[nodiscard]] std::vector<const ReflectedBinding*> Set(std::uint32_t set) const
        {
            std::vector<const ReflectedBinding*> out;
            for (const ReflectedBinding& b : bindings)
            {
                if (b.set == set)
                    out.push_back(&b);
            }
            return out;
        }
    };

    std::optional<ShaderContainer> ReadShaderContainer(std::span<const std::uint8_t> bytes);
} // namespace Fenrir::Render
