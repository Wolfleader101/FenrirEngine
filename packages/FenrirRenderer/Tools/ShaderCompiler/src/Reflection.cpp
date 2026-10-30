#include "Reflection.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <tuple>

#include <spirv_reflect.h>

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        // SPIRV-Reflect hands out its arrays C's way, as a pointer and a count; this is the one place they are trusted.
        template <typename T>
        std::span<const T> CArray(const T* items, std::uint32_t count)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return items != nullptr ? std::span<const T>(items, count) : std::span<const T>();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        ShaderValueType ValueType(const SpvReflectTypeDescription* type)
        {
            if (type == nullptr)
            {
                return ShaderValueType::Other;
            }
            const SpvReflectTypeFlags flags = type->type_flags;
            if ((flags & SPV_REFLECT_TYPE_FLAG_ARRAY) != 0)
            {
                return ShaderValueType::Array;
            }
            if ((flags & SPV_REFLECT_TYPE_FLAG_STRUCT) != 0)
            {
                return ShaderValueType::Struct;
            }
            if ((flags & SPV_REFLECT_TYPE_FLAG_MATRIX) != 0)
            {
                return ShaderValueType::Matrix;
            }
            const std::uint32_t components =
                (flags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0 ? type->traits.numeric.vector.component_count : 1;
            if (components < 1 || components > 4)
            {
                return ShaderValueType::Other;
            }
            const auto offset = static_cast<int>(components - 1);
            if ((flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0 && type->traits.numeric.scalar.width == 32)
            {
                return static_cast<ShaderValueType>(static_cast<int>(ShaderValueType::Float) + offset);
            }
            if ((flags & SPV_REFLECT_TYPE_FLAG_INT) != 0 && type->traits.numeric.scalar.width == 32)
            {
                const ShaderValueType first =
                    type->traits.numeric.scalar.signedness != 0 ? ShaderValueType::Int : ShaderValueType::UInt;
                return static_cast<ShaderValueType>(static_cast<int>(first) + offset);
            }
            return ShaderValueType::Other;
        }

        const char* KindName(SpvReflectDescriptorType type)
        {
            switch (type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                return "a separate sampler";
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                return "a separate texture";
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                return "a storage image";
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                return "a texel buffer";
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                return "an input attachment";
            default:
                return "a resource of a kind the engine does not bind";
            }
        }
    } // namespace

    StageReflection ReflectStage(ShaderStage stage, const std::vector<std::uint32_t>& spirv, const std::string& file,
                                 Diagnostics& diagnostics)
    {
        StageReflection reflection;
        SpvReflectShaderModule module{};
        if (spvReflectCreateShaderModule(spirv.size() * sizeof(std::uint32_t), spirv.data(), &module) !=
            SPV_REFLECT_RESULT_SUCCESS)
        {
            diagnostics.Error(file, "SPIRV-Reflect cannot read the SPIR-V glslang made of it");
            return reflection;
        }

        std::uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
        for (const SpvReflectDescriptorBinding* found : bindings)
        {
            ReflectedBinding reflected;
            ShaderBinding& binding = reflected.binding;
            binding.set = found->set;
            binding.binding = found->binding;
            const bool block = found->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                               found->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.name = block && found->type_description != nullptr && found->type_description->type_name != nullptr
                               ? found->type_description->type_name
                               : (found->name != nullptr ? found->name : "");
            switch (found->descriptor_type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                binding.kind = ShaderBindingKind::UniformBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                binding.kind = ShaderBindingKind::StorageBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                binding.kind = ShaderBindingKind::Texture;
                break;
            default:
                diagnostics.Error(file, std::format("'{}' (set {}, binding {}) is {}: shaders bind uniform blocks, "
                                                    "storage blocks and combined samplers (sampler2D...) only",
                                                    binding.name, binding.set, binding.binding,
                                                    KindName(found->descriptor_type)));
                continue;
            }
            if (found->count > 1)
            {
                diagnostics.Error(file, std::format("'{}' (set {}, binding {}) is an array of resources, which the "
                                                    "engine does not bind",
                                                    binding.name, binding.set, binding.binding));
                continue;
            }
            if (block)
            {
                binding.size = found->block.padded_size;
                // A storage block ending in a runtime array (the draw records, the skinning palette) is as long as its
                // contents; its size is that of one record, the array's offset plus its stride, so the renderer can
                // check the record layout against its own (ADR-0024, ADR-0035).
                // SPIRV-Reflect keeps a runtime array's traits on its type, not on the member.
                if (found->block.member_count > 0)
                {
                    const SpvReflectBlockVariable& last = found->block.members[found->block.member_count - 1];
                    const SpvReflectArrayTraits* array =
                        last.type_description != nullptr ? &last.type_description->traits.array : nullptr;
                    if (array != nullptr && array->dims_count == 1 && array->dims[0] == SPV_REFLECT_ARRAY_DIM_RUNTIME &&
                        array->stride != 0)
                    {
                        binding.size = last.offset + array->stride;
                    }
                }
                for (const SpvReflectBlockVariable& member : CArray(found->block.members, found->block.member_count))
                {
                    reflected.members.push_back({member.name != nullptr ? member.name : "",
                                                 ValueType(member.type_description), member.offset, member.size});
                }
                std::ranges::sort(reflected.members, {}, &ShaderBlockMember::offset);
            }
            reflection.bindings.push_back(std::move(reflected));
        }
        std::ranges::sort(reflection.bindings, {}, [](const ReflectedBinding& b) {
            return std::tuple(b.binding.set, b.binding.binding, static_cast<int>(b.binding.kind));
        });

        if (stage == ShaderStage::Vertex)
        {
            count = 0;
            spvReflectEnumerateInputVariables(&module, &count, nullptr);
            std::vector<SpvReflectInterfaceVariable*> inputs(count);
            spvReflectEnumerateInputVariables(&module, &count, inputs.data());
            for (const SpvReflectInterfaceVariable* input : inputs)
            {
                if (input->built_in != -1 || (input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0)
                {
                    continue;
                }
                reflection.vertexInputs.push_back(
                    {input->location, input->name != nullptr ? input->name : "", ValueType(input->type_description)});
            }
            std::ranges::sort(reflection.vertexInputs, {}, &ShaderVertexInput::location);
        }

        spvReflectDestroyShaderModule(&module);
        return reflection;
    }
} // namespace Fenrir::ShaderCompiler
