#include "IrisAdapters.hpp"

#include "GeneratedNames.hpp"
#include "ScriptComponents.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace Fenrir::Detail
{
    namespace
    {
        using Result = Iris::CallResult<void>;

        Result Succeeded()
        {
            return Result{Iris::CallStatus::Success, {}, false};
        }

        // Every registration of one table whose C# struct can be checked (a script-defined one has no mapping to
        // check against) matches it, or the first that does not, named with the table's noun.
        template <typename Registration>
        Result ValidateTable(const RegistryTable<Registration>& table, const Iris::Component& component)
        {
            for (const auto& registration : table.All())
            {
                if (!registration.validate)
                {
                    continue;
                }
                if (auto validated = registration.validate(component); !validated.Succeeded())
                {
                    std::string noun(table.Noun());
                    noun[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(noun[0])));
                    validated.diagnostic =
                        noun + " '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
                    return validated;
                }
            }
            return Succeeded();
        }

        // Size and alignment, and each field's name, type, kind and offset: two builds of a struct with the same
        // fingerprint lay the same bytes out the same way. A type the assembly does not expose (internal) can only be
        // told apart by its size.
        std::string FingerprintOf(const Iris::Component& component, const ScriptComponentDescription& description)
        {
            std::string text = std::to_string(description.size) + "/" + std::to_string(description.alignment);
            const auto type = component.FindType(description.name);
            if (!type.Succeeded())
            {
                return text + ";opaque";
            }
            const auto fields = type.value->GetFields();
            if (!fields.Succeeded())
            {
                return text + ";opaque";
            }
            for (const auto& field : *fields.value)
            {
                if (!field.isStatic)
                {
                    text += ";" + field.name + ":" + field.type.fullName + ":" +
                            std::to_string(static_cast<int>(field.kind)) + "@" +
                            (field.offset.has_value() ? std::to_string(*field.offset) : std::string("?"));
                }
            }
            return text;
        }
    } // namespace

    Iris::CallResult<std::unique_ptr<LoadedScriptAssembly>> IrisScriptAssemblyLoader::Load(
        const std::filesystem::path& path)
    {
        auto loaded = m_host.LoadComponent(path);
        if (!loaded.Succeeded())
        {
            return {loaded.status, std::nullopt, std::move(loaded.diagnostic), loaded.diagnosticTruncated};
        }
        return {Iris::CallStatus::Success, std::make_unique<Loaded>(std::move(*loaded.value)), {}, false};
    }

    Iris::Component& IrisScriptAssemblyLoader::Of(LoadedScriptAssembly& loaded) noexcept
    {
        return static_cast<Loaded&>(loaded).component;
    }

    Iris::CallResult<void> IrisScriptAssemblyLoader::Loaded::Release()
    {
        return component.Release();
    }

    Iris::CallResult<Iris::ComponentUnloadState> IrisScriptAssemblyLoader::Loaded::WaitForUnload(
        std::chrono::milliseconds timeout)
    {
        return component.WaitForUnload(timeout);
    }

    Iris::CallResult<std::unique_ptr<SystemHost>> IrisSystemHostLoader::Find(LoadedScriptAssembly& generation)
    {
        using FindResult = Iris::CallResult<std::unique_ptr<SystemHost>>;
        const Iris::Component& component = IrisScriptAssemblyLoader::Of(generation);
        const auto type = component.FindType(GeneratedNames::SystemHost);
        if (type.status == Iris::CallStatus::NotFound)
        {
            return FindResult{Iris::CallStatus::NotFound, std::nullopt, type.diagnostic, false};
        }
        if (!type.Succeeded())
        {
            return FindResult{type.status, std::nullopt,
                              "The script's system host could not be read: " + type.diagnostic, false};
        }
        const auto mask = type.value->FindInstanceMethod<std::int32_t()>(GeneratedNames::PhaseMask);
        const auto stages = type.value->FindInstanceMethod<void()>(GeneratedNames::ReportStages);
        const auto create = type.value->FindInstanceMethod<void()>(GeneratedNames::OnCreate);
        const auto run = type.value->FindInstanceMethod<void(std::int32_t)>(GeneratedNames::Run);
        const auto runStage = type.value->FindInstanceMethod<void(std::int32_t)>(GeneratedNames::RunStage);
        const auto destroy = type.value->FindInstanceMethod<void()>(GeneratedNames::OnDestroy);
        for (const auto* found :
             {&mask.status, &stages.status, &create.status, &run.status, &runStage.status, &destroy.status})
        {
            if (*found != Iris::CallStatus::Success)
            {
                return FindResult{*found, std::nullopt,
                                  "The script's system host is missing a method: " + mask.diagnostic +
                                      stages.diagnostic + create.diagnostic + run.diagnostic + runStage.diagnostic +
                                      destroy.diagnostic,
                                  false};
            }
        }
        auto object = type.value->CreateObject();
        if (!object.Succeeded())
        {
            return FindResult{object.status, std::nullopt,
                              "The script's systems could not be created: " + object.diagnostic,
                              object.diagnosticTruncated};
        }
        return FindResult{Iris::CallStatus::Success,
                          std::make_unique<Host>(m_context, std::move(*object.value), *mask.value, *stages.value,
                                                 *create.value, *run.value, *runStage.value, *destroy.value),
                          {},
                          false};
    }

    Iris::CallResult<std::int32_t> IrisSystemHostLoader::Host::PhaseMask()
    {
        return mask.Invoke(object);
    }

    Iris::CallResult<std::vector<StageSystemReport>> IrisSystemHostLoader::Host::StageSystems()
    {
        // The host's ReportStages calls the engine's native once per stage system, which puts it where the slot points.
        std::vector<StageSystemReport> reports;
        context.stageReporting = &reports;
        const auto ran = stages.Invoke(object);
        context.stageReporting = nullptr;
        if (!ran.Succeeded())
        {
            return {ran.status, std::nullopt, ran.diagnostic, ran.diagnosticTruncated};
        }
        return {Iris::CallStatus::Success, std::move(reports), {}, false};
    }

    Iris::CallResult<void> IrisSystemHostLoader::Host::RunStage(std::int32_t group)
    {
        return runStage.Invoke(object, group);
    }

    Iris::CallResult<void> IrisSystemHostLoader::Host::OnCreate()
    {
        return create.Invoke(object);
    }

    Iris::CallResult<void> IrisSystemHostLoader::Host::Run(std::int32_t phase)
    {
        return run.Invoke(object, phase);
    }

    Iris::CallResult<void> IrisSystemHostLoader::Host::OnDestroy()
    {
        return destroy.Invoke(object);
    }

    Iris::CallResult<void> IrisNativeRegistrar::Register(const NativeFunction& function, void* state)
    {
        return function.Register(state);
    }

    IrisScriptAssembly::IrisScriptAssembly(EngineContext& context, const Iris::Component& component)
        : m_context(context), m_component(component)
    {
    }

    Result IrisScriptAssembly::Report(ScriptReport& report)
    {
        struct Table
        {
            const char* noun;
            std::string_view type;
        };
        // The data asset table reports each type's description entry, bases first, as it registers its codecs.
        constexpr Table tables[] = {{"component", GeneratedNames::ComponentTable},
                                    {"event", GeneratedNames::EventTable},
                                    {"data asset", GeneratedNames::DataAssetTable}};
        for (const auto& [noun, type] : tables)
        {
            const auto table = m_component.FindType(type);
            if (table.status == Iris::CallStatus::NotFound)
            {
                continue;
            }
            if (!table.Succeeded())
            {
                return Result{table.status,
                              std::string("The ") + noun + " table could not be read: " + table.diagnostic, false};
            }
            const auto run = table.value->FindMethod<void()>(GeneratedNames::Register);
            if (!run.Succeeded())
            {
                return Result{run.status,
                              std::string("The ") + noun + " table has no Register method: " + run.diagnostic, false};
            }
            // The table's Register calls the engine's natives, which put what it reports where the slot points.
            m_context.reporting = &report;
            const auto ran = run.value->Invoke();
            m_context.reporting = nullptr;
            if (!ran.Succeeded())
            {
                return Result{ran.status,
                              std::string("The script assembly could not report its ") + noun + "s: " + ran.diagnostic,
                              ran.diagnosticTruncated};
            }
        }
        return Succeeded();
    }

    std::string IrisScriptAssembly::Fingerprint(const ScriptComponentDescription& description)
    {
        return FingerprintOf(m_component, description);
    }

    Iris::CallResult<DataAssetTableCalls> IrisScriptAssembly::FindDataAssetTable()
    {
        using Found = Iris::CallResult<DataAssetTableCalls>;
        using Bytes = Iris::Array<std::uint8_t>;
        const auto table = m_component.FindType(GeneratedNames::DataAssetTable);
        if (!table.Succeeded())
        {
            return Found{table.status, std::nullopt, "the data asset table could not be read: " + table.diagnostic,
                         false};
        }
        const auto upgradeMethod =
            table.value->FindMethod<std::optional<Bytes>(std::string_view, std::int32_t, const Bytes&)>(
                GeneratedNames::Upgrade);
        const auto forget = table.value->FindMethod<void()>(GeneratedNames::Forget);
        if (!upgradeMethod.Succeeded() || !forget.Succeeded())
        {
            return Found{upgradeMethod.Succeeded() ? forget.status : upgradeMethod.status, std::nullopt,
                         "the data asset table is missing a method: " + upgradeMethod.diagnostic + forget.diagnostic,
                         false};
        }
        // The method and the component stay good while the generation is loaded, longer than the upgraders bound here.
        const Iris::Component* component = &m_component;
        DataAssetUpgrade upgrade = [component, call = *upgradeMethod.value](
                                       std::string_view type, std::uint32_t fromVersion,
                                       std::span<const std::byte> tree) -> Iris::CallResult<std::vector<std::byte>> {
            using Upgraded = Iris::CallResult<std::vector<std::byte>>;
            // A failed Iris call, as this step's failure.
            const auto failed = [](const auto& call) {
                return Upgraded{call.status, std::nullopt, call.diagnostic, call.diagnosticTruncated};
            };
            auto in = Bytes::Create(*component, tree.size());
            if (!in.Succeeded())
            {
                return failed(in);
            }
            const std::span<const std::uint8_t> raw(reinterpret_cast<const std::uint8_t*>(tree.data()), tree.size());
            if (const auto copied = in.value->CopyIn(0, raw); !copied.Succeeded())
            {
                return failed(copied);
            }
            const auto ran = call.Invoke(type, static_cast<std::int32_t>(fromVersion), *in.value);
            if (!ran.Succeeded())
            {
                return failed(ran);
            }
            if (!ran.value->has_value())
            {
                return Upgraded{Iris::CallStatus::NotFound, std::nullopt,
                                "the script defines no data asset type '" + std::string(type) + "'", false};
            }
            const Bytes& out = **ran.value;
            const auto length = out.Length();
            if (!length.Succeeded())
            {
                return failed(length);
            }
            std::vector<std::byte> bytes(*length.value);
            const std::span<std::uint8_t> room(reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size());
            if (const auto copied = out.CopyOut(0, room); !copied.Succeeded())
            {
                return failed(copied);
            }
            return Upgraded{Iris::CallStatus::Success, std::move(bytes), {}, false};
        };
        // A failure leaves the codecs registered, which keeps the assembly from unloading; the lifecycle reports that.
        std::function<void()> drop = [call = *forget.value] { (void)call.Invoke(); };
        return Found{Iris::CallStatus::Success, DataAssetTableCalls{std::move(upgrade), std::move(drop)}, {}, false};
    }

    Result IrisScriptAssembly::CheckNativeAgainstCSharp(const ComponentRegistry& registry)
    {
        if (auto validated = ValidateTable(registry.Components(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        if (auto validated = ValidateTable(registry.Events(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        if (auto validated = ValidateTable(registry.SharedStructs(), m_component); !validated.Succeeded())
        {
            return validated;
        }
        return ValidateTable(registry.Resources(), m_component);
    }
} // namespace Fenrir::Detail
