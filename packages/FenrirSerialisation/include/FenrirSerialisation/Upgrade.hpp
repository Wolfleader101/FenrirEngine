#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <vector>

namespace Fenrir
{
    class ILogger;
}

namespace Fenrir::Serialisation
{
    /**
     * @brief Rewrites a value's tree, in place, from its type's version `fromVersion` to the next (glossary
     * "Upgrader", ADR-0077 §6): one step, never more, and never down.
     */
    using Upgrader = std::function<void(std::uint32_t fromVersion, Value& tree)>;

    /**
     * @brief The attachment tag a type's upgraders hang on the type registry by: its value is the type's upgraders by
     * the version each steps from, so they iterate in that order. FenrirTypes names no upgrader and knows no tree.
     */
    struct UpgradersTag
    {
        /** @brief The type's upgraders, keyed by the version each steps from. */
        using Value = std::map<std::uint32_t, Upgrader>;
    };

    /**
     * @brief Adds `upgrader` to the upgraders of the type registered under `typeName`, its canonical or a former name,
     * as the step from `fromVersion` to the next.
     *
     * Refused with one error through `logger`, adding nothing, when the registry has no type under `typeName`, when
     * `fromVersion` is 0 or at or above the type's version (a step that could never run), when the type already has
     * an upgrader from `fromVersion`, or when `upgrader` is empty. The C# side registers through this
     * door too.
     *
     * @return Whether it was added.
     */
    bool RegisterUpgrader(TypeRegistry& registry, std::string_view typeName, std::uint32_t fromVersion,
                          Upgrader upgrader, ILogger& logger);

    /**
     * @brief Removes every upgrader of the type registered under `typeName`, its canonical or a former name, so its
     * upgraders can be registered again, as a C# type's are when a hot reload's new generation moves its version.
     *
     * `TypeRegistry::Unregister` removes them with the type, so this is for a type that stays registered.
     *
     * @return Whether the type had upgraders to remove: false when it had none or no type is registered under
     * `typeName`, which is not an error.
     */
    bool UnregisterUpgraders(TypeRegistry& registry, std::string_view typeName) noexcept;

    /** @brief How an `Upgrade` went. */
    enum class UpgradeStatus
    {
        /** @brief The tree is at its type's version: every step ran, or none was needed. */
        Succeeded,
        /** @brief No upgrader steps from `UpgradeResult::version`, so the tree stopped there, with one error. */
        MissingStep,
        /** @brief The tree is newer than its type, so it was left as it is to be read so, with one warning. */
        Newer,
        /** @brief The registry lacks the type: nothing ran or was logged, and the caller keeps the tree. */
        UnknownType,
    };

    /** @brief What `Upgrade` gives: how it went, the version the tree is now at and its diagnostics. */
    struct UpgradeResult
    {
        /** @brief How it went. */
        UpgradeStatus status = UpgradeStatus::Succeeded;
        /**
         * @brief The version the tree is now at: the type's when `Succeeded`, the version reached when `MissingStep`,
         * and the version passed in when `Newer` or `UnknownType`.
         */
        std::uint32_t version = 0;
        /**
         * @brief The one diagnostic of a `MissingStep` or a `Newer` tree, already logged once; none otherwise. It is
         * at the tree's root, or at line 1, column 1 for a tree built in code.
         */
        std::vector<Diagnostic> diagnostics;
    };

    /**
     * @brief Upgrades `tree`, a value of the type registered under `typeName` written at `fromVersion`, to the type's
     * version, running the type's upgraders one step at a time in version order (ADR-0077 §6), before `FromTree`
     * reads it. Text and encoded trees alike (ADR-0085 §2).
     *
     * A missing step is one error, leaving the tree at the version it reached. A tree newer than its type is left as
     * it is with one warning: there is no downgrade, and `FromTree` reads what it can. The caller reads `fromVersion`
     * from wherever its file keeps it, so this names no envelope key.
     *
     * @param typeName The type's canonical or a former name.
     * @param path The path diagnostics name the tree's file by, as `ReadText`'s and `FromTree`'s do.
     */
    [[nodiscard]] UpgradeResult Upgrade(const TypeRegistry& registry, std::string_view typeName,
                                        std::uint32_t fromVersion, Value& tree, std::string_view path, ILogger& logger);
} // namespace Fenrir::Serialisation
