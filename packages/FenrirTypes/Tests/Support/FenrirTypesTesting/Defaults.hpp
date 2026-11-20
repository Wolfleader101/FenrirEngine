#pragma once

// What a package's generated defaults test (tools/generate-type-registry, [registration] 'test') checks with: a type's
// instance, default-constructed through its description and visited through it, each value the visit reaches compared
// with the default the package's API description records. The generator writes the expected values; this walks the
// instance. Catch2 reports each difference, so the including test links Catch2.

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Fenrir::Testing
{
    /** @brief An empty `list` or `optional`: the only default a description records for one. */
    struct Empty
    {
        friend constexpr bool operator==(const Empty&, const Empty&) = default;
    };

    /**
     * @brief One value a description records as a default: an integer (an enum's value, a `char16`, an unsigned one as
     * its bits in two's complement), a float, a `bool`, text, the null entity, the empty reference, or `Empty`.
     */
    using DefaultValue = std::variant<std::int64_t, double, bool, std::string, EntityValue, ReferenceValue, Empty>;

    /** @brief A value a description records, at its path from the instance: `pos.x`, `m_planes[2].distance`. */
    struct ExpectedDefault
    {
        std::string_view path;
        DefaultValue value;
    };

    namespace Detail
    {
        // One value the walk reached: its kind and what it holds.
        struct ReachedValue
        {
            Kind kind;
            DefaultValue value;
        };

        using Reached = std::map<std::string, ReachedValue, std::less<>>;

        template <typename T>
        std::int64_t Integer(const ConstValueRef& value)
        {
            return static_cast<std::int64_t>(*value.Get<T>());
        }

        // The value as a DefaultValue; a list or optional that holds something as its count.
        inline DefaultValue Read(const ConstValueRef& value)
        {
            switch (value.GetKind())
            {
            case Kind::I8:
                return Integer<std::int8_t>(value);
            case Kind::I16:
                return Integer<std::int16_t>(value);
            case Kind::I32:
                return Integer<std::int32_t>(value);
            case Kind::I64:
                return Integer<std::int64_t>(value);
            case Kind::U8:
                return Integer<std::uint8_t>(value);
            case Kind::U16:
                return Integer<std::uint16_t>(value);
            case Kind::U32:
                return Integer<std::uint32_t>(value);
            case Kind::U64:
                return Integer<std::uint64_t>(value);
            case Kind::Char16:
                return Integer<char16_t>(value);
            case Kind::F32:
                return static_cast<double>(*value.Get<float>());
            case Kind::F64:
                return *value.Get<double>();
            case Kind::Bool:
                return *value.Get<bool>();
            case Kind::Enum:
                return value.EnumValue();
            case Kind::Entity:
                return value.Entity();
            case Kind::Reference:
                return value.Reference();
            case Kind::FixedString:
            case Kind::String:
            case Kind::Path:
                return value.Text();
            case Kind::List:
                return value.Size() == 0 ? DefaultValue(Empty{})
                                         : DefaultValue(static_cast<std::int64_t>(value.Size()));
            case Kind::Optional:
                return value.HasValue() ? DefaultValue(std::int64_t{1}) : DefaultValue(Empty{});
            case Kind::Struct:
            case Kind::Array:
            case Kind::Pointer:
                break;
            }
            return Empty{};
        }

        // Walks a value into `reached` by path: a nested struct (a bound type's components, and a tagged union's tag
        // and live alternative, among them) through its own visit, an array element by element. A pointer is opaque:
        // a description records only a null one, and nothing reads it.
        class DefaultsWalker : public ConstFieldVisitor
        {
          public:
            DefaultsWalker(Reached& reached, std::string prefix) : m_reached(&reached), m_prefix(std::move(prefix))
            {
            }

            void Field(const FieldInfo& field, ConstValueRef value) override
            {
                Reach(m_prefix + std::string(field.name), value);
            }

            void Reach(const std::string& path, const ConstValueRef& value)
            {
                switch (value.GetKind())
                {
                case Kind::Array:
                    for (std::size_t i = 0; i < value.Size(); ++i)
                        Reach(path + "[" + std::to_string(i) + "]", value.Element(i));
                    return;
                case Kind::Struct: {
                    DefaultsWalker nested(*m_reached, path + ".");
                    value.Visit(nested);
                    return;
                }
                case Kind::Pointer:
                    return;
                default:
                    m_reached->insert_or_assign(path, ReachedValue{value.GetKind(), Read(value)});
                }
            }

          private:
            Reached* m_reached;
            std::string m_prefix;
        };

        // Whether a reached value is the expected default: a float compared at the field's own width.
        inline bool Matches(const ReachedValue& reached, const DefaultValue& expected)
        {
            if (reached.kind == Kind::F32 && std::holds_alternative<double>(expected) &&
                std::holds_alternative<double>(reached.value))
                return static_cast<float>(std::get<double>(expected)) ==
                       static_cast<float>(std::get<double>(reached.value));
            return reached.value == expected;
        }

        inline std::string Spelled(const DefaultValue& value)
        {
            return std::visit(
                [](const auto& held) -> std::string {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, std::int64_t> || std::is_same_v<Held, double>)
                        return std::to_string(held);
                    else if constexpr (std::is_same_v<Held, bool>)
                        return held ? "true" : "false";
                    else if constexpr (std::is_same_v<Held, std::string>)
                        return "\"" + held + "\"";
                    else if constexpr (std::is_same_v<Held, EntityValue>)
                        return held == EntityValue{} ? "the null entity" : "an entity";
                    else if constexpr (std::is_same_v<Held, ReferenceValue>)
                        return held == ReferenceValue{} ? "the empty reference" : "a reference";
                    else
                        return "empty";
                },
                value);
        }

        // An instance of a described type, default-constructed through its description and destroyed through it.
        class Instance
        {
          public:
            explicit Instance(const TypeDescription& type)
                : m_type(&type),
                  m_bytes(static_cast<std::byte*>(::operator new(type.size, std::align_val_t{type.alignment})))
            {
                type.Construct(m_bytes);
            }
            ~Instance()
            {
                m_type->Destroy(m_bytes);
                ::operator delete(m_bytes, std::align_val_t{m_type->alignment});
            }
            Instance(const Instance&) = delete;
            Instance& operator=(const Instance&) = delete;

            [[nodiscard]] const void* Bytes() const noexcept
            {
                return m_bytes;
            }

          private:
            const TypeDescription* m_type;
            std::byte* m_bytes;
        };
    } // namespace Detail

    /**
     * @brief Checks that `description`'s type, default-constructed through it and visited through it, reaches each of
     * `expected` at its path, holding its value.
     */
    inline void CheckDefaults(const TypeDescription& description, std::initializer_list<ExpectedDefault> expected)
    {
        const Detail::Instance instance(description);
        Detail::Reached reached;
        Detail::DefaultsWalker walker(reached, "");
        description.Visit(instance.Bytes(), walker);

        for (const ExpectedDefault& want : expected)
        {
            INFO(description.name << "'s " << want.path << " is " << Detail::Spelled(want.value)
                                  << " by its description");
            const auto found = reached.find(want.path);
            REQUIRE(found != reached.end());
            INFO("and the visit of the instance its description constructs reaches "
                 << Detail::Spelled(found->second.value));
            CHECK(Detail::Matches(found->second, want.value));
        }
    }
} // namespace Fenrir::Testing
