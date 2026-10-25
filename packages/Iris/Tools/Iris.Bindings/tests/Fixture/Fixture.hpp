#pragma once

// The binding generator's fixture: one of everything an API description records, and Fixture.api.json beside it, its
// description. The header is here so the generated mappings and defaults test compile and run against the real types.

#include <cstdint>
#include <limits>
#include <numbers>

namespace Fixture
{
    /** @brief A bound type built like a maths library's vector: each component in an anonymous union of names. */
    struct Vec3
    {
        Vec3() = default;

        explicit Vec3(float scalar) : x(scalar), y(scalar), z(scalar)
        {
        }

        Vec3(float x, float y, float z) : x(x), y(y), z(z)
        {
        }

        union {
            float x, r;
        };
        union {
            float y, g;
        };
        union {
            float z, b;
        };
    };

    /**
     * @brief A bound type whose constructor takes its components in another order than it stores them, under
     * parameter names with a leading underscore, and stores them in an anonymous struct inside an anonymous union.
     */
    struct Quat
    {
        Quat() = default;

        Quat(float _w, float _x, float _y, float _z) : x(_x), y(_y), z(_z), w(_w)
        {
        }

        union {
            struct
            {
                float x, y, z, w;
            };
            float data[4];
        };
    };

    /** @brief A bound type that is not plain data: described by layout only. */
    class Handle
    {
      public:
        Handle() = default;

      private:
        std::uint32_t m_id = ~0u;
        void* m_owner = nullptr;
    };

    /** @brief A pair of values, as a class template. */
    template <typename T>
    struct Pair
    {
        T first;
        T second;
    };

    /** @brief A bound type that is a template's specialisation nothing here instantiates: the tool instantiates it. */
    using Size = Pair<float>;

    /** @brief How a body moves. */
    enum class Motion : std::uint8_t
    {
        /** @brief Never moves. */
        Still,
        /** @brief Moved by game code. */
        Driven,
        Simulated
    };

    /** @brief A signed enum with explicit values. */
    enum class Offset : std::int32_t
    {
        Back = -2,
        None = 0,
        Far = 0x40000000
    };

    /**
     * @brief Every number kind, each with a literal default.
     *
     * A second paragraph, which is kept apart from the first.
     */
    struct Numbers
    {
        std::int8_t i8 = -8;
        std::uint8_t u8 = 200;
        std::int16_t i16 = -16;
        std::uint16_t u16 = 65535;
        std::int32_t i32 = -32;
        /** @brief All bits set, written `~0u`. */
        std::uint32_t u32 = ~0u;
        std::int64_t i64 = -64;
        std::uint64_t u64 = 18446744073709551615ull;
        float f32 = 0.05f;
        double f64 = 0.1;
        bool flag = true;
        char16_t letter = u'A';
        /** @brief An integer literal for a float. */
        float whole = 1;
        std::uint32_t shifted = 1u << 3;
        float zero = {};
        Offset offset = Offset::Back;
    };

    /** @brief The std::numeric_limits and std::numbers defaults. */
    struct Limits
    {
        float infinity = std::numeric_limits<float>::infinity();
        float largest = std::numeric_limits<float>::max();
        float lowest = std::numeric_limits<float>::lowest();
        double wideInfinity = std::numeric_limits<double>::infinity();
        std::int32_t maxInt = std::numeric_limits<std::int32_t>::max();
        std::uint64_t maxUnsigned = std::numeric_limits<std::uint64_t>::max();
        /** @brief A std::numbers constant, with operators applied. */
        float quarterTurn = -std::numbers::pi_v<float> / 2.0f;
    };

    /** @brief A nested struct with defaults of its own. */
    struct Spring
    {
        float stiffness = 2.0f;
        float damping = 0.5f;
    };

    /** @brief Holds nothing: an empty struct is one byte. */
    struct Marker
    {
    };

    /**
     * @brief A component using each kind of field: enums, bound types, nested structs, arrays and pointers. Its doc
     * comment is wrapped over
     * three lines.
     */
    struct Body
    {
        Motion motion = Motion::Simulated;
        /** @brief Kilograms. */
        float mass = 1.0f;
        /** @brief One argument sets every component. */
        Vec3 velocity = Vec3(0.0f);
        Vec3 offset = Vec3(1.0f, 2.0f, 3.0f);
        /** @brief The arguments are matched to the components by name. */
        Quat rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Spring spring = {};
        Spring braced = Spring{};
        Spring plain;
        /** @brief Sets one member; the other keeps the nested struct's own default. */
        Spring tuned = {.stiffness = 4.0f};
        Handle owner;
        Numbers numbers;
        Limits limits;
        Marker marker;
        float samples[4] = {1.0f, 2.0f};
        Motion modes[2] = {Motion::Driven, Motion::Still};
        Spring springs[2];
        std::uint8_t zeros[3] = {};
        void* userData = nullptr;
        const Body* next = nullptr;
    };

    struct BoxShape
    {
        Vec3 halfExtents = Vec3(0.5f);
    };

    struct SphereShape
    {
        float radius = 0.5f;
    };

    /** @brief Which kind of shape a Shape holds. */
    enum class ShapeType : std::uint8_t
    {
        Box,
        Sphere
    };

    /** @brief A tagged union: one enum tag followed by one anonymous union. */
    class Shape
    {
      public:
        /** @brief A box with its own defaults. */
        Shape() : Shape(BoxShape{})
        {
        }

        Shape(BoxShape box) : m_type(ShapeType::Box), m_box(box)
        {
        }

        Shape(SphereShape sphere) : m_type(ShapeType::Sphere), m_sphere(sphere)
        {
        }

        [[nodiscard]] ShapeType Type() const
        {
            return m_type;
        }

      private:
        ShapeType m_type;
        union {
            /** @brief Held when the tag is Box. */
            BoxShape m_box;
            SphereShape m_sphere;
        };
    };

    /** @brief A component holding a tagged union. */
    struct Collider
    {
        Shape shape;
        bool solid = true;
    };

    /** @brief A shared struct: plain data a native function fills through a pointer. */
    struct Hit
    {
        Handle body;
        Vec3 point = Vec3(0.0f);
        float distance = std::numeric_limits<float>::infinity();
    };

    /** @brief A distinct integer type: an enum with no enumerators, recorded as the integer it is. */
    enum class Id : std::uint32_t
    {
    };

    /** @brief The null Id, as a constant that converts to one (as EnTT's entt::null does). */
    struct NullId
    {
        constexpr operator Id() const
        {
            return static_cast<Id>(~0u);
        }
    };

    inline constexpr NullId noId{};

    /**
     * @brief A shared struct whose members are private: described all the same, since the selection's private-members
     * names it.
     */
    class Tag
    {
      public:
        Tag() = default;

      private:
        /** @brief An Id: its default is the constant its initialiser converts to. */
        Id m_id = noId;
        const void* m_owner = nullptr;
        /** @brief Characters: bytes, on every target. */
        char m_label[4] = {'a'};
    };

    /** @brief A scene resource: plain data, as a component is, holding a UTF-32 code point. */
    struct Weather
    {
        float wind = 0.5f;
        /** @brief A char32_t: recorded as the u32 it is. */
        char32_t glyph = U'*';
    };

    /** @brief An app resource. */
    struct Clock
    {
        double seconds = 0.0;
    };

    /** @brief An app resource that is a tagged union. */
    class Sky
    {
      public:
        Sky() : Sky(BoxShape{})
        {
        }

        Sky(BoxShape box) : m_type(ShapeType::Box), m_box(box)
        {
        }

        Sky(SphereShape sphere) : m_type(ShapeType::Sphere), m_sphere(sphere)
        {
        }

      private:
        ShapeType m_type;
        union {
            BoxShape m_box;
            SphereShape m_sphere;
        };
    };

    /** @brief An id: a struct of one field, which a native takes and returns by value as that field. */
    struct BodyId
    {
        std::uint32_t value = 0;
    };

    /** @brief A bound type of one member, which a binding may pass to a native by value as that member. */
    class Ticket
    {
      public:
        Ticket() = default;

      private:
        std::uint32_t m_number = 0;
    };

    /** @brief A direction no field holds, which a native takes: described because the selection names it. */
    enum class Heading : std::uint8_t
    {
        North,
        /** @brief Clockwise from North. */
        East,
        South,
        West
    };

    /** @brief An event between two handles. */
    struct Touched
    {
        Handle first;
        Handle second;
    };
} // namespace Fixture
