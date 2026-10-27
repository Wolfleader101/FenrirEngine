#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief What a script component or event looked like when its data was last in use, so a reload can tell whether
     * the data still means the same thing: the size and alignment, and each field's name, type, kind and offset.
     */
    struct OwnedLayout
    {
        std::string fingerprint;
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    /** @brief Layouts by name. */
    using OwnedLayouts = std::map<std::string, OwnedLayout>;

    /**
     * @brief The script assembly as admission sees it: its script report, and the few questions admission asks of it.
     * Two adapters stand behind it: one over a loaded Iris component (which runs the assembly's generated tables and
     * reads its types), and one for tests that returns literals. Everything else admission does is rules over the
     * answers.
     *
     * The questions are asked in a fixed order: the report, then whether the native components, events and shared
     * structs match their C# structs, then (for what is kept) a layout fingerprint of each definition.
     */
    class ScriptAssembly
    {
      public:
        virtual ~ScriptAssembly() = default;

        /**
         * @brief Fills `report` (empty on entry) with what the assembly defines: its components, then its events, each
         * in the order it reports them. An assembly that defines none of a kind reports success and none of it.
         *
         * @return success, or why the assembly could not report: a diagnostic naming the kind
         */
        [[nodiscard]] virtual Iris::CallResult<void> Report(ScriptReport& report) = 0;

        /**
         * @brief Text that is equal for two builds of a definition exactly when they lay their bytes out the same way:
         * the size and alignment, and each field's name, type, kind and offset where the assembly can tell. A type it
         * does not expose can only be told apart by its size.
         */
        [[nodiscard]] virtual std::string Fingerprint(const ScriptComponentDescription& description) = 0;

        /**
         * @brief Checks every native component, event, shared struct and resource in `registry` against its C# struct
         * in this assembly.
         *
         * @return success, or a diagnostic naming the component, event, shared struct or resource and the first
         * mismatching field
         */
        [[nodiscard]] virtual Iris::CallResult<void> CheckNativeAgainstCSharp(const ComponentRegistry& registry) = 0;
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

    /**
     * @brief The script data: the script components and events whose data is alive in the ECS and the App, which a
     * generation of the script assembly leaves behind for the next one, together with the registry's script-defined
     * entries. It is the one place that registers and forgets them.
     *
     * Admission keeps a generation's script report all or nothing: nothing of it is registered unless all of it is,
     * and a failed admission leaves the registry as Forget would. The data outlives a generation: Forget, the stop
     * path, keeps it for the next generation to admit against; Drop, the shutdown path, removes it.
     */
    class ScriptData
    {
      public:
        /** @brief `registry` is where the script-defined components and events are registered; it must outlive this. */
        explicit ScriptData(ComponentRegistry& registry) : m_registry(registry)
        {
        }
        ScriptData(const ScriptData&) = delete;
        ScriptData& operator=(const ScriptData&) = delete;

        /**
         * @brief Admits a freshly loaded script assembly: its report is discovered and checked, every native component,
         * event, shared struct and resource is checked against its C# struct, its components and events are
         * registered, and only then is what the previous generation left in `app` compared with these definitions
         * (unchanged data stays, anything else goes with a warning) and the queues of new events made.
         *
         * @return success, or why the assembly was refused; a refusal leaves the registry as Forget would, and the
         * data as it was before the stale layouts were dropped, or as that drop left it
         */
        [[nodiscard]] Iris::CallResult<void> Admit(App& app, ScriptAssembly& assembly);

        /** @brief Forgets the registry's script-defined components and events. The data and its layouts stay. */
        void Forget();

        /** @brief Removes the data of every owned script component and event from `app`, and forgets them. */
        void Drop(App& app);

        /** @brief The layouts of the script components whose data is alive. */
        [[nodiscard]] const OwnedLayouts& OwnedComponents() const noexcept
        {
            return m_components;
        }

        /** @brief The layouts of the script events whose queues are alive. */
        [[nodiscard]] const OwnedLayouts& OwnedEvents() const noexcept
        {
            return m_events;
        }

      private:
        ComponentRegistry& m_registry;
        OwnedLayouts m_components;
        OwnedLayouts m_events;
    };
} // namespace Fenrir::Detail
