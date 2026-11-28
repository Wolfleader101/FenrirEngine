// A data asset's value as the store holds it (ADR-0075, ADR-0083): a C++ type's instance, or a tree-backed type's
// value tree beside its materialised C++ base. Reading one from a tree, composing its whole tree, and assigning one in
// place.

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"
#include "Envelope.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

namespace Fenrir::Detail
{
    namespace Ser = Serialisation;

    namespace
    {
        // The type `type` extends, or null when it extends none or its base is not registered.
        const TypeDescription* BaseOf(const TypeDescription& type, const TypeRegistry& registry)
        {
            return type.base.empty() ? nullptr : registry.Find(type.base);
        }

        // Whether `key` names a field of `layout` or a type up its base chain, by its name or a former one: a member
        // the materialised base reads, not the tree.
        bool LayoutNames(const TypeDescription* layout, const TypeRegistry& registry, std::string_view key)
        {
            for (const TypeDescription* at = layout; at != nullptr; at = BaseOf(*at, registry))
            {
                for (const FieldInfo& field : at->fields)
                {
                    if (field.name == key || std::ranges::find(field.formerly, key) != field.formerly.end())
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // `where` then `.name`, a field's path inside a value as a diagnostic names it.
        std::string Below(std::string_view where, std::string_view name)
        {
            return where.empty() ? std::string(name) : std::format("{}.{}", where, name);
        }

        // The smallest and largest value of an integer kind.
        struct IntegerRange
        {
            std::int64_t min = 0;
            std::uint64_t max = 0;
        };

        template <typename T>
        constexpr IntegerRange RangeOf()
        {
            return {static_cast<std::int64_t>(std::numeric_limits<T>::min()),
                    static_cast<std::uint64_t>(std::numeric_limits<T>::max())};
        }

        // The range of an integer kind (a `char16` is a code unit); none for any other kind.
        std::optional<IntegerRange> IntegerRangeOf(Kind kind)
        {
            switch (kind)
            {
            case Kind::I8:
                return RangeOf<std::int8_t>();
            case Kind::I16:
                return RangeOf<std::int16_t>();
            case Kind::I32:
                return RangeOf<std::int32_t>();
            case Kind::I64:
                return RangeOf<std::int64_t>();
            case Kind::U8:
                return RangeOf<std::uint8_t>();
            case Kind::U16:
            case Kind::Char16:
                return RangeOf<std::uint16_t>();
            case Kind::U32:
                return RangeOf<std::uint32_t>();
            case Kind::U64:
                return RangeOf<std::uint64_t>();
            default:
                return std::nullopt;
            }
        }

        // How a diagnostic names what a value of `type` is.
        std::string Expected(const ValueType& type)
        {
            switch (type.kind)
            {
            case Kind::Bool:
                return "a bool";
            case Kind::F32:
            case Kind::F64:
                return std::format("a number ({})", KindName(type.kind));
            case Kind::Enum:
                return type.type != nullptr ? std::format("an enumerator of {}", type.type->name) : "an enumerator";
            case Kind::Struct:
                return type.type != nullptr ? std::format("a value of {}", type.type->name) : "a struct";
            case Kind::Array:
                return std::format("an array of {} elements", type.count);
            case Kind::List:
                return "an array";
            case Kind::Optional:
                return type.element != nullptr ? std::format("{} or null", Expected(*type.element)) : "a value or null";
            case Kind::Reference:
                return "an asset path or null";
            case Kind::String:
            case Kind::FixedString:
            case Kind::Path:
                return "a string";
            default:
                if (IntegerRangeOf(type.kind))
                {
                    return std::format("an integer ({})", KindName(type.kind));
                }
                return std::format("a value of kind {}", KindName(type.kind));
            }
        }

        // Checks a tree-backed value's own members against its description, in place (ReadDataValue's rules). Each
        // diagnostic is made at once, at the node it is about, before anything moves.
        class TreeChecker
        {
          public:
            TreeChecker(const Ser::Value& root, const std::string& where, std::string_view path,
                        const TypeRegistry& registry, const Ser::Resolvers& resolvers, ReferenceReading& reading,
                        ILogger& logger, std::vector<Ser::Diagnostic>& diagnostics)
                : m_root(root), m_where(where), m_path(path), m_registry(registry), m_resolvers(resolvers),
                  m_reading(reading), m_logger(logger), m_diagnostics(diagnostics)
            {
            }

            // Checks each member of `object` against `fields`, the members' path inside the value being `at`: a key
            // no field names is dropped, a former name read as its field's own, and a value that does not check
            // dropped, its field keeping its default.
            void CheckObject(Ser::Object& object, std::span<const FieldInfo* const> fields, std::string_view at)
            {
                std::vector<const FieldInfo*> claimed(object.members.size(), nullptr);
                std::vector<bool> dropped(object.members.size(), false);
                std::vector<bool> filled(fields.size(), false);
                const auto unclaimed = [&](std::string_view key) -> std::optional<std::size_t> {
                    for (std::size_t m = 0; m < object.members.size(); ++m)
                    {
                        if (claimed[m] == nullptr && !dropped[m] && object.members[m].key == key)
                        {
                            return m;
                        }
                    }
                    return std::nullopt;
                };
                // Every field's current name before any former one, so a field keeps its own key though another field
                // once had that name; of several former names, the first the field lists is read.
                for (std::size_t f = 0; f < fields.size(); ++f)
                {
                    if (const std::optional<std::size_t> m = unclaimed(fields[f]->name))
                    {
                        claimed[*m] = fields[f];
                        filled[f] = true;
                    }
                }
                for (std::size_t f = 0; f < fields.size(); ++f)
                {
                    for (const std::string_view former : fields[f]->formerly)
                    {
                        const std::optional<std::size_t> m = unclaimed(former);
                        if (!m)
                        {
                            continue;
                        }
                        Ser::Member& member = object.members[*m];
                        if (filled[f])
                        {
                            dropped[*m] = true;
                            const std::string current = Below(at, fields[f]->name);
                            Report(
                                Ser::DiagnosticSeverity::Warning, &member.value,
                                std::format("`{}` is a former name of `{}`, which is read from `{}`; `{}` is dropped",
                                            Below(at, former), current, current, Below(at, former)));
                            continue;
                        }
                        member.key = std::string(fields[f]->name);
                        claimed[*m] = fields[f];
                        filled[f] = true;
                    }
                }

                std::vector<Ser::Member> kept;
                kept.reserve(object.members.size());
                for (std::size_t m = 0; m < object.members.size(); ++m)
                {
                    Ser::Member& member = object.members[m];
                    const FieldInfo* field = claimed[m];
                    if (dropped[m])
                    {
                        continue;
                    }
                    const std::string named = Below(at, member.key);
                    if (field == nullptr || field->transient)
                    {
                        Report(Ser::DiagnosticSeverity::Warning, &member.value,
                               field != nullptr
                                   ? std::format("`{}` is transient, which no file holds, so it is dropped", named)
                                   : std::format("`{}` names no field, so it is dropped", named));
                        continue;
                    }
                    if (Check(member.value, field->type, *field, named, named))
                    {
                        kept.push_back(std::move(member));
                    }
                }
                object.members = std::move(kept);
            }

          private:
            // Whether `node`, at `where`, is a value of `type` (the type of `field`, or of an element of it), made one
            // where it can be; when it is not, one located error saying so, naming `owner`, the member dropped for it.
            bool Check(Ser::Value& node, const ValueType& type, const FieldInfo& field, const std::string& where,
                       const std::string& owner)
            {
                const auto reject = [&](std::string_view why = {}) {
                    Report(Ser::DiagnosticSeverity::Error, &node,
                           std::format("`{}` expected {}, found {}{}; `{}` keeps its default", where, Expected(type),
                                       Found(&node), why, owner));
                    return false;
                };
                const Ser::ValueKind kind = node.Kind();
                if (const std::optional<IntegerRange> range = IntegerRangeOf(type.kind))
                {
                    if (kind != Ser::ValueKind::Number)
                    {
                        return reject();
                    }
                    const Ser::Number& number = node.AsNumber();
                    // Held in its kind's width, as ToTree writes it, so a tree reads the same from text or bytes.
                    const bool isSigned = range->min < 0;
                    switch (number.Width())
                    {
                    case Ser::NumberWidth::Signed: {
                        const std::int64_t value = number.AsSigned();
                        if (value < range->min || (value > 0 && static_cast<std::uint64_t>(value) > range->max))
                        {
                            return reject(", which is out of its range");
                        }
                        if (!isSigned)
                        {
                            Renumber(node, Ser::Number(static_cast<std::uint64_t>(value)));
                        }
                        return true;
                    }
                    case Ser::NumberWidth::Unsigned: {
                        const std::uint64_t value = number.AsUnsigned();
                        if (value > range->max)
                        {
                            return reject(", which is out of its range");
                        }
                        if (isSigned)
                        {
                            Renumber(node, Ser::Number(static_cast<std::int64_t>(value)));
                        }
                        return true;
                    }
                    default:
                        return reject(", which is not an integer");
                    }
                }
                switch (type.kind)
                {
                case Kind::F32:
                case Kind::F64:
                    if (kind != Ser::ValueKind::Number)
                    {
                        return reject();
                    }
                    {
                        const Ser::Number& number = node.AsNumber();
                        double value = 0.0;
                        switch (number.Width())
                        {
                        case Ser::NumberWidth::Signed:
                            value = static_cast<double>(number.AsSigned());
                            break;
                        case Ser::NumberWidth::Unsigned:
                            value = static_cast<double>(number.AsUnsigned());
                            break;
                        case Ser::NumberWidth::Single:
                            value = number.AsSingle();
                            break;
                        case Ser::NumberWidth::Double:
                            value = number.AsDouble();
                            break;
                        }
                        if (type.kind == Kind::F64)
                        {
                            Renumber(node, Ser::Number(value));
                            return true;
                        }
                        if (std::isfinite(value) && std::abs(value) > std::numeric_limits<float>::max())
                        {
                            return reject(", which is out of its range");
                        }
                        Renumber(node, Ser::Number(static_cast<float>(value)));
                        return true;
                    }
                case Kind::Bool:
                    return kind == Ser::ValueKind::Bool || reject();
                case Kind::String:
                case Kind::FixedString:
                case Kind::Path:
                    return kind == Ser::ValueKind::String || reject();
                case Kind::Enum:
                    return IsEnumerator(node, type) || reject();
                case Kind::Reference:
                    return CheckReference(node, type, field) || reject();
                case Kind::Entity:
                    return reject(", but a data asset holds no entity");
                case Kind::Pointer:
                    return reject(", but a pointer is never written to a file");
                case Kind::List:
                case Kind::Array:
                    if (kind != Ser::ValueKind::Array || type.element == nullptr ||
                        (type.kind == Kind::Array && node.AsArray().elements.size() != type.count))
                    {
                        return reject();
                    }
                    for (std::size_t i = 0; i < node.AsArray().elements.size(); ++i)
                    {
                        if (!Check(node.AsArray().elements[i].value, *type.element, field,
                                   std::format("{}[{}]", where, i), owner))
                        {
                            return false;
                        }
                    }
                    return true;
                case Kind::Optional:
                    if (kind == Ser::ValueKind::Null)
                    {
                        return true;
                    }
                    return type.element != nullptr ? Check(node, *type.element, field, where, owner) : reject();
                case Kind::Struct:
                    return CheckStruct(node, type, where) || reject();
                default:
                    return reject();
                }
            }

            // Gives `node` the number `number`, keeping its location.
            static void Renumber(Ser::Value& node, Ser::Number number)
            {
                const std::optional<Ser::Location> location = node.location;
                node = Ser::Value(number);
                node.location = location;
            }

            // An enumerator's name of the enum, or a whole number, as ToTree writes a value no enumerator has.
            static bool IsEnumerator(const Ser::Value& node, const ValueType& type)
            {
                if (node.Kind() == Ser::ValueKind::Number)
                {
                    const Ser::NumberWidth width = node.AsNumber().Width();
                    return width == Ser::NumberWidth::Signed || width == Ser::NumberWidth::Unsigned;
                }
                return node.Kind() == Ser::ValueKind::String && type.type != nullptr &&
                       std::ranges::find(type.type->enumerators, std::string_view(node.AsString()),
                                         &Enumerator::name) != type.type->enumerators.end();
            }

            // A path goes through the store's reader, which loads its target (ADR-0076 §3); one naming none is said
            // where it is and becomes `null`, the empty handle, as FromTree leaves such a field.
            bool CheckReference(Ser::Value& node, const ValueType& type, const FieldInfo& field)
            {
                if (node.Kind() == Ser::ValueKind::Null)
                {
                    return true;
                }
                if (node.Kind() != Ser::ValueKind::String)
                {
                    return false;
                }
                FieldInfo asked = field;
                asked.type = type;
                (void)m_resolvers.readReference(node.AsString(), asked);
                if (m_reading.emptied.empty())
                {
                    return true;
                }
                for (EmptiedReference& emptied : m_reading.emptied)
                {
                    Record(LocatedAt(Ser::DiagnosticSeverity::Error, m_path, &node, m_root, std::move(emptied.message)),
                           m_logger, m_diagnostics);
                }
                m_reading.emptied.clear();
                const std::optional<Ser::Location> location = node.location;
                node = Ser::Value(nullptr);
                node.location = location;
                return true;
            }

            // A nested tree-backed struct is checked member by member. A nested C++ struct is read through FromTree
            // and written back from what read, its paths as they were given, so what did not read takes its default.
            bool CheckStruct(Ser::Value& node, const ValueType& type, const std::string& where)
            {
                const TypeDescription* described = type.type;
                if (described == nullptr)
                {
                    return false;
                }
                if (described->treeBacked)
                {
                    if (node.Kind() != Ser::ValueKind::Object)
                    {
                        return false;
                    }
                    const std::vector<const FieldInfo*> fields = TreeFieldsOf(*described, m_registry);
                    CheckObject(node.AsObject(), fields, where);
                    return true;
                }
                DataValue scratch(*described);
                std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> paths;
                Ser::Resolvers resolvers;
                resolvers.readReference = [&](std::string_view path, const FieldInfo& field) {
                    std::optional<ReferenceValue> named = m_resolvers.readReference(path, field);
                    if (named && named->generation != 0)
                    {
                        paths.insert_or_assign({named->index, named->generation}, std::string(path));
                    }
                    return named;
                };
                resolvers.writeReference = [&](ReferenceValue reference, const FieldInfo&) {
                    const auto found = paths.find({reference.index, reference.generation});
                    return found != paths.end() ? found->second : std::string();
                };
                Ser::BridgeResult bridged =
                    Ser::FromTree(node, *described, m_registry, resolvers, scratch.value, m_path, m_logger);
                Append(m_diagnostics, std::move(bridged.diagnostics));
                ReportEmptied(m_reading, node, m_path, m_logger, m_diagnostics);
                if (bridged.status == Ser::BridgeStatus::UnknownType || bridged.status == Ser::BridgeStatus::TreeBacked)
                {
                    return false;
                }
                Ser::Value written = Ser::ToTree(scratch.value, *described, m_registry, resolvers, &node);
                written.location = node.location;
                node = std::move(written);
                return true;
            }

            void Report(Ser::DiagnosticSeverity severity, const Ser::Value* node, std::string message)
            {
                Record(LocatedAt(severity, m_path, node, m_root, m_where + std::move(message)), m_logger,
                       m_diagnostics);
            }

            const Ser::Value& m_root;
            const std::string& m_where;
            std::string_view m_path;
            const TypeRegistry& m_registry;
            const Ser::Resolvers& m_resolvers;
            ReferenceReading& m_reading;
            ILogger& m_logger;
            std::vector<Ser::Diagnostic>& m_diagnostics;
        };

        // Gives `node` the comments of `prior`, the node at its key path in the tree last read or written, as ToTree
        // does: a member's by key, an element's by index, and a container's closing comments; with no `prior`, none.
        void TakeTrivia(Ser::Value& node, const Ser::Value* prior)
        {
            if (node.Kind() == Ser::ValueKind::Object)
            {
                Ser::Object& object = node.AsObject();
                const Ser::Object* before =
                    prior != nullptr && prior->Kind() == Ser::ValueKind::Object ? &prior->AsObject() : nullptr;
                object.closingComments = before != nullptr ? before->closingComments : std::vector<std::string>();
                for (Ser::Member& member : object.members)
                {
                    const Ser::Member* match = nullptr;
                    if (before != nullptr)
                    {
                        const auto found = std::ranges::find(before->members, member.key, &Ser::Member::key);
                        match = found != before->members.end() ? &*found : nullptr;
                    }
                    member.leadingComments = match != nullptr ? match->leadingComments : std::vector<std::string>();
                    member.trailingComment = match != nullptr ? match->trailingComment : std::nullopt;
                    TakeTrivia(member.value, match != nullptr ? &match->value : nullptr);
                }
            }
            else if (node.Kind() == Ser::ValueKind::Array)
            {
                Ser::Array& array = node.AsArray();
                const Ser::Array* before =
                    prior != nullptr && prior->Kind() == Ser::ValueKind::Array ? &prior->AsArray() : nullptr;
                array.closingComments = before != nullptr ? before->closingComments : std::vector<std::string>();
                for (std::size_t i = 0; i < array.elements.size(); ++i)
                {
                    Ser::Element& element = array.elements[i];
                    const Ser::Element* match =
                        before != nullptr && i < before->elements.size() ? &before->elements[i] : nullptr;
                    element.leadingComments = match != nullptr ? match->leadingComments : std::vector<std::string>();
                    element.trailingComment = match != nullptr ? match->trailingComment : std::nullopt;
                    TakeTrivia(element.value, match != nullptr ? &match->value : nullptr);
                }
            }
        }
    } // namespace

    const TypeDescription* LayoutOf(const TypeDescription& type, const TypeRegistry& registry)
    {
        // Register refuses a base chain that does not end, so this walk ends.
        const TypeDescription* at = &type;
        while (at != nullptr && at->treeBacked)
        {
            at = BaseOf(*at, registry);
        }
        return at;
    }

    std::vector<const FieldInfo*> TreeFieldsOf(const TypeDescription& type, const TypeRegistry& registry)
    {
        std::vector<const TypeDescription*> chain;
        for (const TypeDescription* at = &type; at != nullptr && at->treeBacked; at = BaseOf(*at, registry))
        {
            chain.push_back(at);
        }
        std::vector<const FieldInfo*> fields;
        for (auto at = chain.rbegin(); at != chain.rend(); ++at)
        {
            for (const FieldInfo& field : (*at)->fields)
            {
                fields.push_back(&field);
            }
        }
        return fields;
    }

    DataRead ReadDataValue(Ser::Value& data, const TypeDescription& type, const std::string& where,
                           std::string_view path, const TypeRegistry& registry,
                           const std::function<ReferenceRead(std::string_view, const FieldInfo&)>& read,
                           ILogger& logger, std::vector<Ser::Diagnostic>& diagnostics)
    {
        auto value = std::make_unique<DataValue>(type, LayoutOf(type, registry));
        // No entity callbacks are given: a data asset holds no entity (ADR-0075).
        ReferenceReading reading;
        Ser::Resolvers checked;
        checked.readReference = CheckedReferenceReader(read, where, reading);
        bool incomplete = false;

        if (!type.treeBacked)
        {
            Ser::BridgeResult bridged = Ser::FromTree(data, type, registry, checked, value->value, path, logger);
            Append(diagnostics, std::move(bridged.diagnostics));
            ReportEmptied(reading, data, path, logger, diagnostics);
            incomplete = bridged.status == Ser::BridgeStatus::Partial;
        }
        else if (data.Kind() != Ser::ValueKind::Object)
        {
            Record(LocatedAt(Ser::DiagnosticSeverity::Error, path, &data, data,
                             std::format("{}expected an object of `{}`'s fields, found {}; every field keeps its "
                                         "default",
                                         where, type.name, Found(&data))),
                   logger, diagnostics);
        }
        else
        {
            // The caller's tree is kept as the file's, for the comments a save keeps, so it is read from a copy: the
            // members the C++ base names go to it, the rest stay the tree's own.
            Ser::Value base{Ser::Object{}};
            Ser::Value own{Ser::Object{}};
            own.location = data.location;
            for (const Ser::Member& member : data.AsObject().members)
            {
                (LayoutNames(value->layout, registry, member.key) ? base : own).AsObject().members.push_back(member);
            }
            if (value->layout != nullptr)
            {
                base.location = data.location;
                Ser::BridgeResult bridged =
                    Ser::FromTree(base, *value->layout, registry, checked, value->value, path, logger);
                Append(diagnostics, std::move(bridged.diagnostics));
                ReportEmptied(reading, base, path, logger, diagnostics);
                incomplete = bridged.status == Ser::BridgeStatus::Partial;
            }
            const std::vector<const FieldInfo*> fields = TreeFieldsOf(type, registry);
            TreeChecker checker(own, where, path, registry, checked, reading, logger, diagnostics);
            checker.CheckObject(own.AsObject(), fields, {});
            value->tree = std::move(own);
        }
        incomplete = incomplete || reading.unresolved;
        return {AssetBox(value.release(), &DestroyAssetData<DataValue>), incomplete, std::move(reading.named)};
    }

    Ser::Value ComposeDataTree(const DataValue& value, const TypeRegistry& registry, const Ser::Resolvers& resolvers,
                               const Ser::Value* prior)
    {
        if (!value.type->treeBacked)
        {
            return Ser::ToTree(value.value, *value.type, registry, resolvers, prior);
        }
        // The C++ base's members first, as a visit pushes a base's fields first, then the tree's own.
        Ser::Value whole{Ser::Object{}};
        if (value.layout != nullptr)
        {
            whole = Ser::ToTree(value.value, *value.layout, registry, resolvers, prior);
        }
        Ser::Value own = *value.tree;
        TakeTrivia(own, prior);
        Ser::Object& object = whole.AsObject();
        for (Ser::Member& member : own.AsObject().members)
        {
            object.members.push_back(std::move(member));
        }
        object.closingComments = own.AsObject().closingComments;
        return whole;
    }

    void AssignInPlace(DataValue& into, DataValue& from, const TypeRegistry& registry, ILogger& logger)
    {
        assert(into.type == from.type && into.layout == from.layout && "a value in place keeps the asset's type");
        if (into.layout != nullptr)
        {
            // A description has no assignment of its own, so the instance crosses as a tree, each reference as its
            // slot rather than a path, which no file ever sees.
            Ser::Resolvers slots;
            slots.writeReference = [](ReferenceValue reference, const FieldInfo&) {
                return std::format("{}:{}", reference.index, reference.generation);
            };
            slots.readReference = [](std::string_view text, const FieldInfo&) -> std::optional<ReferenceValue> {
                ReferenceValue reference;
                const std::size_t colon = text.find(':');
                const char* end = text.data() + text.size();
                if (colon == std::string_view::npos ||
                    std::from_chars(text.data(), text.data() + colon, reference.index).ec != std::errc() ||
                    std::from_chars(text.data() + colon + 1, end, reference.generation).ec != std::errc())
                {
                    return std::nullopt;
                }
                return reference;
            };
            const Ser::Value tree = Ser::ToTree(from.value, *from.layout, registry, slots);
            into.layout->Destroy(into.value);
            into.layout->Construct(into.value);
            [[maybe_unused]] const Ser::BridgeResult copied =
                Ser::FromTree(tree, *into.layout, registry, slots, into.value, "", logger);
            assert(copied.status == Ser::BridgeStatus::Succeeded && copied.diagnostics.empty() &&
                   "a value's own tree reads back whole");
        }
        if (from.tree)
        {
            into.tree = std::move(from.tree);
        }
    }
} // namespace Fenrir::Detail
