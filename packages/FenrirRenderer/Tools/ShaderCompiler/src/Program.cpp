#include "Program.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include "FenrirAssets/ShaderGlBindings.hpp"
#include "GlRewrite.hpp"
#include "Pragmas.hpp"
#include "Reflection.hpp"
#include "StageCompiler.hpp"

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        namespace fs = std::filesystem;

        // The material's uniform block (ADR-0024), and the block Fenrir/2D.glsl declares in set 0 of every 2D shader,
        // whose material is in set 1 instead of 2.
        constexpr std::string_view MaterialBlockName = "MaterialParams";
        constexpr std::string_view Target2DBlockName = "Target2D";
        constexpr std::uint32_t MeshMaterialSet = 2;
        constexpr std::uint32_t Material2DSet = 1;

        struct StageBuild
        {
            std::string file;
            CompiledStage compiled;
            StageReflection reflection;
            std::vector<DefaultPragma> pragmas;
        };

        const char* BindingKindName(ShaderBindingKind kind)
        {
            switch (kind)
            {
            case ShaderBindingKind::UniformBuffer:
                return "uniform block";
            case ShaderBindingKind::StorageBuffer:
                return "storage block";
            default:
                return "texture";
            }
        }

        const char* StageName(ShaderStage stage)
        {
            return stage == ShaderStage::Vertex ? "vertex" : "fragment";
        }

        const char* TypeName(ShaderValueType type)
        {
            constexpr std::array<const char*, 16> names = {"float",  "vec2",  "vec3",   "vec4",  "int",   "ivec2",
                                                           "ivec3",  "ivec4", "uint",   "uvec2", "uvec3", "uvec4",
                                                           "matrix", "array", "struct", "value"};
            return names.at(static_cast<std::size_t>(type));
        }

        // 1 to 4 for a float to vec4 member, the material parameter types; 0 for anything else.
        std::size_t Components(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float:
                return 1;
            case ShaderValueType::Vec2:
                return 2;
            case ShaderValueType::Vec3:
                return 3;
            case ShaderValueType::Vec4:
                return 4;
            default:
                return 0;
            }
        }

        std::string Where(const DefaultPragma& pragma)
        {
            return std::format("{}:{}", pragma.file, pragma.line);
        }

        // The parameter's default as `pragma` gives it, or false, with the error reported, when the values do not fit
        // the parameter.
        bool Resolve(const DefaultPragma& pragma, const ShaderParameter& parameter, std::size_t components,
                     const std::string& typeName, ShaderParameter& resolved, Diagnostics& diagnostics)
        {
            resolved = parameter;
            if (parameter.type == ShaderParameterType::Texture)
            {
                const std::string& value = pragma.values.front();
                if (pragma.values.size() != 1 || (value != "white" && value != "black" && value != "normal"))
                {
                    diagnostics.Error(pragma.file, pragma.line,
                                      std::format("'{}' is a texture: its default is one of white, black and normal, "
                                                  "but the pragma gives {} value{} ('{}'...)",
                                                  pragma.name, pragma.values.size(),
                                                  pragma.values.size() == 1 ? "" : "s", value));
                    return false;
                }
                resolved.texture = value == "white"   ? DefaultTexture::White
                                   : value == "black" ? DefaultTexture::Black
                                                      : DefaultTexture::Normal;
                return true;
            }
            if (pragma.values.size() != components)
            {
                diagnostics.Error(pragma.file, pragma.line,
                                  std::format("wrong value count: '{}' is a {} and takes {} value{}, but the pragma "
                                              "gives {}",
                                              pragma.name, typeName, components, components == 1 ? "" : "s",
                                              pragma.values.size()));
                return false;
            }
            for (std::size_t i = 0; i < components; ++i)
            {
                if (!ParseFloatValue(pragma.values[i], resolved.value[i]))
                {
                    diagnostics.Error(pragma.file, pragma.line,
                                      std::format("'{}' is a {} and takes float literals, but the pragma gives '{}'",
                                                  pragma.name, typeName, pragma.values[i]));
                    return false;
                }
            }
            return true;
        }

        // Applies every stage's pragmas to the variant's parameters and checks them against its reflection.
        void ApplyDefaults(const std::vector<StageBuild>& stages, std::uint32_t materialSet, ShaderVariant& variant,
                           Diagnostics& diagnostics)
        {
            // One pragma per name in a stage.
            std::vector<std::map<std::string, const DefaultPragma*>> perStage(stages.size());
            for (std::size_t s = 0; s < stages.size(); ++s)
            {
                for (const DefaultPragma& pragma : stages[s].pragmas)
                {
                    const auto [first, added] = perStage[s].try_emplace(pragma.name, &pragma);
                    if (!added)
                    {
                        diagnostics.Error(pragma.file, pragma.line,
                                          std::format("a second default for '{}' in the {} stage; the first is at {}",
                                                      pragma.name, StageName(stages[s].compiled.stage),
                                                      Where(*first->second)));
                    }
                }
            }

            std::map<std::string, std::pair<const DefaultPragma*, ShaderParameter>> applied;
            for (std::size_t s = 0; s < stages.size(); ++s)
            {
                for (const auto& [name, pragma] : perStage[s])
                {
                    const auto parameter = std::ranges::find(variant.parameters, name, &ShaderParameter::name);
                    if (parameter == variant.parameters.end())
                    {
                        const auto member = std::ranges::find(variant.materialMembers, name, &ShaderBlockMember::name);
                        if (member != variant.materialMembers.end())
                        {
                            diagnostics.Error(pragma->file, pragma->line,
                                              std::format("'{}' is {} {} member of {}: a default goes only on a "
                                                          "float, vec2, vec3 or vec4 member, or a texture",
                                                          name, member->type == ShaderValueType::Array ? "an" : "a",
                                                          TypeName(member->type), MaterialBlockName));
                        }
                        else
                        {
                            diagnostics.Error(pragma->file, pragma->line,
                                              std::format("unknown member: '{}' is neither a member of {} nor a "
                                                          "texture in set {}",
                                                          name, MaterialBlockName, materialSet));
                        }
                        continue;
                    }
                    const auto member = std::ranges::find(variant.materialMembers, name, &ShaderBlockMember::name);
                    const ShaderValueType type =
                        member != variant.materialMembers.end() ? member->type : ShaderValueType::Other;
                    ShaderParameter resolved;
                    if (!Resolve(*pragma, *parameter, Components(type), TypeName(type), resolved, diagnostics))
                    {
                        continue;
                    }
                    const auto [earlier, added] = applied.try_emplace(name, pragma, resolved);
                    if (!added && earlier->second.second != resolved)
                    {
                        diagnostics.Error(pragma->file, pragma->line,
                                          std::format("the stages disagree on the default of '{}': the {} stage's "
                                                      "differs from the one at {}",
                                                      name, StageName(stages[s].compiled.stage),
                                                      Where(*earlier->second.first)));
                    }
                }
            }
            for (ShaderParameter& parameter : variant.parameters)
            {
                if (const auto found = applied.find(parameter.name); found != applied.end())
                {
                    parameter = found->second.second;
                }
            }
        }

        // Merges the stages' bindings and reads the material set from them.
        void MergeReflection(const std::vector<StageBuild>& stages, std::uint32_t materialSet, ShaderVariant& variant,
                             Diagnostics& diagnostics)
        {
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<const ReflectedBinding*, const StageBuild*>>
                merged;
            for (const StageBuild& stage : stages)
            {
                for (const ReflectedBinding& reflected : stage.reflection.bindings)
                {
                    const ShaderBinding& binding = reflected.binding;
                    const auto [earlier, added] =
                        merged.try_emplace({binding.set, binding.binding}, &reflected, &stage);
                    if (added)
                    {
                        continue;
                    }
                    const ReflectedBinding& other = *earlier->second.first;
                    if (other.binding != binding || other.members != reflected.members)
                    {
                        diagnostics.Error(stage.file, std::format("set {} binding {} is '{}' here, but '{}' in {}, "
                                                                  "declared differently",
                                                                  binding.set, binding.binding, binding.name,
                                                                  other.binding.name, earlier->second.second->file));
                    }
                }
            }
            // How many bindings of each kind each set has held so far, in binding order: at most ShaderBindingsPerSet.
            std::map<std::pair<std::uint32_t, ShaderBindingKind>, std::uint32_t> perKind;
            for (const auto& [key, found] : merged)
            {
                const ReflectedBinding& reflected = *found.first;
                const std::uint32_t count = ++perKind[{reflected.binding.set, reflected.binding.kind}];
                if (count > ShaderBindingsPerSet)
                {
                    diagnostics.Error(found.second->file,
                                      std::format("'{}' is {} {} of set {}, but a set holds at most {} bindings of a "
                                                  "kind",
                                                  reflected.binding.name, BindingKindName(reflected.binding.kind),
                                                  count, reflected.binding.set, ShaderBindingsPerSet));
                    continue;
                }
                variant.bindings.push_back(reflected.binding);
                const bool materialBlock = reflected.binding.kind == ShaderBindingKind::UniformBuffer &&
                                           reflected.binding.name == MaterialBlockName;
                if (materialBlock)
                {
                    if (reflected.binding.set != materialSet || reflected.binding.binding != 0)
                    {
                        diagnostics.Error(found.second->file,
                                          std::format("{} is at set {} binding {}, but it belongs at set {} binding 0",
                                                      MaterialBlockName, reflected.binding.set,
                                                      reflected.binding.binding, materialSet));
                        continue;
                    }
                    variant.materialBlockBinding = reflected.binding.binding;
                    variant.materialMembers = reflected.members;
                }
            }

            // The parameters: the block's float to vec4 members by offset, then the set's textures by binding, each
            // zero or white until a pragma says otherwise.
            for (const ShaderBlockMember& member : variant.materialMembers)
            {
                if (const std::size_t components = Components(member.type); components != 0)
                {
                    ShaderParameter parameter;
                    parameter.name = member.name;
                    parameter.type = static_cast<ShaderParameterType>(components - 1);
                    variant.parameters.push_back(std::move(parameter));
                }
            }
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set == materialSet && binding.kind == ShaderBindingKind::Texture)
                {
                    variant.parameters.push_back(
                        {binding.name, ShaderParameterType::Texture, {}, DefaultTexture::White});
                }
            }
        }

        // The MaterialParams block with each number parameter's default at its offset, zero elsewhere.
        std::vector<std::byte> DefaultBlock(const ShaderVariant& variant)
        {
            std::vector<std::byte> block;
            const auto binding = std::ranges::find_if(variant.bindings, [&](const ShaderBinding& b) {
                return b.kind == ShaderBindingKind::UniformBuffer && b.name == MaterialBlockName;
            });
            if (!variant.materialBlockBinding.has_value() || binding == variant.bindings.end())
            {
                return block;
            }
            block.resize(binding->size);
            for (const ShaderParameter& parameter : variant.parameters)
            {
                const auto member =
                    std::ranges::find(variant.materialMembers, parameter.name, &ShaderBlockMember::name);
                if (member == variant.materialMembers.end())
                {
                    continue;
                }
                const std::size_t components = Components(member->type);
                std::memcpy(std::span(block).subspan(member->offset).data(), parameter.value.data(),
                            components * sizeof(float));
            }
            return block;
        }

        std::optional<ShaderVariant> BuildVariant(const ProgramSource& program, std::vector<std::string> defines,
                                                  std::set<fs::path>& read, std::uint32_t& materialSet,
                                                  Diagnostics& diagnostics)
        {
            const int errorsBefore = diagnostics.ErrorCount();
            std::vector<StageBuild> stages;
            for (const auto& [stage, path] :
                 {std::pair(ShaderStage::Vertex, program.vertex), std::pair(ShaderStage::Fragment, program.fragment)})
            {
                std::optional<CompiledStage> compiled =
                    CompileStage(stage, path, defines, program.includeDirs, read, diagnostics);
                if (!compiled)
                {
                    continue;
                }
                StageBuild build;
                build.file = fs::absolute(path).lexically_normal().generic_string();
                build.reflection = ReflectStage(stage, compiled->spirv, build.file, diagnostics);
                build.pragmas = ReadDefaultPragmas(compiled->preprocessed, build.file, diagnostics);
                build.compiled = std::move(*compiled);
                stages.push_back(std::move(build));
            }
            if (diagnostics.ErrorCount() != errorsBefore)
            {
                return std::nullopt;
            }

            const bool is2D = std::ranges::any_of(stages, [](const StageBuild& stage) {
                return std::ranges::any_of(stage.reflection.bindings, [](const ReflectedBinding& b) {
                    return b.binding.set == 0 && b.binding.kind == ShaderBindingKind::UniformBuffer &&
                           b.binding.name == Target2DBlockName;
                });
            });
            materialSet = is2D ? Material2DSet : MeshMaterialSet;

            ShaderVariant variant;
            variant.keys = std::move(defines);
            MergeReflection(stages, materialSet, variant, diagnostics);
            ApplyDefaults(stages, materialSet, variant, diagnostics);
            variant.defaultBlock = DefaultBlock(variant);
            const GlBindingMap glBindings = GlBindingsOf(variant.bindings);
            for (StageBuild& stage : stages)
            {
                if (stage.compiled.stage == ShaderStage::Vertex)
                {
                    variant.vertexInputs = stage.reflection.vertexInputs;
                }
                variant.stages.push_back({stage.compiled.stage, RewriteForGl(stage.compiled.spirv, glBindings),
                                          std::move(stage.compiled.spirv)});
            }
            if (diagnostics.ErrorCount() != errorsBefore)
            {
                return std::nullopt;
            }
            return variant;
        }
    } // namespace

    std::optional<ShaderContainer> BuildProgram(const ProgramSource& program, std::set<fs::path>& read,
                                                Diagnostics& diagnostics)
    {
        GlslangProcess glslang;
        ShaderContainer container;
        container.name = program.name;
        container.keys = program.keys;
        const std::size_t variantCount = std::size_t{1} << program.keys.size();
        bool built = true;
        for (std::size_t index = 0; index < variantCount; ++index)
        {
            std::vector<std::string> defines;
            for (std::size_t k = 0; k < program.keys.size(); ++k)
            {
                if ((index & (std::size_t{1} << k)) != 0)
                {
                    defines.push_back(program.keys[k]);
                }
            }
            std::uint32_t materialSet = MeshMaterialSet;
            std::optional<ShaderVariant> variant =
                BuildVariant(program, std::move(defines), read, materialSet, diagnostics);
            if (!variant)
            {
                built = false;
                continue;
            }
            if (index == 0)
            {
                container.materialSet = materialSet;
            }
            else if (materialSet != container.materialSet)
            {
                diagnostics.Error(program.name, "some variants are 2D shaders and some are not");
                built = false;
            }
            container.variants.push_back(std::move(*variant));
        }
        if (!built)
        {
            return std::nullopt;
        }
        return container;
    }
} // namespace Fenrir::ShaderCompiler
