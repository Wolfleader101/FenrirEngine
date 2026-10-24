#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "Device/CommandList.hpp"
#include "Device/Resources.hpp"

namespace RendererTest
{
    /** @brief A binding of a whole uniform buffer. */
    inline Fenrir::Graphics::BindingGroupEntry UniformAt(std::uint32_t binding, Fenrir::Graphics::BufferHandle buffer)
    {
        Fenrir::Graphics::BindingGroupEntry entry;
        entry.binding = binding;
        entry.buffer = buffer;
        return entry;
    }

    /** @brief A graphics pass named `label`, drawing into `target` if given. */
    inline Fenrir::Graphics::PassDesc Pass(std::string label, Fenrir::Graphics::TextureHandle target = {})
    {
        Fenrir::Graphics::PassDesc pass;
        pass.label = std::move(label);
        if (!target.IsEmpty())
        {
            Fenrir::Graphics::ColourAttachment colour;
            colour.texture = target;
            pass.colour.push_back(colour);
        }
        return pass;
    }
} // namespace RendererTest
