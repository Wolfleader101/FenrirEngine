#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "ScriptDataAssets.hpp"
#include "ScriptTypes.hpp"

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
     * Two adapters stand behind it: one over a loaded Iris component (in IrisAdapters.hpp, which
     * runs the assembly's generated tables and reads its types), and one for tests that returns literals. Everything
     * else admission does is rules over the answers.
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

        /**
         * @brief The assembly's data asset table, bound by name: its one upgrade step for its C# data asset types, and
         * the call that drops its codecs. Both are good while the assembly stays loaded, which is longer than they are
         * kept, since Forget removes the upgraders and drops the codecs before the generation is released.
         *
         * @return the calls, or why the assembly has none (it defines no data asset types, or its table lacks one)
         */
        [[nodiscard]] virtual Iris::CallResult<DataAssetTableCalls> FindDataAssetTable() = 0;
    };

    /**
     * @brief The script data: the script components and events whose data is alive in the ECS and the App, which a
     * generation of the script assembly leaves behind for the next one, together with the registry's script-defined
     * entries. It is the one place that registers and forgets them.
     *
     * Admission keeps a generation's script report all or nothing: nothing of it is registered unless all of it is,
     * and a failed admission leaves the registry as Forget would. The data outlives a generation: Forget, the stop
     * path, keeps it for the next generation to admit against; Drop, the shutdown path, removes it.
     *
     * Each generation's components are also described to the App's type registry (ADR-0074), from the fields the
     * script reported: a fixed-layout description of each, and a nested one of each struct they hold, owned by the
     * generation's ScriptTypes, with the component's ComponentType attached, so the scene writer and reader treat a C#
     * component as they treat a native one. Forget unregisters them.
     *
     * Its C# data asset types are described there too (ADR-0083), each as a tree-backed `data-asset` description in
     * the generation's ScriptDataAssets, with its upgraders bound to the script's `Upgrade`, all or nothing with the
     * rest of the report. Forget removes the upgraders, then unregisters the types, topmost first; a type the asset
     * store's records still hold is kept, with a warning, until a later Forget finds it free.
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

        /** @brief Unregisters the descriptions still registered; the App whose registry holds them must still live. */
        ~ScriptData();

        /**
         * @brief Admits a freshly loaded script assembly: its report is discovered and checked, every native component,
         * event, shared struct and resource is checked against its C# struct, its components and events are
         * registered, its components and data asset types described to `app`'s type registry (which this keeps, for
         * Forget), and only then is what the previous generation left in `app` compared with these definitions
         * (unchanged data stays, anything else goes with a warning) and the queues of new events made.
         *
         * @return success, or why the assembly was refused; a refusal leaves the registry as Forget would, the data as
         * it was before the stale layouts were dropped, or as that drop left it, and the script's codecs dropped
         */
        [[nodiscard]] Iris::CallResult<void> Admit(App& app, ScriptAssembly& assembly);

        /**
         * @brief Forgets the registry's script-defined components and events, drops the script's data asset codecs, and
         * unregisters their descriptions and upgraders from the type registry, keeping a data asset type the store
         * holds. The data and its layouts stay.
         */
        void Forget();

        /** @brief Removes the data of every owned script component and event from `app`, and forgets them. */
        void Drop(App& app);

        /**
         * @brief What the asset store says about C# data asset types, from the extensions: the asset kinds a reference
         * may target, and what holds a type. Empty until set: no asset kinds, and nothing held.
         */
        void SetDataAssetStore(DataAssetStore store)
        {
            m_store = std::move(store);
        }

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
        // Admission once the report is in: everything but dropping the codecs of a refused report.
        [[nodiscard]] Iris::CallResult<void> AdmitReport(App& app, ScriptAssembly& assembly, const ScriptReport& report,
                                                         const DataAssetTableCalls& table);

        // Unregisters the generation's descriptions, and any a refusal left behind before.
        void ForgetTypes();

        ComponentRegistry& m_registry;
        OwnedLayouts m_components;
        OwnedLayouts m_events;
        // The App and its type registry, from the last admission; null before one.
        App* m_app = nullptr;
        TypeRegistry* m_types = nullptr;
        DataAssetStore m_store;
        // The descriptions of the admitted generation's components and the structs they hold.
        std::unique_ptr<ScriptTypes> m_scriptTypes;
        // The admitted generation's data asset types, the structs they hold and their enums, and the call that drops
        // its codecs when it is forgotten.
        std::unique_ptr<ScriptDataAssets> m_dataAssets;
        std::function<void()> m_forgetCodecs;
        // Descriptions the registry refused to unregister, or the store holds: they must outlive the registry, so they
        // stay until it lets them go.
        std::vector<std::unique_ptr<ScriptTypes>> m_stranded;
        std::vector<std::unique_ptr<ScriptDataAssets>> m_strandedDataAssets;
    };
} // namespace Fenrir::Detail
