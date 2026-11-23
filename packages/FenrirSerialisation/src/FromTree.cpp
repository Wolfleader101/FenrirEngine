#include "FenrirSerialisation/Bridge.hpp"

#include "HandedField.hpp"

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Fenrir::Serialisation
{
    namespace
    {
        // One field as the visitor pushed it, writable.
        struct VisitedField
        {
            const FieldInfo* info;
            ValueRef value;
        };

        // Keeps every field a visit pushes, in the order pushed.
        class FieldCollector final : public FieldVisitor
        {
          public:
            explicit FieldCollector(std::vector<VisitedField>& fields) noexcept : m_fields(fields)
            {
            }

            void Field(const FieldInfo& field, ValueRef value) override
            {
                m_fields.push_back({&field, value});
            }

          private:
            std::vector<VisitedField>& m_fields;
        };

        // The fields of the struct `value` refers to, as its description's visitor pushes them: for a tagged union,
        // its tag and then its live alternative.
        std::vector<VisitedField> FieldsOf(ValueRef value)
        {
            std::vector<VisitedField> fields;
            FieldCollector collector(fields);
            value.Visit(collector);
            return fields;
        }

        // What a node is, as a diagnostic names what it found.
        std::string Found(const Value& node)
        {
            switch (node.Kind())
            {
            case ValueKind::Object:
                return "an object";
            case ValueKind::Array:
                return "an array";
            case ValueKind::String:
                return std::format("the string \"{}\"", node.AsString());
            case ValueKind::Number:
                switch (node.AsNumber().Width())
                {
                case NumberWidth::Signed:
                    return std::format("the integer {}", node.AsNumber().AsSigned());
                case NumberWidth::Unsigned:
                    return std::format("the integer {}", node.AsNumber().AsUnsigned());
                case NumberWidth::Single:
                    return std::format("the float {}", node.AsNumber().AsSingle());
                case NumberWidth::Double:
                    return std::format("the float {}", node.AsNumber().AsDouble());
                }
                break;
            case ValueKind::Bool:
                return node.AsBool() ? "true" : "false";
            case ValueKind::Null:
                return "null";
            }
            return "a value";
        }

        // Whether `number`, an integer, is in `T`'s range; its value as a `T` in `out` when it is.
        template <typename T>
        bool IntegerFits(const Number& number, T& out) noexcept
        {
            if (number.Width() == NumberWidth::Signed)
            {
                const std::int64_t value = number.AsSigned();
                if (!std::in_range<T>(value))
                    return false;
                out = static_cast<T>(value);
                return true;
            }
            const std::uint64_t value = number.AsUnsigned();
            if (!std::in_range<T>(value))
                return false;
            out = static_cast<T>(value);
            return true;
        }

        // Whether `number`, an integer, fits an enum's underlying integer kind; its value as the enum holds it.
        bool EnumFits(const Number& number, Kind underlying, std::int64_t& out) noexcept
        {
            const auto fit = [&]<typename T>(T) {
                T value{};
                if (!IntegerFits(number, value))
                    return false;
                out = static_cast<std::int64_t>(value);
                return true;
            };
            switch (underlying)
            {
            case Kind::I8:
                return fit(std::int8_t{});
            case Kind::I16:
                return fit(std::int16_t{});
            case Kind::I32:
                return fit(std::int32_t{});
            case Kind::I64:
                return fit(std::int64_t{});
            case Kind::U8:
                return fit(std::uint8_t{});
            case Kind::U16:
                return fit(std::uint16_t{});
            case Kind::U32:
                return fit(std::uint32_t{});
            case Kind::U64:
                return fit(std::uint64_t{});
            default:
                return false;
            }
        }

        // The enumerator of `type` named `name`; null when it has none.
        const Enumerator* FindEnumerator(const TypeDescription& type, std::string_view name) noexcept
        {
            for (const Enumerator& enumerator : type.enumerators)
                if (enumerator.name == name)
                    return &enumerator;
            return nullptr;
        }

        // The name a diagnostic gives a field below `where`: `range.min`, `tags[2]`.
        std::string Below(std::string_view where, std::string_view name)
        {
            return where.empty() ? std::string(name) : std::format("{}.{}", where, name);
        }

        // Reads a tree into described values, adding to one result. Every Read either takes the node, wholly or
        // value by value below it, or rejects it whole, writing nothing, with one diagnostic counted in `defaulted`.
        class Reader
        {
          public:
            Reader(const Resolvers& resolvers, std::string_view path, BridgeResult& result)
                : m_resolvers(resolvers), m_path(path), m_result(result)
            {
            }

            // Reads `node` into `target`, named `where` in diagnostics; false when the node was rejected whole.
            bool Read(const Value& node, ValueRef target, const std::string& where)
            {
                switch (target.GetKind())
                {
                case Kind::I8:
                    return ReadInteger<std::int8_t>(node, target, where, "an i8");
                case Kind::I16:
                    return ReadInteger<std::int16_t>(node, target, where, "an i16");
                case Kind::I32:
                    return ReadInteger<std::int32_t>(node, target, where, "an i32");
                case Kind::I64:
                    return ReadInteger<std::int64_t>(node, target, where, "an i64");
                case Kind::U8:
                    return ReadInteger<std::uint8_t>(node, target, where, "a u8");
                case Kind::U16:
                    return ReadInteger<std::uint16_t>(node, target, where, "a u16");
                case Kind::U32:
                    return ReadInteger<std::uint32_t>(node, target, where, "a u32");
                case Kind::U64:
                    return ReadInteger<std::uint64_t>(node, target, where, "a u64");
                case Kind::F32:
                    return ReadFloat<float>(node, target, where, "an f32");
                case Kind::F64:
                    return ReadFloat<double>(node, target, where, "an f64");
                case Kind::Bool:
                    if (node.Kind() != ValueKind::Bool)
                        return Reject(node, where, "a bool");
                    *target.Get<bool>() = node.AsBool();
                    return true;
                case Kind::Char16: {
                    std::uint16_t unit = 0;
                    if (node.Kind() != ValueKind::Number || !IsInteger(node.AsNumber()) ||
                        !IntegerFits(node.AsNumber(), unit))
                        return Reject(node, where, "a char16, an integer from 0 to 65535");
                    *target.Get<char16_t>() = static_cast<char16_t>(unit);
                    return true;
                }
                case Kind::Enum:
                    return ReadEnum(node, target, where);
                case Kind::String:
                case Kind::Path:
                    if (node.Kind() != ValueKind::String)
                        return Reject(node, where, "a string");
                    target.SetText(node.AsString());
                    return true;
                case Kind::FixedString:
                    return ReadFixedString(node, target, where);
                case Kind::Array:
                    return ReadArray(node, target, where);
                case Kind::List:
                    return ReadList(node, target, where);
                case Kind::Optional:
                    return ReadOptional(node, target, where);
                case Kind::Reference:
                    return ReadResolved(node, target, where, "an asset path", "readReference",
                                        m_resolvers.readReference,
                                        [&](Fenrir::ReferenceValue reference) { target.SetReference(reference); });
                case Kind::Entity:
                    return ReadResolved(node, target, where, "an entity id", "readEntity", m_resolvers.readEntity,
                                        [&](Fenrir::EntityValue entity) { target.SetEntity(entity); });
                case Kind::Pointer:
                    // Never in a file.
                    return true;
                case Kind::Struct:
                    return ReadStruct(node, target, where);
                }
                return true;
            }

          private:
            static bool IsInteger(const Number& number) noexcept
            {
                return number.Width() == NumberWidth::Signed || number.Width() == NumberWidth::Unsigned;
            }

            void Report(DiagnosticSeverity severity, const Value& node, std::string message)
            {
                const Location at = node.location.value_or(Location{});
                m_result.diagnostics.push_back({.severity = severity,
                                                .path = std::string(m_path),
                                                .line = at.line,
                                                .column = at.column,
                                                .message = std::move(message)});
            }

            // Rejects `node` whole: one error saying what `where` expected, counted in `defaulted`.
            bool Reject(const Value& node, std::string_view where, std::string_view expected)
            {
                return Reject(node, std::format("`{}` expected {}, found {}; it keeps its default", where, expected,
                                                Found(node)));
            }

            bool Reject(const Value& node, std::string message)
            {
                ++m_result.defaulted;
                Report(DiagnosticSeverity::Error, node, std::move(message));
                return false;
            }

            template <typename T>
            bool ReadInteger(const Value& node, ValueRef target, std::string_view where, std::string_view kind)
            {
                if (node.Kind() != ValueKind::Number)
                    return Reject(node, where, kind);
                if (!IsInteger(node.AsNumber()))
                    return Reject(node, std::format("`{}` expected {}, found {}, which is not an integer; it keeps "
                                                    "its default",
                                                    where, kind, Found(node)));
                T value{};
                if (!IntegerFits(node.AsNumber(), value))
                    return Reject(node, std::format("`{}` expected {}, found {}, which is out of its range; it keeps "
                                                    "its default",
                                                    where, kind, Found(node)));
                *target.Get<T>() = value;
                return true;
            }

            template <typename T>
            bool ReadFloat(const Value& node, ValueRef target, std::string_view where, std::string_view kind)
            {
                if (node.Kind() != ValueKind::Number)
                    return Reject(node, where, kind);
                const Number& number = node.AsNumber();
                T value{};
                switch (number.Width())
                {
                case NumberWidth::Signed:
                    value = static_cast<T>(number.AsSigned());
                    break;
                case NumberWidth::Unsigned:
                    value = static_cast<T>(number.AsUnsigned());
                    break;
                case NumberWidth::Single:
                    value = static_cast<T>(number.AsSingle());
                    break;
                case NumberWidth::Double: {
                    const double wide = number.AsDouble();
                    // A finite double beyond a float's range has no float; an infinity or a NaN has one.
                    if constexpr (std::is_same_v<T, float>)
                        if (std::isfinite(wide) && std::abs(wide) > std::numeric_limits<float>::max())
                            return Reject(node, std::format("`{}` expected {}, found {}, which is out of its range; "
                                                            "it keeps its default",
                                                            where, kind, Found(node)));
                    value = static_cast<T>(wide);
                    break;
                }
                }
                *target.Get<T>() = value;
                return true;
            }

            // A reference or an entity: `null` is none, and a string is what `resolve`, the caller's callback, names
            // the value by. A string it names nothing for, or one with no callback to ask, leaves the value empty,
            // with one error counted in `unresolved`. `what` is what the string should be and `callback` the
            // callback's name, for the diagnostic; `set` writes the value.
            template <typename Resolve, typename Set>
            bool ReadResolved(const Value& node, ValueRef target, std::string_view where, std::string_view what,
                              std::string_view callback, const Resolve& resolve, const Set& set)
            {
                using Resolved = typename std::invoke_result_t<Resolve, std::string_view, const FieldInfo&>::value_type;
                if (node.Kind() == ValueKind::Null)
                {
                    set(Resolved{});
                    return true;
                }
                if (node.Kind() != ValueKind::String)
                    return Reject(node, where, std::format("{} or null", what));
                std::optional<Resolved> resolved;
                if (resolve && m_field != nullptr)
                    resolved = HandedField(*m_field, target.Type(),
                                           [&](const FieldInfo& handed) { return resolve(node.AsString(), handed); });
                set(resolved.value_or(Resolved{}));
                if (resolved.has_value())
                    return true;
                ++m_result.unresolved;
                Report(DiagnosticSeverity::Error, node,
                       resolve ? std::format("`{}` names {}, which {} resolves to nothing; it is left empty", where,
                                             Found(node), callback)
                               : std::format("`{}` names {}, but there is no {} to resolve it; it is left empty", where,
                                             Found(node), callback));
                return true;
            }

            // An enumerator's name, or the number `ToTree` writes for a value no enumerator has.
            bool ReadEnum(const Value& node, ValueRef target, std::string_view where)
            {
                const TypeDescription* type = target.Description();
                if (type == nullptr)
                    return true;
                if (node.Kind() == ValueKind::String)
                {
                    if (const Enumerator* enumerator = FindEnumerator(*type, node.AsString()))
                    {
                        target.SetEnumValue(enumerator->value);
                        return true;
                    }
                    return Reject(node, std::format("`{}` expected an enumerator of {}, found {}, which it does not "
                                                    "have; it keeps its default",
                                                    where, type->name, Found(node)));
                }
                std::int64_t value = 0;
                if (node.Kind() == ValueKind::Number && IsInteger(node.AsNumber()) &&
                    EnumFits(node.AsNumber(), type->underlying, value))
                {
                    target.SetEnumValue(value);
                    return true;
                }
                return Reject(node, where, std::format("an enumerator of {}", type->name));
            }

            // Cut to N-1 bytes by `SetText` when too long, which reading it back shows.
            bool ReadFixedString(const Value& node, ValueRef target, std::string_view where)
            {
                if (node.Kind() != ValueKind::String)
                    return Reject(node, where, "a string");
                const std::string& text = node.AsString();
                target.SetText(text);
                // Measured against the array, not the text read back, which a NUL in the string also shortens.
                const std::size_t capacity = target.Type().count - 1;
                const std::size_t kept = target.Text().size();
                if (text.size() > capacity)
                    Report(DiagnosticSeverity::Warning, node,
                           std::format("`{}` holds at most {} bytes, so the {}-byte string was cut to {}", where,
                                       capacity, text.size(), kept));
                return true;
            }

            // A fixed array reads only from an array of its own length, each element in its place.
            bool ReadArray(const Value& node, ValueRef target, const std::string& where)
            {
                if (node.Kind() != ValueKind::Array || node.AsArray().elements.size() != target.Size())
                    return Reject(node, where, std::format("an array of {} elements", target.Size()));
                ReadElements(node.AsArray(), target, where);
                return true;
            }

            // A list takes the array's length, its elements value-initialised before each reads.
            bool ReadList(const Value& node, ValueRef target, const std::string& where)
            {
                if (node.Kind() != ValueKind::Array)
                    return Reject(node, where, "an array");
                target.Resize(0);
                target.Resize(node.AsArray().elements.size());
                ReadElements(node.AsArray(), target, where);
                return true;
            }

            void ReadElements(const Array& array, ValueRef target, const std::string& where)
            {
                for (std::size_t i = 0; i < array.elements.size(); ++i)
                    Read(array.elements[i].value, target.Element(i), std::format("{}[{}]", where, i));
            }

            // `null` disengages; a value fills the one held, or one emplaced, which a rejected value takes back out.
            bool ReadOptional(const Value& node, ValueRef target, const std::string& where)
            {
                if (node.Kind() == ValueKind::Null)
                {
                    target.Reset();
                    return true;
                }
                const bool held = target.HasValue();
                const ValueRef value = held ? target.Value() : target.Emplace();
                if (Read(node, value, where))
                    return true;
                if (!held)
                    target.Reset();
                return false;
            }

            bool ReadStruct(const Value& node, ValueRef target, const std::string& where)
            {
                const TypeDescription* type = target.Description();
                if (type == nullptr)
                    return true;
                if (type->role == Role::BoundType)
                    return ReadBound(node, target, where);
                if (node.Kind() != ValueKind::Object)
                    return Reject(node, where.empty() ? std::string(type->name) : where, "an object");
                if (type->IsTaggedUnion())
                    return ReadUnion(node, target, where);
                ReadFields(node.AsObject(), FieldsOf(target), where, {});
                return true;
            }

            // A bound type is an array of its components, in the order its description visits them.
            bool ReadBound(const Value& node, ValueRef target, const std::string& where)
            {
                const std::vector<VisitedField> components = FieldsOf(target);
                if (node.Kind() != ValueKind::Array || node.AsArray().elements.size() != components.size())
                    return Reject(
                        node, where,
                        std::format("an array of {} components of {}", components.size(), target.Description()->name));
                for (std::size_t i = 0; i < components.size(); ++i)
                {
                    const FieldScope scope(m_field, components[i].info);
                    Read(node.AsArray().elements[i].value, components[i].value, Below(where, components[i].info->name));
                }
                return true;
            }

            // `kind` first, selecting its alternative, then the alternative's fields from the members beside it.
            bool ReadUnion(const Value& node, ValueRef target, const std::string& where)
            {
                const Object& object = node.AsObject();
                const TypeDescription& type = *target.Description();
                const std::string kindName = Below(where, type.tag.name);
                const Value* kind = object.Find(type.tag.name);
                if (kind == nullptr)
                    return Reject(node, std::format("`{}` has no `{}`, so it keeps its default alternative", where,
                                                    type.tag.name));
                const TypeDescription* tagType = type.tag.type.type;
                const Enumerator* selected = kind->Kind() == ValueKind::String && tagType != nullptr
                                                 ? FindEnumerator(*tagType, kind->AsString())
                                                 : nullptr;
                if (selected == nullptr || !SelectUnionAlternative(target, selected->value))
                    return Reject(*kind, std::format("`{}` expected an alternative of {}, found {}; `{}` keeps its "
                                                     "default alternative",
                                                     kindName, type.name, Found(*kind), where));

                const std::vector<VisitedField> fields = FieldsOf(target);
                if (fields.size() < 2)
                    return true;
                const VisitedField& live = fields[1];
                const TypeDescription* alternative = live.value.Description();
                if (live.info->type.kind == Kind::Struct && alternative != nullptr &&
                    alternative->role != Role::BoundType && !alternative->IsTaggedUnion())
                {
                    // Spread into the union's own object beside `kind`, as `ToTree` writes it.
                    ReadFields(object, FieldsOf(live.value), where, type.tag.name);
                    return true;
                }
                std::vector<VisitedField> member{live};
                ReadFields(object, member, where, type.tag.name);
                return true;
            }

            // Selects the alternative `tagValue` names in the union `target` refers to, unless it is live already,
            // whose fields then keep the defaults the instance gave them.
            static bool SelectUnionAlternative(ValueRef target, std::int64_t tagValue)
            {
                const std::vector<VisitedField> fields = FieldsOf(target);
                if (fields.size() >= 2 && fields.front().value.EnumValue() == tagValue)
                    return true;
                return target.SelectAlternative(tagValue);
            }

            // Reads each of `fields` from the member named by it, or else by the first of its former names present,
            // and warns of every member neither reads. Every field claims its current name before any former name is
            // tried, so a field renamed to another's old name keeps its own key. `tagKey` is a key read already, a
            // tagged union's `kind`.
            void ReadFields(const Object& object, const std::vector<VisitedField>& fields, const std::string& where,
                            std::string_view tagKey)
            {
                const std::size_t none = object.members.size();
                std::vector<bool> used(object.members.size(), false);
                const auto claim = [&](std::string_view key) {
                    for (std::size_t m = 0; m < object.members.size(); ++m)
                        if (!used[m] && object.members[m].key == key)
                        {
                            used[m] = true;
                            return m;
                        }
                    return none;
                };
                if (!tagKey.empty())
                    claim(tagKey);

                std::vector<std::size_t> sources(fields.size(), none);
                for (std::size_t f = 0; f < fields.size(); ++f)
                    sources[f] = claim(fields[f].info->name);

                for (std::size_t f = 0; f < fields.size(); ++f)
                {
                    const FieldInfo& info = *fields[f].info;
                    for (std::string_view formerName : info.formerly)
                        for (std::size_t m = claim(formerName); m != none; m = claim(formerName))
                        {
                            if (sources[f] == none)
                            {
                                sources[f] = m;
                                continue;
                            }
                            Report(DiagnosticSeverity::Warning, object.members[m].value,
                                   std::format("`{}` is a former name of `{}`, which is read from `{}`; `{}` is "
                                               "dropped",
                                               Below(where, formerName), Below(where, info.name),
                                               Below(where, object.members[sources[f]].key), Below(where, formerName)));
                        }
                    if (sources[f] != none && info.type.kind != Kind::Pointer)
                    {
                        const FieldScope scope(m_field, &info);
                        Read(object.members[sources[f]].value, fields[f].value, Below(where, info.name));
                    }
                }

                for (std::size_t m = 0; m < object.members.size(); ++m)
                {
                    if (used[m])
                        continue;
                    ++m_result.unknownFields;
                    Report(DiagnosticSeverity::Warning, object.members[m].value,
                           std::format("`{}` names no field, so it is dropped", Below(where, object.members[m].key)));
                }
            }

            // Makes `field` the field being read until it ends, the one before it then again.
            class FieldScope
            {
              public:
                FieldScope(const FieldInfo*& current, const FieldInfo* field) noexcept
                    : m_current(current), m_previous(current)
                {
                    m_current = field;
                }

                ~FieldScope()
                {
                    m_current = m_previous;
                }

                FieldScope(const FieldScope&) = delete;
                FieldScope& operator=(const FieldScope&) = delete;

              private:
                const FieldInfo*& m_current;
                const FieldInfo* m_previous;
            };

            const Resolvers& m_resolvers;
            std::string_view m_path;
            BridgeResult& m_result;
            // The field being read, or holding the element or optional's value being read: the one a resolver is
            // handed.
            const FieldInfo* m_field = nullptr;
        };
    } // namespace

    BridgeResult FromTree(const Value& tree, const TypeDescription& type, const TypeRegistry& registry,
                          const Resolvers& resolvers, void* value, std::string_view path, ILogger& logger)
    {
        BridgeResult result;
        if (registry.Find(type.name) == nullptr)
        {
            result.status = BridgeStatus::UnknownType;
            return result;
        }

        const ValueType valueType{.kind = Kind::Struct, .type = &type};
        Reader(resolvers, path, result).Read(tree, ValueRef(value, valueType), std::string());

        for (const Diagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.severity == DiagnosticSeverity::Error)
                logger.Error("{}", FormatDiagnostic(diagnostic));
            else
                logger.Warn("{}", FormatDiagnostic(diagnostic));
        }
        if (result.defaulted != 0 || result.unknownFields != 0 || result.unresolved != 0)
            result.status = BridgeStatus::Partial;
        return result;
    }
} // namespace Fenrir::Serialisation
