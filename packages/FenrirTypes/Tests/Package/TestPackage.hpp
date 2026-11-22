#pragma once

// The FenrirTypes test package: a header that is no engine package's, holding one of everything a type registration
// describes, run through both real tools (fenrir-describe-api writes TestPackage.api.json beside it, and
// fenrir-generate-type-registry writes Generated/TestPackageTypes.* from that), whose committed output the tests
// compile and exercise.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The engine's entity and asset handle, as describe-api recognises them by qualified name: the shapes FenrirECS and
// FenrirAssets declare, so the package builds against FenrirTypes alone.
namespace Fenrir
{
    class Entity
    {
      public:
        Entity() = default;
        Entity(std::uint32_t id, void* entityList) : m_entityId(id), m_entityList(entityList)
        {
        }

      private:
        std::uint32_t m_entityId = 0xFFFFFFFFu;
        void* m_entityList = nullptr;
    };

    template <typename T>
    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
} // namespace Fenrir

namespace Test
{
    struct Mesh;

    /** @brief How rare an item is. */
    enum class Rarity : std::uint8_t
    {
        Common,
        /** @brief Found once a level. */
        Rare = 3,
    };

    /** @brief A bound type: a maths library's two-component vector, visited as its components. */
    struct Vec2
    {
        Vec2() = default;

        Vec2(float x, float y) : x(x), y(y)
        {
        }

        float x = 0.0f;
        float y = 0.0f;
    };

    /**
     * @brief A component holding the three fixed-layout kinds (an entity, a fixed string and a reference), a bound
     * type, an enum, and every field doc command.
     *
     * @version 3
     * @formerly Test::Beacon
     * @formerly Test::Flag
     */
    struct Marker
    {
        /**
         * @brief The entity the marker points at.
         *
         * @formerly pointee
         */
        Fenrir::Entity target;
        /**
         * @brief What the marker says.
         *
         * @display Caption
         * @multiline
         */
        char label[16] = "marker";
        /**
         * @brief The mesh drawn at the marker.
         *
         * @group Look
         */
        Fenrir::AssetHandle<Mesh> mesh;
        /**
         * @brief How far the marker sits from its entity.
         *
         * @range -10 10
         * @step 0.25
         * @units m
         */
        Vec2 offset{0.0f, 1.0f};
        /** @readonly */
        Rarity rarity = Rarity::Rare;
        /** @hidden */
        std::uint32_t serial = 7;
    };

    /**
     * @brief A scene resource: plain run-time data a scene file never holds.
     *
     * @transient
     */
    struct Weather
    {
        float wind = 2.5f;
    };

    /**
     * @brief An app resource an editor keeps and a cooked build strips.
     *
     * @editor-only
     */
    struct Layers
    {
        std::uint32_t visible = 0xFFu;
    };

    /** @brief Which alternative a Volume holds. */
    enum class VolumeKind : std::uint8_t
    {
        Box,
        Circle = 2,
    };

    /** @brief A box's half extents, as a Volume's alternative. */
    struct Box
    {
        Vec2 halfExtents{0.5f, 0.5f};
    };

    /** @brief A tagged union: a struct alternative, the default, and a scalar one, its tag value not its index. */
    class Volume
    {
      public:
        Volume() : Volume(Box{})
        {
        }

        Volume(Box box) : m_kind(VolumeKind::Box), m_box(box)
        {
        }

        explicit Volume(float radius) : m_kind(VolumeKind::Circle), m_radius(radius)
        {
        }

        [[nodiscard]] VolumeKind Kind() const
        {
            return m_kind;
        }

        [[nodiscard]] float Radius() const
        {
            return m_radius;
        }

      private:
        VolumeKind m_kind;
        union {
            Box m_box;
            /** @brief A circle's radius. */
            float m_radius;
        };
    };

    /** @brief A component holding a tagged union. */
    struct Trigger
    {
        Volume volume;
        bool once = false;
    };

    /** @brief A row of a data asset's table: a nested struct holding a string, so visited by member too. */
    struct Stat
    {
        std::string name;
        float value = 1.0f;
    };

    /** @brief A data asset that others extend: a string and a list of nested structs. */
    struct WeaponData
    {
        /** @brief What a menu calls the weapon. */
        std::string displayName = "Weapon";
        /** @brief Its numbers, by name. */
        std::vector<Stat> stats;
        float damage = 10.0f;
    };

    /**
     * @brief A data asset extending another: its base's fields first, then its own (an optional, a path and a list of
     * strings).
     *
     * @version 2
     * @formerly Test::Blade
     */
    struct Sword : WeaponData
    {
        /** @brief How sharp the edge is, when it has one. */
        std::optional<float> edge;
        /** @brief The icon a menu shows. */
        std::filesystem::path icon = "icons/sword.png";
        std::vector<std::string> titles;
    };

    /** @brief A settings type, as an Install config is: a path, a list of strings and a number. */
    struct Options
    {
        std::filesystem::path root = "assets";
        std::vector<std::string> extensions;
        std::uint32_t threads = 4;
    };
} // namespace Test
