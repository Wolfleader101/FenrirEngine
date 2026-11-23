#include "FenrirSerialisation/Upgrade.hpp"

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <utility>

namespace Fenrir::Serialisation
{
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
        UpgradersTag::Value* upgraders = registry.Attachment<UpgradersTag>(typeName);
        if (upgraders == nullptr)
        {
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
                          Value& tree, ILogger& logger)
    {
        const TypeDescription* type = registry.Find(typeName);
        if (type == nullptr)
            return {.status = UpgradeStatus::UnknownType, .version = fromVersion};
        if (fromVersion > type->version)
        {
            logger.Warn("A {} written at version {} is newer than the type's version {}; it is read as it is.",
                        type->name, fromVersion, type->version);
            return {.status = UpgradeStatus::Newer, .version = fromVersion};
        }

        static const UpgradersTag::Value none;
        const UpgradersTag::Value* attached = registry.Attachment<UpgradersTag>(typeName);
        const UpgradersTag::Value& upgraders = attached != nullptr ? *attached : none;
        std::uint32_t version = fromVersion;
        while (version < type->version)
        {
            const auto step = upgraders.find(version);
            if (step == upgraders.end())
            {
                logger.Error("The type {} has no upgrader from version {} to {}; its tree stays at version {}.",
                             type->name, version, version + 1, version);
                return {.status = UpgradeStatus::MissingStep, .version = version};
            }
            step->second(version, tree);
            ++version;
        }
        return {.status = UpgradeStatus::Succeeded, .version = version};
    }
} // namespace Fenrir::Serialisation
