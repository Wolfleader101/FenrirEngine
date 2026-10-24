#include "StageCompiler.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <utility>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Include/intermediate.h>
#include <glslang/MachineIndependent/localintermediate.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr int GlslVersion = 460;

        bool ReadText(const fs::path& path, std::string& text)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return false;
            }
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        std::string Display(const fs::path& path)
        {
            return path.lexically_normal().generic_string();
        }

        // Resolves #include "x" against the including file's directory, then the include directories in order;
        // #include <x> against the include directories only. Every file found is recorded for the depfile.
        class Includer final : public glslang::TShader::Includer
        {
          public:
            Includer(const std::vector<fs::path>& dirs, std::set<fs::path>& read) : m_dirs(dirs), m_read(read)
            {
            }

            IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t depth) override
            {
                if (IncludeResult* found = Open(fs::path(includerName).parent_path() / headerName))
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
                std::error_code error;
                if (!fs::is_regular_file(path, error))
                {
                    return nullptr;
                }
                auto text = std::make_unique<std::string>();
                if (!ReadText(path, *text))
                {
                    return nullptr;
                }
                const fs::path absolute = fs::absolute(path).lexically_normal();
                m_read.insert(absolute);
                std::string* data = text.release();
                return new IncludeResult(Display(absolute), data->data(), data->size(), data);
            }

            const std::vector<fs::path>& m_dirs;
            std::set<fs::path>& m_read;
        };

        // The engine's rules glslang leaves alone with Vulkan semantics: it gives a resource without `set` set 0, and
        // compiles gl_InstanceIndex, which GL cannot express (its InstanceID leaves out the base instance).
        class RuleChecker final : public glslang::TIntermTraverser
        {
          public:
            RuleChecker(Diagnostics& diagnostics, std::string mainFile)
                : glslang::TIntermTraverser(true, false, true), m_diagnostics(diagnostics),
                  m_mainFile(std::move(mainFile))
            {
            }

            // The linker objects, after every function, list each global the stage declares, used or not, but without
            // a source location. So the resources are checked there, and an error points at the resource's first use,
            // or at the stage's file when it is never used.
            bool visitAggregate(glslang::TVisit visit, glslang::TIntermAggregate* node) override
            {
                if (node->getOp() == glslang::EOpLinkerObjects)
                {
                    m_inDeclarations = visit == glslang::EvPreVisit;
                }
                return true;
            }

            void visitSymbol(glslang::TIntermSymbol* symbol) override
            {
                const glslang::TQualifier& qualifier = symbol->getQualifier();
                if (qualifier.builtIn == glslang::EbvInstanceIndex)
                {
                    Report("gl_InstanceIndex", symbol->getLoc(),
                           "gl_InstanceIndex is refused: OpenGL has no equivalent (its instance ID leaves out the base "
                           "instance). Index draws with gl_BaseInstance instead");
                    return;
                }
                const bool resource =
                    qualifier.storage == glslang::EvqUniform || qualifier.storage == glslang::EvqBuffer;
                if (!resource || qualifier.builtIn != glslang::EbvNone)
                {
                    return;
                }
                const std::string name = ResourceName(*symbol);
                if (!m_inDeclarations)
                {
                    if (symbol->getLoc().line > 0)
                    {
                        m_firstUse.try_emplace(name, symbol->getLoc());
                    }
                    return;
                }
                const auto use = m_firstUse.find(name);
                const glslang::TSourceLoc loc = use != m_firstUse.end() ? use->second : glslang::TSourceLoc{};
                if (qualifier.isPushConstant())
                {
                    Report(name, loc,
                           std::format("'{}' is a push constant block, which OpenGL has no equivalent of; put it in a "
                                       "uniform block with layout(set, binding)",
                                       name));
                }
                else if (!qualifier.hasSet())
                {
                    Report(name, loc,
                           std::format("'{}' has no layout(set = ...): every resource names its binding set", name));
                }
                else if (!qualifier.hasBinding())
                {
                    Report(name, loc,
                           std::format("'{}' has no layout(binding = ...): every resource names its binding", name));
                }
            }

          private:
            static std::string ResourceName(const glslang::TIntermSymbol& symbol)
            {
                const glslang::TType& type = symbol.getType();
                if (type.getBasicType() == glslang::EbtBlock)
                {
                    return std::string(type.getTypeName().c_str());
                }
                return std::string(symbol.getName().c_str());
            }

            // Once per name.
            void Report(const std::string& name, const glslang::TSourceLoc& loc, const std::string& message)
            {
                if (!m_reported.insert(name).second)
                {
                    return;
                }
                if (loc.line <= 0)
                {
                    m_diagnostics.Error(m_mainFile, message);
                    return;
                }
                const char* file = loc.getFilenameStr();
                const std::string_view where = file != nullptr ? std::string_view(file) : std::string_view();
                m_diagnostics.Error(where.empty() ? std::string_view(m_mainFile) : where, loc.line, message);
            }

            Diagnostics& m_diagnostics;
            std::string m_mainFile;
            std::set<std::string> m_reported;
            std::map<std::string, glslang::TSourceLoc> m_firstUse;
            bool m_inDeclarations = false;
        };

        std::string Preamble(const std::vector<std::string>& defines)
        {
            std::string preamble;
            for (const std::string& define : defines)
            {
                preamble += "#define " + define + " 1\n";
            }
            return preamble;
        }

        // One glslang shader set up the same way for preprocessing and compiling.
        std::unique_ptr<glslang::TShader> MakeShader(EShLanguage language, const char* const* source,
                                                     const char* const* name, const std::string& preamble)
        {
            auto shader = std::make_unique<glslang::TShader>(language);
            shader->setStringsWithLengthsAndNames(source, nullptr, name, 1);
            shader->setPreamble(preamble.c_str());
            shader->setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
            shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
            shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
            return shader;
        }
    } // namespace

    GlslangProcess::GlslangProcess()
    {
        glslang::InitializeProcess();
    }

    GlslangProcess::~GlslangProcess()
    {
        glslang::FinalizeProcess();
    }

    std::optional<CompiledStage> CompileStage(ShaderStage stage, const fs::path& path,
                                              const std::vector<std::string>& defines,
                                              const std::vector<fs::path>& includeDirs, std::set<fs::path>& read,
                                              Diagnostics& diagnostics)
    {
        const fs::path absolute = fs::absolute(path).lexically_normal();
        const std::string name = Display(absolute);
        std::string source;
        if (!ReadText(absolute, source))
        {
            diagnostics.Error(name, "cannot be read");
            return std::nullopt;
        }
        read.insert(absolute);

        const EShLanguage language = stage == ShaderStage::Vertex ? EShLangVertex : EShLangFragment;
        const char* sources[] = {source.c_str()};
        const char* names[] = {name.c_str()};
        const std::string preamble = Preamble(defines);
        const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        Includer includer(includeDirs, read);

        CompiledStage compiled;
        compiled.stage = stage;
        {
            std::unique_ptr<glslang::TShader> shader = MakeShader(language, sources, names, preamble);
            if (!shader->preprocess(GetDefaultResources(), GlslVersion, ENoProfile, false, false, messages,
                                    &compiled.preprocessed, includer))
            {
                diagnostics.Log(shader->getInfoLog());
                return std::nullopt;
            }
        }

        std::unique_ptr<glslang::TShader> shader = MakeShader(language, sources, names, preamble);
        if (!shader->parse(GetDefaultResources(), GlslVersion, ENoProfile, false, false, messages, includer))
        {
            diagnostics.Log(shader->getInfoLog());
            return std::nullopt;
        }
        glslang::TProgram program;
        program.addShader(shader.get());
        if (!program.link(messages))
        {
            diagnostics.Log(program.getInfoLog());
            return std::nullopt;
        }

        glslang::TIntermediate* intermediate = program.getIntermediate(language);
        const int errorsBefore = diagnostics.ErrorCount();
        RuleChecker checker(diagnostics, name);
        intermediate->getTreeRoot()->traverse(&checker);
        if (diagnostics.ErrorCount() != errorsBefore)
        {
            return std::nullopt;
        }

        glslang::SpvOptions options;
        options.generateDebugInfo = false;
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*intermediate, compiled.spirv, &options);
        return compiled;
    }
} // namespace Fenrir::ShaderCompiler
