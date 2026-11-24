#include "FenrirSerialisation/Upgrade.hpp"

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <format>
#include <string>
#include <utility>

namespace Fenrir::Serialisation
{
    namespace
    {
        // The result `status` at `version`, carrying its one diagnostic at `at`, logged once.
        UpgradeResult Reported(UpgradeStatus status, std::uint32_t version, DiagnosticSeverity severity,
                               std::string_view path, Location at, std::string message, ILogger& logger)
        {
            const Diagnostic diagnostic{.severity = severity,
                                        .path = std::string(path),
                                        .line = at.line,
                                        .column = at.column,
                                        .message = std::move(message)};
            if (severity == DiagnosticSeverity::Error)
                logger.Error("{}", FormatDiagnostic(diagnostic));
            else
                logger.Warn("{}", FormatDiagnostic(diagnostic));
            return {.status = status, .version = version, .diagnostics = {diagnostic}};
        }
    } // namespace

    bool RegisterUpgrader(TypeRegistry& registry, std::string_view typeName, std::uint32_t fromVersion,
                          Upgrader upgrader, ILogger& logger)
    {
        const TypeDescription* type = registry.Find(typeName);
        if (type == nullptr)
        {
            logger.Error("An upgrader from version {} names the type {}, which no package registered.", fromVersion,
                         typeName);
            return false;
        }
        if (!upgrader)
        {
            logger.Error("The upgrader from version {} for the type {} is empty; it is refused.", fromVersion,
                         type->name);
            return false;
        }
        UpgradersTag::Value* upgraders = registry.Attachment<UpgradersTag>(typeName);
        if (upgraders == nullptr)
        {
            // Find found the type just above, so the attachment is hung and found again.
            registry.Attach<UpgradersTag>(typeName, {});
            upgraders = registry.Attachment<UpgradersTag>(typeName);
        }
        if (!upgraders->try_emplace(fromVersion, std::move(upgrader)).second)
        {
            logger.Error("The type {} already has an upgrader from version {}; the second is refused.", type->name,
                         fromVersion);
            return false;
        }
        return true;
    }

    UpgradeResult Upgrade(const TypeRegistry& registry, std::string_view typeName, std::uint32_t fromVersion,
                          Value& tree, std::string_view path, ILogger& logger)
    {
        const TypeDescription* type = registry.Find(typeName);
        if (type == nullptr)
            return {.status = UpgradeStatus::UnknownType, .version = fromVersion};
        // Taken before any step runs, since an upgrader may replace the root.
        const Location root = tree.location.value_or(Location{});
        if (fromVersion > type->version)
            return Reported(UpgradeStatus::Newer, fromVersion, DiagnosticSeverity::Warning, path, root,
                            std::format("A {} written at version {} is newer than the type's version {}; it is read "
                                        "as it is.",
                                        type->name, fromVersion, type->version),
                            logger);

        static const UpgradersTag::Value none;
        const UpgradersTag::Value* attached = registry.Attachment<UpgradersTag>(typeName);
        const UpgradersTag::Value& upgraders = attached != nullptr ? *attached : none;
        std::uint32_t version = fromVersion;
        while (version < type->version)
        {
            const auto step = upgraders.find(version);
            if (step == upgraders.end())
                return Reported(UpgradeStatus::MissingStep, version, DiagnosticSeverity::Error, path, root,
                                std::format("The type {} has no upgrader from version {} to {}; its tree stays at "
                                            "version {}.",
                                            type->name, version, version + 1, version),
                                logger);
            step->second(version, tree);
            ++version;
        }
        return {.status = UpgradeStatus::Succeeded, .version = version};
    }
} // namespace Fenrir::Serialisation
