#pragma once

// Types the tool refuses, one per diagnostic. The tests select each one alone and check the diagnostic it gives.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

    struct Mesh;

    template <typename T>
    struct AssetHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
} // namespace Fenrir

namespace Failing
{
    float Scale();

    struct UnknownDefault
    {
        float scale = Scale();
    };

    class PrivateMember
    {
      public:
        float visible = 1.0f;

      private:
        float m_hidden = 0.0f;
    };

    struct ReferenceMember
    {
        float& target;
    };

    struct VirtualFunction
    {
        virtual void Update();
        float value = 0.0f;
    };

    struct Base
    {
        float inherited = 0.0f;
    };

    struct BaseWithData : Base
    {
        float own = 0.0f;
    };

    struct Copied
    {
        Copied() = default;
        Copied(const Copied& other);
        float value = 0.0f;
    };

    struct NonTriviallyCopyableMember
    {
        Copied member;
    };

    /** @brief An enum, which a selection cannot give a role. */
    enum class Mode
    {
        Off
    };

    struct Inexpressible
    {
        long double wide = 0.0L;
    };

    struct NoInitialiser
    {
        float value;
    };

    struct Pair
    {
        Pair() = default;

        Pair(float a, float b) : first(a), second(b)
        {
        }

        float first, second;
    };

    struct UnnamedArguments
    {
        Pair pair = Pair(1.0f, 2.0f);
    };

    /** @brief Holds a nested type with a private member: described only when private-members names PrivateMember. */
    struct HoldsPrivateMember
    {
        PrivateMember member;
    };

    struct MalformedRange
    {
        /** @range 10 */
        float speed = 0.0f;
    };

    /** @version 0 */
    struct VersionZero
    {
        float value = 0.0f;
    };

    struct FieldVersion
    {
        /** @version 2 */
        float value = 0.0f;
    };

    /** @range 0 1 */
    struct TypeRange
    {
        float value = 0.0f;
    };

    struct FormerlyWithoutName
    {
        /** @formerly */
        float value = 0.0f;
    };

    struct UnknownCommand
    {
        /** @colour red */
        float value = 0.0f;
    };

    /** @brief A doc command on an enumerator, which takes none. */
    enum class Tinted
    {
        /** @hidden */
        Red
    };

    struct EnumeratorCommand
    {
        Tinted tint = Tinted::Red;
    };

    int Count();

    struct UnevaluatedDefault
    {
        int count = Count() * 2;
    };

    struct NamedEntity
    {
        Fenrir::Entity owner{7, nullptr};
    };

    struct NamedAsset
    {
        Fenrir::AssetHandle<Fenrir::Mesh> mesh{1, 1};
    };

    struct ComputedText
    {
        char label[8] = {static_cast<char>(Count())};
    };

    // The owning kinds (string, list, optional, path) belong to data assets and settings types.

    struct NamedComponent
    {
        std::string name;
    };

    enum class TransientTagKind : std::uint8_t
    {
        Number,
        Count,
    };

    struct TransientTag
    {
        TransientTag() : kind(TransientTagKind::Number), number{}
        {
        }

        /** @transient */
        TransientTagKind kind;
        union
        {
            float number;
            std::int32_t count;
        };
    };

    struct ListedEvent
    {
        std::vector<float> values;
    };

    struct MaybeResource
    {
        std::optional<float> value;
    };

    struct RootedShared
    {
        std::filesystem::path root;
    };

    /** @brief A nested struct holding a string, which a component reaches and a data asset reaches too. */
    struct Labelled
    {
        std::string label;
    };

    struct HoldsLabelled
    {
        Labelled labelled;
    };

    struct AssetHoldsLabelled
    {
        Labelled labelled;
    };

    struct HoldsLabelledOnly
    {
        Labelled labelled;
    };

    struct VirtualAsset
    {
        virtual ~VirtualAsset() = default;
        float value = 0.0f;
    };

    struct SharedBase
    {
        float value = 0.0f;
    };

    struct ExtendsShared : SharedBase
    {
        float own = 0.0f;
    };

    struct AssetA
    {
        float a = 0.0f;
    };

    struct AssetB
    {
        float b = 0.0f;
    };

    struct ExtendsTwo : AssetA, AssetB
    {
    };

    struct SettingsBase
    {
        float inherited = 0.0f;
    };

    struct ExtendsSettings : SettingsBase
    {
        float own = 0.0f;
    };

    struct DoublyOptional
    {
        std::optional<std::optional<float>> value;
    };

    struct Flags
    {
        std::vector<bool> flags;
    };

    struct FilledList
    {
        std::vector<float> values = {1.0f, 2.0f};
    };

    struct FilledOptional
    {
        std::optional<float> value = 1.0f;
    };

    struct ComputedName
    {
        std::string name = std::string(3, 'a');
    };

    struct ArrayOfStrings
    {
        std::string names[2];
    };

    struct NamedAssetBase
    {
        std::string name;
    };

    struct DerivedAsset : NamedAssetBase
    {
        float value = 0.0f;
    };

    /** @brief A component holding a data asset whose base holds a string. */
    struct HoldsDerivedAsset
    {
        DerivedAsset held;
    };

    /** @brief A data asset holding a component type by value, which holds no owning kind. */
    struct HoldsBase
    {
        Base base;
    };

    /** @brief A data asset holding an entity, which no data asset may: a data asset is not in a scene. */
    struct TargetingAsset
    {
        Fenrir::Entity target;
    };

    struct Slot
    {
        Fenrir::Entity target;
    };

    /** @brief A data asset reaching an entity through a list of nested structs. */
    struct Spawner
    {
        std::vector<Slot> slots;
    };

    /** @brief A component holding an entity, which a component may. */
    struct Anchored
    {
        Fenrir::Entity anchor;
    };

    /** @brief A data asset holding that component by value, and with it the entity. */
    struct HoldsAnchored
    {
        Anchored anchored;
    };
} // namespace Failing
