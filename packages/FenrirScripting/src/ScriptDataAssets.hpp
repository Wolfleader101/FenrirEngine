#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include "ScriptComponents.hpp"

namespace Fenrir
{
    class App;
}

namespace Fenrir::Detail
{
    /**
     * @brief One step of a C# data asset type's upgraders, run in the script: the tree of a value of the type named
     * `type` written at `fromVersion`, upgraded to the next version, both in the tree encoding; or why it could not be.
     * It is the generated table's `Upgrade(name, fromVersion, bytes)`, bound by name (GeneratedNames.hpp).
     */
    using DataAssetUpgrade = std::function<Iris::CallResult<std::vector<std::byte>>(
        std::string_view type, std::uint32_t fromVersion, std::span<const std::byte> tree)>;

    /**
     * @brief What admission binds by name of a script's data asset table (GeneratedNames.hpp): its upgrade step, and
     * the call that drops the script's codecs from `DataAssetCodecs`, which the table registered as it reported, so
     * nothing there keeps the collectible assembly alive once its generation is refused or stops.
     */
    struct DataAssetTableCalls
    {
        DataAssetUpgrade upgrade;
        std::function<void()> forget;
    };

    /**
     * @brief What admission asks of the asset store about C# data asset types, from the extensions'
     * `ScriptingExtension::assetKinds` and `typeHeld`: which reference targets are asset kinds, and why a type may not
     * be unregistered now. Empty when no extension holds assets: then no target is an asset kind and no type is held.
     */
    struct DataAssetStore
    {
        /** @brief The asset kinds' canonical names (`Fenrir::Mesh`). */
        std::vector<std::string> assetKinds;
        /** @brief The checks of whatever holds records of a type. */
        std::vector<ScriptingExtension::TypeHeld> typeHeld;

        /** @brief Whether `name` is an asset kind's canonical name. */
        [[nodiscard]] bool IsAssetKind(std::string_view name) const;

        /** @brief Why live records in `app` hold the type named `name`, from the first check that says so; empty when
         * none. */
        [[nodiscard]] std::optional<std::string> Held(App& app, std::string_view name) const;
    };

    /**
     * @brief The C# data asset types of one script generation (ADR-0083): a tree-backed `DataAsset` description of each
     * entry its script report holds, a tree-backed `Nested` description of each struct their fields hold, and an `Enum`
     * description of each enum, made from the entries at run time and owned here for as long as they are registered,
     * with each type's upgraders bound to the script's `Upgrade`.
     *
     * Building checks everything and registers nothing; registering is all or nothing; unregistering removes the
     * upgraders, then the types, the topmost first, keeping a type live records hold.
     */
    class ScriptDataAssets
    {
      public:
        /** @brief Why a type may not be unregistered now; empty when it may. */
        using Held = std::function<std::optional<std::string>(std::string_view name)>;

        ScriptDataAssets();
        ~ScriptDataAssets();
        ScriptDataAssets(const ScriptDataAssets&) = delete;
        ScriptDataAssets& operator=(const ScriptDataAssets&) = delete;

        /**
         * @brief Reads every data asset entry in `report` and checks it against `types` and the rest of the report, so
         * the whole report is refused when one entry fails (glossary "Admission"). Registers nothing. It refuses:
         * - an entry that is not one well-formed description entry;
         * - a canonical or former name a registered type holds, or another reported type (a data asset, component,
         *   struct, event, or a struct or enum two entries describe differently);
         * - a base that is not a registered data asset type or a data asset earlier in the report;
         * - a field of a kind the `data-asset` role does not admit (an entity, a pointer, a fixed string or a fixed
         *   array), or a struct or enum no one describes;
         * - a `reference` whose target is neither a registered data asset type, a data asset in the report, nor one of
         *   `store`'s asset kinds;
         * - upgrader versions that are not consecutive, each below the type's version, ending at the one below it.
         *
         * @return success, or a diagnostic naming the type and, where there is one, the field
         */
        [[nodiscard]] Iris::CallResult<void> Build(const ScriptReport& report, const TypeRegistry& types,
                                                   const DataAssetStore& store);

        /** @brief Whether a type `Build` made has upgraders, so the script must give `Upgrade`. */
        [[nodiscard]] bool HasUpgraders() const noexcept;

        /**
         * @brief Registers every description `Build` made with `types`: the enums, then each struct after the structs
         * it holds, then the data asset types, bases first, each with an upgrader from each version it covers, which
         * runs `upgrade` and logs to `app`'s logger when it fails. All or none: a refusal unregisters what this call
         * registered. The script's own component types must already be registered, as a field may hold one.
         *
         * @return success, or a diagnostic naming the type the registry refused
         */
        [[nodiscard]] Iris::CallResult<void> Register(TypeRegistry& types, App& app, DataAssetUpgrade upgrade);

        /**
         * @brief Removes every upgrader `Register` registered, then unregisters its types, the topmost first. A data
         * asset type `held` names a reason for is kept, with a warning to `app`'s logger, and so is what it extends or
         * holds; a later call tries again.
         *
         * @return Whether every description went: when one stays, the descriptions must outlive the registry, so this
         * must not be destroyed.
         */
        [[nodiscard]] bool Unregister(TypeRegistry& types, App& app, const Held& held);

        /** @brief One description and what it points into; defined in the source file. */
        struct Entry;

      private:
        // The enums, then the structs, each after the structs it holds, then the data asset types in report order:
        // the order they register in.
        std::vector<std::unique_ptr<Entry>> m_entries;
    };
} // namespace Fenrir::Detail
