#pragma once

// The adapters at FenrirScripting's four seams over Iris, in one place: what a generation is to Iris (a loaded
// component), how its system host is found, how a native function is registered, and how the script assembly reports
// what it defines. Each seam's other adapter is a fake under Tests/Rules/, so every module behind a seam
// (GenerationLifecycle, ScriptSystems, ExtensionSet, ScriptData) is tested with no .NET; these four are tested through
// the fixture assembly.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "NativeRegistrar.hpp"
#include "ScriptAssemblyLoader.hpp"
#include "ScriptData.hpp"
#include "SystemHost.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief The lifecycle's loader over the Iris host: a loaded generation is an Iris component, and this is the one
     * place that knows it. Scripting's hooks reach the component through Of.
     */
    class IrisScriptAssemblyLoader final : public ScriptAssemblyLoader
    {
      public:
        explicit IrisScriptAssemblyLoader(Iris::Host& host) : m_host(host)
        {
        }

        [[nodiscard]] Iris::CallResult<std::unique_ptr<LoadedScriptAssembly>> Load(
            const std::filesystem::path& path) override;

        /** @brief The Iris component behind a generation this loader loaded. */
        [[nodiscard]] static Iris::Component& Of(LoadedScriptAssembly& loaded) noexcept;

      private:
        struct Loaded final : LoadedScriptAssembly
        {
            explicit Loaded(Iris::Component&& owner) : component(std::move(owner))
            {
            }

            [[nodiscard]] Iris::CallResult<void> Release() override;
            [[nodiscard]] Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(
                std::chrono::milliseconds timeout) override;

            Iris::Component component;
        };

        Iris::Host& m_host;
    };

    /**
     * @brief ScriptSystems' loader over the Iris component a generation is: the one place that knows the generated host
     * is a managed type found by name, with six methods bound by name (GeneratedNames.hpp), and that the host reports
     * its stage systems through the engine context's slot, which a native fills. The method lookup, and so the
     * missing-method diagnostic, is this adapter's and no fake's.
     */
    class IrisSystemHostLoader final : public SystemHostLoader
    {
      public:
        /** @brief `context` is where the native puts the stage systems the host reports; borrowed. */
        explicit IrisSystemHostLoader(EngineContext& context) : m_context(context)
        {
        }

        [[nodiscard]] Iris::CallResult<std::unique_ptr<SystemHost>> Find(LoadedScriptAssembly& generation) override;

      private:
        // The managed host and its bound methods. Released before the component, which cannot be released while an
        // object it made is alive: ScriptSystems drops it at Stop, before the generation is unloaded.
        struct Host final : SystemHost
        {
            Host(EngineContext& context, Iris::Object&& instance, Iris::InstanceMethod<std::int32_t()> mask,
                 Iris::InstanceMethod<void()> stages, Iris::InstanceMethod<void()> create,
                 Iris::InstanceMethod<void(std::int32_t)> run, Iris::InstanceMethod<void(std::int32_t)> runStage,
                 Iris::InstanceMethod<void()> destroy)
                : context(context), object(std::move(instance)), mask(mask), stages(stages), create(create), run(run),
                  runStage(runStage), destroy(destroy)
            {
            }

            [[nodiscard]] Iris::CallResult<std::int32_t> PhaseMask() override;
            [[nodiscard]] Iris::CallResult<std::vector<StageSystemReport>> StageSystems() override;
            [[nodiscard]] Iris::CallResult<void> OnCreate() override;
            [[nodiscard]] Iris::CallResult<void> Run(std::int32_t phase) override;
            [[nodiscard]] Iris::CallResult<void> RunStage(std::int32_t group) override;
            [[nodiscard]] Iris::CallResult<void> OnDestroy() override;

            EngineContext& context;
            Iris::Object object;
            Iris::InstanceMethod<std::int32_t()> mask;
            Iris::InstanceMethod<void()> stages;
            Iris::InstanceMethod<void()> create;
            Iris::InstanceMethod<void(std::int32_t)> run;
            Iris::InstanceMethod<void(std::int32_t)> runStage;
            Iris::InstanceMethod<void()> destroy;
        };

        EngineContext& m_context;
    };

    /**
     * @brief The extension set's registrar over Iris: the one line that reaches Iris's registration. NativeFunction's
     * registrar sets the function's not-running value, state and name, then registers it under its binding name.
     */
    class IrisNativeRegistrar final : public NativeRegistrar
    {
      public:
        [[nodiscard]] Iris::CallResult<void> Register(const NativeFunction& function, void* state) override;
    };

    /**
     * @brief The script assembly as a loaded Iris component: it reports what it defines by running the tables the
     * SDK's generator wrote into it (through the engine context's reporting slot, which the natives fill), reads its
     * types for fingerprints, and holds the C# structs the native components are checked against.
     */
    class IrisScriptAssembly final : public ScriptAssembly
    {
      public:
        /** @brief `context` is where the natives put what the assembly reports; `component` is the loaded assembly. */
        IrisScriptAssembly(EngineContext& context, const Iris::Component& component);

        [[nodiscard]] Iris::CallResult<void> Report(ScriptReport& report) override;
        [[nodiscard]] std::string Fingerprint(const ScriptComponentDescription& description) override;
        [[nodiscard]] Iris::CallResult<void> CheckNativeAgainstCSharp(const ComponentRegistry& registry) override;

      private:
        EngineContext& m_context;
        const Iris::Component& m_component;
    };
} // namespace Fenrir::Detail
