#pragma once

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <string_view>
#include <vector>

namespace Fenrir::Serialisation
{
    /**
     * @brief One field a file holds, as a visit pushed it: its description and the value it refers to, read-only for
     * `ToTree` (`ConstValueRef`) and writable for `FromTree` (`ValueRef`).
     */
    template <typename Ref>
    struct HeldField
    {
        const FieldInfo* info;
        Ref value;
    };

    namespace Detail
    {
        // The key a tagged union's tag is written under, whatever the tag field is named in the type (`m_type` in the
        // engine's unions): `ToTree` writes it and `FromTree` reads it (Bridge.hpp).
        inline constexpr std::string_view UnionTagKey = "kind";

        // Keeps, in the order a visit pushes them, every field but a transient one (FieldInfo::transient): read-back
        // state that stays in the type's layout and that no file holds, so the bridge never writes, compares or reads
        // it.
        template <typename Visitor, typename Ref>
        class HeldFieldCollector final : public Visitor
        {
          public:
            explicit HeldFieldCollector(std::vector<HeldField<Ref>>& fields) noexcept : m_fields(fields)
            {
            }

            void Field(const FieldInfo& field, Ref value) override
            {
                if (!field.transient)
                    m_fields.push_back({&field, value});
            }

          private:
            std::vector<HeldField<Ref>>& m_fields;
        };

        template <typename Visitor, typename Ref>
        std::vector<HeldField<Ref>> HeldFieldsOf(Ref value)
        {
            std::vector<HeldField<Ref>> fields;
            HeldFieldCollector<Visitor, Ref> collector(fields);
            value.Visit(collector);
            return fields;
        }
    } // namespace Detail

    /**
     * @brief The fields a file holds of the struct `value` refers to, as its description's visitor pushes them, every
     * transient field left out. For a tagged union that is its tag and then its live alternative, neither of which
     * can be transient.
     */
    inline std::vector<HeldField<ConstValueRef>> HeldFieldsOf(ConstValueRef value)
    {
        return Detail::HeldFieldsOf<ConstFieldVisitor>(value);
    }

    /** @copydoc HeldFieldsOf(ConstValueRef) */
    inline std::vector<HeldField<ValueRef>> HeldFieldsOf(ValueRef value)
    {
        return Detail::HeldFieldsOf<FieldVisitor>(value);
    }

    /**
     * @brief Whether `key` is the name of one of `type`'s transient fields: a key a file should not hold, which
     * HeldFieldsOf leaves nothing to read into.
     */
    inline bool NamesTransientField(const TypeDescription& type, std::string_view key)
    {
        for (const FieldInfo& field : type.fields)
            if (field.transient && field.name == key)
                return true;
        return false;
    }
} // namespace Fenrir::Serialisation
