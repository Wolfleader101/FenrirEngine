#pragma once

// FenrirAssets' data asset test package: game-defined data asset types, as a game writes them, run through both real
// tools (fenrir-describe-api writes DataAssetsTestPackage.api.json beside it, and fenrir-generate-type-registry writes
// Generated/DataAssetsTestPackageTypes.* from that), whose committed registration FenrirAssets' tests compile and
// register into the app's type registry.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"

namespace AssetsTest
{
    /** @brief A weapon's numbers: a struct a data asset holds by value. */
    struct WeaponStats
    {
        float damage = 10.0f;
        float weight = 1.0f;
    };

    /**
     * @brief A data asset that others extend: a string, a list, a nested struct, and handles to an image and to another
     * weapon.
     */
    struct WeaponData
    {
        /** @brief What a menu calls the weapon. */
        std::string displayName = "Weapon";
        /** @brief Words a shop filters by. */
        std::vector<std::string> tags;
        WeaponStats stats;
        /** @brief The icon a menu shows. */
        Fenrir::AssetHandle<Fenrir::Image> icon;
        /** @brief The weapon this one upgrades to, of any weapon type. */
        Fenrir::AssetHandle<WeaponData> upgradesTo;
    };

    /**
     * @brief A data asset extending another: its base's fields first, then its own. Version 1 called `edge`
     * `sharpness`.
     *
     * @version 2
     */
    struct Sword : WeaponData
    {
        /** @brief How sharp the blade is, from 0 to 1. */
        float edge = 0.5f;
    };

    /** @brief A data asset of its own, extending nothing: not a weapon. */
    struct ShieldData
    {
        float block = 0.5f;
        /** @brief The sword the shield is made to parry: a sword, not any weapon. */
        Fenrir::AssetHandle<Sword> parries;
    };
} // namespace AssetsTest

// The types of the format research's golden sword (FenrirSerialisation's fixtures/sword.asset.json5), field for field
// as FenrirSerialisation's hand-written golden types have them, so the store loads the golden and saves it back.
namespace MyGame
{
    /** @brief One row of a sword's upgrade table. */
    struct Upgrade
    {
        std::int32_t level = 0;
        std::int32_t cost = 0;
        float damageBonus = 0.0f;
    };

    /**
     * @brief The golden sword: `description` is declared but the golden leaves it at its default, and `mesh` and
     * `onHit` are plain strings, as the golden's types have them.
     *
     * @version 2
     */
    struct Sword
    {
        std::string displayName;
        std::string description;
        float damage = 10.0f;
        float durability = 100.0f;
        std::string mesh;
        std::vector<std::string> tags;
        std::vector<Upgrade> upgrades;
        std::string onHit;
    };

    /** @brief The golden sword's sub-asset. */
    struct ParticleEffect
    {
        std::filesystem::path texture;
        std::int32_t count = 0;
        float lifetime = 1.0f;
    };
} // namespace MyGame
