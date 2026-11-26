#pragma once

// FenrirAssets' data asset test package: game-defined data asset types, as a game writes them, run through both real
// tools (fenrir-describe-api writes DataAssetsTestPackage.api.json beside it, and fenrir-generate-type-registry writes
// Generated/DataAssetsTestPackageTypes.* from that), whose committed registration FenrirAssets' tests compile and
// register into the app's type registry.

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
