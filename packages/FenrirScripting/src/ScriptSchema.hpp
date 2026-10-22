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
     * @brief The script components and events whose data is alive in the ECS and the App: the data a generation of the
     * script assembly leaves behind for the next one.
     */
    struct OwnedScriptData
    {
        OwnedLayouts components;
        OwnedLayouts events;
    };

    /**
     * @brief Checks one native component against its C# struct in a loaded script assembly.
     *
     * @return success, or a diagnostic naming the component and the first mismatching field
     */
    [[nodiscard]] Iris::CallResult<void> ValidateRegistration(const ComponentRegistration& registration,
                                                              const Iris::Component& component);

    /** @brief The two kinds of thing a script assembly defines. */
    enum class ScriptKind
    {
        Component,
        Event,
    };

    /**
     * @brief The script assembly as reconcile sees it: what it says it defines, and the few questions reconcile asks of
     * it. Two adapters stand behind it: one over a loaded Iris component (which runs the assembly's generated tables
     * and reads its types), and one for tests that returns literals. Everything else reconcile does is rules over the
     * answers.
     *
     * The questions are asked in a fixed order: the components, then the events, then whether the native components and
     * events match their C# structs, then (for what is kept) a layout fingerprint of each definition.
     */
    class ScriptAssembly
    {
      public:
        virtual ~ScriptAssembly() = default;

        /**
         * @brief Appends to `found` (empty on entry) what the assembly defines of one kind (name, size and alignment,
         * in the order it reports them). An assembly that defines none of the kind reports success and nothing.
         *
         * @return success, or why the assembly could not report: a diagnostic naming the kind
         */
        [[nodiscard]] virtual Iris::CallResult<void> Report(ScriptKind kind,
                                                            std::vector<ScriptComponentDescription>& found) = 0;

        /**
         * @brief Text that is equal for two builds of a definition exactly when they lay their bytes out the same way:
         * the size and alignment, and each field's name, type, kind and offset where the assembly can tell. A type it
         * does not expose can only be told apart by its size.
         */
        [[nodiscard]] virtual std::string Fingerprint(const ScriptComponentDescription& description) = 0;

        /**
         * @brief Checks every native component, event and resource in `registry` against its C# struct in this
         * assembly.
         *
         * @return success, or a diagnostic naming the component, event or resource and the first mismatching field
         */
        [[nodiscard]] virtual Iris::CallResult<void> CheckNativeAgainstCSharp(const ComponentRegistry& registry) = 0;
    };

    /**
     * @brief Everything that must hold before a freshly loaded script assembly is kept, and then the keeping.
     *
     * It either commits the whole load or changes nothing: what the assembly defines is discovered and checked, every
     * native component, event and resource is checked against its C# struct, and only then is what the previous
     * generation left in the ECS and the App compared with this one's definitions (unchanged data stays, anything else
     * goes with a warning) and the new definitions registered.
     *
     * @param app where script data lives: the entity lists of its scenes and its event queues
     * @param registry the components and events scripts can name; gains the assembly's definitions
     * @param assembly what the freshly loaded script assembly says it defines
     * @param owned what the ECS and the App still hold from the previous generation; updated to match this one
     */
    [[nodiscard]] Iris::CallResult<void> Reconcile(App& app, ComponentRegistry& registry, ScriptAssembly& assembly,
                                                   OwnedScriptData& owned);

    /**
     * @brief Reconcile with the assembly being a loaded Iris component.
     *
     * @param context where the engine's native functions put what the assembly reports; its App is the one reconciled
     * @param component the freshly loaded script assembly
     */
    [[nodiscard]] Iris::CallResult<void> Reconcile(EngineContext& context, ComponentRegistry& registry,
                                                   const Iris::Component& component, OwnedScriptData& owned);

    /**
     * @brief Removes the data of every owned script component and event: they exist only while a script assembly is
     * loaded. `owned` is empty afterwards.
     */
    void DropScriptData(App& app, OwnedScriptData& owned);
} // namespace Fenrir::Detail
