#pragma once

// Types the tool refuses, one per diagnostic. The tests select each one alone and check the diagnostic it gives.

#include <cstdint>

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
} // namespace Failing
