#include "GlRewrite.hpp"

#include <algorithm>
#include <map>

#include "FenrirAssets/ShaderGlBindings.hpp"

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        // From the SPIR-V specification.
        constexpr std::size_t HeaderWords = 5;
        constexpr std::uint32_t OpDecorate = 71;
        constexpr std::uint32_t DecorationBuiltIn = 11;
        constexpr std::uint32_t DecorationBinding = 33;
        constexpr std::uint32_t DecorationDescriptorSet = 34;
        constexpr std::uint32_t BuiltInVertexId = 5;
        constexpr std::uint32_t BuiltInVertexIndex = 42;

        // Calls `visit(at)` with the index of each OpDecorate instruction.
        template <typename Visit>
        void ForEachDecoration(const std::vector<std::uint32_t>& words, Visit&& visit)
        {
            for (std::size_t at = HeaderWords; at < words.size();)
            {
                const std::uint32_t count = words[at] >> 16u;
                if ((words[at] & 0xffffu) == OpDecorate && at + 3 < words.size())
                {
                    visit(at);
                }
                at += count == 0 ? 1 : count;
            }
        }
    } // namespace

    GlBindingMap GlBindingsOf(const std::vector<ShaderBinding>& bindings)
    {
        GlBindingMap points;
        for (const ShaderBinding& binding : bindings)
        {
            const auto below = std::ranges::count_if(bindings, [&](const ShaderBinding& other) {
                return other.set == binding.set && other.kind == binding.kind && other.binding < binding.binding;
            });
            points[{binding.set, binding.binding}] = GlBindingOf(binding.set, static_cast<std::uint32_t>(below));
        }
        return points;
    }

    std::vector<std::uint32_t> RewriteForGl(const std::vector<std::uint32_t>& spirv, const GlBindingMap& bindings)
    {
        std::vector<std::uint32_t> words = spirv;

        // A binding is flattened with its variable's set, which may be decorated after it, so read every set first.
        std::map<std::uint32_t, std::uint32_t> setOf;
        ForEachDecoration(words, [&](std::size_t at) {
            if (words[at + 2] == DecorationDescriptorSet)
            {
                setOf[words[at + 1]] = words[at + 3];
            }
        });

        ForEachDecoration(words, [&](std::size_t at) {
            const std::uint32_t target = words[at + 1];
            const std::uint32_t decoration = words[at + 2];
            std::uint32_t& value = words[at + 3];
            if (decoration == DecorationDescriptorSet)
            {
                value = 0;
            }
            else if (decoration == DecorationBinding)
            {
                const auto set = setOf.find(target);
                // Every binding a stage declares is in its variant's reflection.
                const auto point = bindings.find({set != setOf.end() ? set->second : 0, value});
                if (point != bindings.end())
                {
                    value = point->second;
                }
            }
            else if (decoration == DecorationBuiltIn && value == BuiltInVertexIndex)
            {
                value = BuiltInVertexId;
            }
        });
        return words;
    }
} // namespace Fenrir::ShaderCompiler
