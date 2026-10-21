// SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirShaderCompiler, ADR-0018's build-time tool, cut to what the
// spike needs. Compiles one program's GLSL 460 stages with Vulkan semantics (glslang refuses `set != 0` otherwise),
// then rewrites the SPIR-V for OpenGL 4.6: every (set, binding) flattened into one GL binding per resource kind,
// descriptor sets zeroed, VertexIndex retargeted to GL's VertexId. Writes one shader container per program, embedded as
// a byte array in a generated header. No variants, no depfiles.
//
// FenrirShaderCompiler --name Lit --out Lit.hpp -I <dir> --vert lit.vert --frag lit.frag

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv_reflect.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // Shared with the GL backend (src/Backend/OpenGL/GLDevice.cpp): GL binding = set * kBindingsPerSet + binding, per
    // resource kind. Four per set keeps four sets of textures inside GL's minimum of 16 fragment texture units.
    constexpr std::uint32_t kBindingsPerSet = 4;

    enum class Kind : std::uint32_t
    {
        UniformBuffer = 0,
        StorageBuffer = 1,
        Texture = 2,
    };

    bool ReadFile(const fs::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        out = buffer.str();
        return true;
    }

    class Includer final : public glslang::TShader::Includer
    {
      public:
        explicit Includer(std::vector<fs::path> dirs) : m_dirs(std::move(dirs))
        {
        }

        IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t depth) override
        {
            fs::path local = fs::path(includerName).parent_path() / headerName;
            if (IncludeResult* found = Open(local))
            {
                return found;
            }
            return includeSystem(headerName, includerName, depth);
        }

        IncludeResult* includeSystem(const char* headerName, const char*, size_t) override
        {
            for (const fs::path& dir : m_dirs)
            {
                if (IncludeResult* found = Open(dir / headerName))
                {
                    return found;
                }
            }
            return nullptr;
        }

        void releaseInclude(IncludeResult* result) override
        {
            if (result != nullptr)
            {
                delete static_cast<std::string*>(result->userData);
                delete result;
            }
        }

      private:
        IncludeResult* Open(const fs::path& path)
        {
            auto* text = new std::string();
            if (!ReadFile(path, *text))
            {
                delete text;
                return nullptr;
            }
            return new IncludeResult(path.generic_string(), text->data(), text->size(), text);
        }

        std::vector<fs::path> m_dirs;
    };

    struct Member
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    struct Binding
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        Kind kind = Kind::UniformBuffer;
        std::string name;
        std::uint32_t blockSize = 0;
        std::vector<Member> members;
    };

    struct Stage
    {
        std::uint32_t stage = 0; // 0 vertex, 1 fragment
        std::vector<std::uint32_t> glWords;
    };

    bool Compile(EShLanguage language, const fs::path& path, const std::vector<fs::path>& includeDirs,
                 std::vector<std::uint32_t>& spirv)
    {
        std::string source;
        if (!ReadFile(path, source))
        {
            std::fprintf(stderr, "%s: cannot read\n", path.string().c_str());
            return false;
        }
        const std::string name = path.generic_string();
        const char* sources[] = {source.c_str()};
        const char* names[] = {name.c_str()};

        glslang::TShader shader(language);
        shader.setStringsWithLengthsAndNames(sources, nullptr, names, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

        std::vector<fs::path> dirs = includeDirs;
        dirs.push_back(path.parent_path());
        Includer includer(dirs);
        const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 460, ENoProfile, false, false, messages, includer))
        {
            std::fprintf(stderr, "%s\n%s\n", shader.getInfoLog(), shader.getInfoDebugLog());
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            std::fprintf(stderr, "%s\n", program.getInfoLog());
            return false;
        }
        glslang::SpvOptions options;
        options.generateDebugInfo = false;
        glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &options);
        return true;
    }

    bool Reflect(const std::vector<std::uint32_t>& spirv,
                 std::map<std::pair<std::uint32_t, std::uint32_t>, Binding>& out)
    {
        SpvReflectShaderModule module;
        if (spvReflectCreateShaderModule(spirv.size() * 4, spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        {
            return false;
        }
        std::uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
        bool ok = true;
        for (const SpvReflectDescriptorBinding* b : bindings)
        {
            Binding binding;
            binding.set = b->set;
            binding.binding = b->binding;
            switch (b->descriptor_type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                binding.kind = Kind::UniformBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                binding.kind = Kind::StorageBuffer;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                binding.kind = Kind::Texture;
                break;
            default:
                std::fprintf(stderr, "binding %u.%u: unsupported descriptor type %d\n", b->set, b->binding,
                             b->descriptor_type);
                ok = false;
                continue;
            }
            if (b->binding >= kBindingsPerSet)
            {
                std::fprintf(stderr, "binding %u.%u: at most %u bindings per set\n", b->set, b->binding,
                             kBindingsPerSet);
                ok = false;
            }
            binding.name = b->type_description && b->type_description->type_name ? b->type_description->type_name
                                                                                 : (b->name ? b->name : "");
            if (binding.kind != Kind::Texture)
            {
                binding.blockSize = b->block.size;
                for (std::uint32_t m = 0; m < b->block.member_count; ++m)
                {
                    const SpvReflectBlockVariable& member = b->block.members[m];
                    binding.members.push_back({member.name ? member.name : "", member.offset, member.size});
                }
            }
            else
            {
                binding.name = b->name ? b->name : "";
            }
            out[{binding.set, binding.binding}] = binding;
        }
        spvReflectDestroyShaderModule(&module);
        return ok;
    }

    // The GL rewrite: flatten bindings, zero sets, VertexIndex -> VertexId. InstanceIndex has no GL equivalent
    // (InstanceId leaves out the base instance), so it is refused: shaders read gl_BaseInstance instead.
    bool RewriteForGL(std::vector<std::uint32_t>& words,
                      const std::map<std::pair<std::uint32_t, std::uint32_t>, Binding>& bindings)
    {
        constexpr std::uint32_t OpDecorate = 71;
        constexpr std::uint32_t DecorationBuiltIn = 11;
        constexpr std::uint32_t DecorationBinding = 33;
        constexpr std::uint32_t DecorationDescriptorSet = 34;
        constexpr std::uint32_t BuiltInVertexId = 5;
        constexpr std::uint32_t BuiltInVertexIndex = 42;
        constexpr std::uint32_t BuiltInInstanceIndex = 43;

        std::map<std::uint32_t, std::uint32_t> setOf;
        for (std::size_t i = 5; i < words.size();)
        {
            const std::uint32_t count = words[i] >> 16;
            if ((words[i] & 0xFFFF) == OpDecorate && words[i + 2] == DecorationDescriptorSet)
            {
                setOf[words[i + 1]] = words[i + 3];
            }
            i += count;
        }
        for (std::size_t i = 5; i < words.size();)
        {
            const std::uint32_t count = words[i] >> 16;
            if ((words[i] & 0xFFFF) == OpDecorate)
            {
                const std::uint32_t target = words[i + 1];
                const std::uint32_t decoration = words[i + 2];
                if (decoration == DecorationDescriptorSet)
                {
                    words[i + 3] = 0;
                }
                else if (decoration == DecorationBinding)
                {
                    const std::uint32_t set = setOf.count(target) ? setOf[target] : 0;
                    const std::uint32_t binding = words[i + 3];
                    if (bindings.find({set, binding}) == bindings.end())
                    {
                        std::fprintf(stderr, "binding %u.%u not reflected\n", set, binding);
                        return false;
                    }
                    words[i + 3] = set * kBindingsPerSet + binding;
                }
                else if (decoration == DecorationBuiltIn && words[i + 3] == BuiltInVertexIndex)
                {
                    words[i + 3] = BuiltInVertexId;
                }
                else if (decoration == DecorationBuiltIn && words[i + 3] == BuiltInInstanceIndex)
                {
                    std::fprintf(stderr, "gl_InstanceIndex has no OpenGL equivalent; use gl_BaseInstance\n");
                    return false;
                }
            }
            i += count;
        }
        return true;
    }

    void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }

    void PutString(std::vector<std::uint8_t>& out, const std::string& s)
    {
        PutU32(out, static_cast<std::uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
} // namespace

int main(int argc, char** argv)
{
    std::string name;
    fs::path out;
    std::vector<fs::path> includeDirs;
    std::vector<std::pair<EShLanguage, fs::path>> stages;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        const char* value = argv[i + 1];
        if (flag == "--name")
            name = value;
        else if (flag == "--out")
            out = value;
        else if (flag == "-I")
            includeDirs.emplace_back(value);
        else if (flag == "--vert")
            stages.emplace_back(EShLangVertex, value);
        else if (flag == "--frag")
            stages.emplace_back(EShLangFragment, value);
    }
    if (name.empty() || out.empty() || stages.empty())
    {
        std::fprintf(stderr, "usage: FenrirShaderCompiler --name N --out F.hpp [-I dir] --vert F --frag F\n");
        return 2;
    }

    glslang::InitializeProcess();
    std::map<std::pair<std::uint32_t, std::uint32_t>, Binding> bindings;
    std::vector<Stage> compiled;
    bool ok = true;
    for (const auto& [language, path] : stages)
    {
        std::vector<std::uint32_t> spirv;
        if (!Compile(language, path, includeDirs, spirv) || !Reflect(spirv, bindings))
        {
            ok = false;
            continue;
        }
        compiled.push_back({language == EShLangVertex ? 0u : 1u, std::move(spirv)});
    }
    if (ok)
    {
        for (Stage& stage : compiled)
        {
            ok = RewriteForGL(stage.glWords, bindings) && ok;
        }
    }
    glslang::FinalizeProcess();
    if (!ok)
    {
        return 1;
    }

    // Container: "FSC1", version, stages (stage, word count, GL SPIR-V), then reflection of every binding.
    std::vector<std::uint8_t> bytes = {'F', 'S', 'C', '1'};
    PutU32(bytes, 1);
    PutU32(bytes, static_cast<std::uint32_t>(compiled.size()));
    for (const Stage& stage : compiled)
    {
        PutU32(bytes, stage.stage);
        PutU32(bytes, static_cast<std::uint32_t>(stage.glWords.size()));
        for (std::uint32_t w : stage.glWords)
        {
            PutU32(bytes, w);
        }
    }
    PutU32(bytes, static_cast<std::uint32_t>(bindings.size()));
    for (const auto& [key, b] : bindings)
    {
        PutU32(bytes, b.set);
        PutU32(bytes, b.binding);
        PutU32(bytes, static_cast<std::uint32_t>(b.kind));
        PutString(bytes, b.name);
        PutU32(bytes, b.blockSize);
        PutU32(bytes, static_cast<std::uint32_t>(b.members.size()));
        for (const Member& m : b.members)
        {
            PutString(bytes, m.name);
            PutU32(bytes, m.offset);
            PutU32(bytes, m.size);
        }
    }

    fs::create_directories(out.parent_path());
    std::ofstream header(out, std::ios::binary);
    header << "// Generated by FenrirShaderCompiler. Do not edit.\n#pragma once\n#include <cstdint>\n\n";
    header << "namespace Fenrir::Shaders\n{\n    inline constexpr std::uint8_t " << name << "[] = {";
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i % 24 == 0)
            header << "\n        ";
        header << static_cast<unsigned>(bytes[i]) << ",";
    }
    header << "\n    };\n} // namespace Fenrir::Shaders\n";
    return header ? 0 : 1;
}
