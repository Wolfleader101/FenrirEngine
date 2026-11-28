#pragma once

// FenrirAssets' data asset test package: game-defined data asset types, components and a scene resource, as a game
// writes them, run through both real tools (fenrir-describe-api writes DataAssetsTestPackage.api.json beside it, and
// fenrir-generate-type-registry writes Generated/DataAssetsTestPackageTypes.* from that), whose committed registration
// FenrirAssets' tests compile and register into the app's type registry.

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"

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

    /**
     * @brief A component a scene file writes an entity id and an asset path for: it follows another entity, showing
     * an icon.
     */
    struct Follower
    {
        /** @brief The entity followed. */
        Fenrir::Entity target;
        /** @brief The icon shown over the follower. */
        Fenrir::AssetHandle<Fenrir::Image> icon;
        float speed = 1.0f;
    };

    /**
     * @brief A component at a version above 1, which a scene file's entry spells before its fields.
     *
     * @version 2
     */
    struct Health
    {
        float current = 100.0f;
        float maximum = 100.0f;
    };

    /**
     * @brief Run-time bookkeeping a scene file never holds.
     *
     * @transient
     */
    struct FrameCounter
    {
        std::uint32_t frames = 0;
    };

    /**
     * @brief A described scene resource: a sky's settings, as FenrirRenderer's `Fenrir::Sky` holds its own, which a
     * scene file writes under `resources`.
     */
    struct Sky
    {
        /** @brief The cubemap the sky shows. */
        Fenrir::AssetHandle<Fenrir::Image> cubemap;
        float exposure = 1.0f;
    };

    /** @brief A plain look: a flat tint. */
    struct PlainLook
    {
        float tint = 1.0f;
    };

    /** @brief A painted look: an image, scaled. */
    struct PaintedLook
    {
        /** @brief The image painted on. */
        Fenrir::AssetHandle<Fenrir::Image> image;
        float scale = 1.0f;
    };

    /** @brief Which kind of look a Look holds. */
    enum class LookType : std::uint8_t
    {
        Plain,
        Painted
    };

    /**
     * @brief A tagged union, as the engine's are (`Fenrir::CollisionShape`, `Fenrir::Sky`): its tag is the field
     * `m_type`, which a file writes as `kind`, so an asset path held by its painted look sits beside that `kind`.
     */
    class Look
    {
      public:
        /** @brief A plain look (`PlainLook{}`). */
        Look() : Look(PlainLook{})
        {
        }

        /** @brief A plain look. */
        Look(PlainLook plain) : m_type(LookType::Plain), m_plain(plain)
        {
        }

        /** @brief A painted look. */
        Look(PaintedLook painted) : m_type(LookType::Painted), m_painted(painted)
        {
        }

        /** @brief The kind of look held. */
        [[nodiscard]] LookType Type() const
        {
            return m_type;
        }

        /** @brief The painted look. Asserts that it is one. */
        [[nodiscard]] const PaintedLook& Painted() const
        {
            assert(m_type == LookType::Painted && "Look::Painted: the look is a different kind");
            return m_painted;
        }

      private:
        LookType m_type;
        union {
            PlainLook m_plain;
            PaintedLook m_painted;
        };
    };

    /** @brief A data asset holding a tagged union: a banner, with its look. */
    struct BannerData
    {
        Look look;
    };

    /** @brief A component holding a tagged union: a decal on an entity, with its look. */
    struct Decal
    {
        Look look;
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
