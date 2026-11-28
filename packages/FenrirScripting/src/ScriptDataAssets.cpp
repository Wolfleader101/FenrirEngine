#include "ScriptDataAssets.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Fenrir::Detail
{
    namespace
    {
        using Result = Iris::CallResult<void>;
        using Serialisation::Value;
        using Serialisation::ValueKind;

        Result Succeeded()
        {
            return Result{Iris::CallStatus::Success, {}, false};
        }

        Result Failed(std::string diagnostic)
        {
            return Result{Iris::CallStatus::InvalidArgument, std::move(diagnostic), false};
        }

        // Where an entry's bytes are decoded: a bad entry is a refusal with its own diagnostic, so nothing is logged.
        class QuietLogger final : public ILogger
        {
          public:
            void AddLogger(std::shared_ptr<ILogger>) override
            {
            }
            void RemoveLogger(std::shared_ptr<ILogger>) override
            {
            }

          protected:
            void LogImpl(std::string_view) override
            {
            }
            void InfoImpl(std::string_view) override
            {
            }
            void WarnImpl(std::string_view) override
            {
            }
            void ErrorImpl(std::string_view) override
            {
            }
            void FatalImpl(std::string_view) override
            {
            }
        };

        const Value* MemberOf(const Value& object, std::string_view key)
        {
            return object.Kind() == ValueKind::Object ? object.AsObject().Find(key) : nullptr;
        }

        const std::string* StringOf(const Value* value)
        {
            return value != nullptr && value->Kind() == ValueKind::String ? &value->AsString() : nullptr;
        }

        std::optional<bool> BoolOf(const Value* value)
        {
            return value != nullptr && value->Kind() == ValueKind::Bool ? std::optional<bool>(value->AsBool())
                                                                        : std::nullopt;
        }

        std::optional<std::int64_t> IntegerOf(const Value* value)
        {
            if (value == nullptr || value->Kind() != ValueKind::Number)
            {
                return std::nullopt;
            }
            const auto& number = value->AsNumber();
            switch (number.Width())
            {
            case Serialisation::NumberWidth::Signed:
                return number.AsSigned();
            case Serialisation::NumberWidth::Unsigned:
                return number.AsUnsigned() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                           ? std::optional<std::int64_t>(static_cast<std::int64_t>(number.AsUnsigned()))
                           : std::nullopt;
            default:
                return std::nullopt;
            }
        }

        std::optional<double> NumberOf(const Value* value)
        {
            if (value == nullptr || value->Kind() != ValueKind::Number)
            {
                return std::nullopt;
            }
            const auto& number = value->AsNumber();
            switch (number.Width())
            {
            case Serialisation::NumberWidth::Signed:
                return static_cast<double>(number.AsSigned());
            case Serialisation::NumberWidth::Unsigned:
                return static_cast<double>(number.AsUnsigned());
            case Serialisation::NumberWidth::Single:
                return static_cast<double>(number.AsSingle());
            default:
                return number.AsDouble();
            }
        }

        // The array at `key`, or null when it is absent or not an array.
        const Serialisation::Array* ArrayAt(const Value& object, std::string_view key)
        {
            const Value* value = MemberOf(object, key);
            return value != nullptr && value->Kind() == ValueKind::Array ? &value->AsArray() : nullptr;
        }

        bool IsInteger(Kind kind)
        {
            return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32 || kind == Kind::I64 ||
                   kind == Kind::U8 || kind == Kind::U16 || kind == Kind::U32 || kind == Kind::U64;
        }

        // A description's construct and destroy for an enum of each underlying width, which a script enum is.
        struct EnumOps
        {
            std::size_t size;
            void (*construct)(void*);
            void (*destroy)(void*);
        };

        template <typename T>
        constexpr EnumOps EnumOpsOf()
        {
            return {sizeof(T), &ConstructValue<T>, &DestroyValue<T>};
        }

        EnumOps EnumOpsFor(Kind underlying)
        {
            switch (underlying)
            {
            case Kind::I8:
                return EnumOpsOf<std::int8_t>();
            case Kind::I16:
                return EnumOpsOf<std::int16_t>();
            case Kind::I64:
                return EnumOpsOf<std::int64_t>();
            case Kind::U8:
                return EnumOpsOf<std::uint8_t>();
            case Kind::U16:
                return EnumOpsOf<std::uint16_t>();
            case Kind::U32:
                return EnumOpsOf<std::uint32_t>();
            case Kind::U64:
                return EnumOpsOf<std::uint64_t>();
            default:
                return EnumOpsOf<std::int32_t>();
            }
        }
    } // namespace

    bool DataAssetStore::IsAssetKind(std::string_view name) const
    {
        return std::find(assetKinds.begin(), assetKinds.end(), name) != assetKinds.end();
    }

    std::optional<std::string> DataAssetStore::Held(App& app, std::string_view name) const
    {
        for (const auto& check : typeHeld)
        {
            if (auto reason = check(app, name))
            {
                return reason;
            }
        }
        return std::nullopt;
    }

    struct ScriptDataAssets::Entry
    {
        enum class What
        {
            Enum,
            Struct,
            DataAsset,
        };

        What what = What::DataAsset;
        std::string name;
        TypeDescription description;
        // What the description's views point into, none of which moves once made.
        std::deque<std::string> strings;
        std::deque<std::vector<std::string_view>> nameLists;
        std::deque<ValueType> elements;
        std::vector<FieldInfo> fields;
        std::vector<Enumerator> enumerators;
        std::vector<std::byte> defaultTree;
        // The versions its upgraders step from.
        std::vector<std::uint32_t> upgraders;
        // The struct and enum types its fields hold, resolved when it registers: one of this arena's, or the
        // registry's type of that name.
        struct Pending
        {
            ValueType* type = nullptr;
            std::string_view target;
            Entry* ours = nullptr;
        };
        std::vector<Pending> pending;
        // This arena's entries it extends or holds, which must stay while it does.
        std::vector<const Entry*> needs;
        bool registered = false;
        bool upgradersRegistered = false;

        std::string_view Intern(std::string text)
        {
            return strings.emplace_back(std::move(text));
        }

        std::span<const std::string_view> Names(const Serialisation::Array* names)
        {
            auto& list = nameLists.emplace_back();
            if (names != nullptr)
            {
                for (const auto& element : names->elements)
                {
                    if (const std::string* text = StringOf(&element.value))
                    {
                        list.push_back(Intern(*text));
                    }
                }
            }
            return list;
        }
    };

    namespace
    {
        using Entry = ScriptDataAssets::Entry;

        // What a field's type may name while one report is built: the entries made so far and those still to be,
        // the rest of the report, the registry and the store.
        struct Context
        {
            const TypeRegistry& types;
            const DataAssetStore& store;
            // The report's data asset types, structs and enums by name; a struct or enum entry is made before any
            // field names it.
            const std::unordered_set<std::string>& assets;
            const std::unordered_map<std::string, Entry*>& structs;
            const std::unordered_map<std::string, Entry*>& enums;
            // The structs and components the report's components hold or are, which ScriptTypes describes.
            const std::unordered_set<std::string>& componentTypes;
        };

        // Reads the type at `node` into `type`, for the field `where` names.
        Result ReadType(Entry& owner, const Value* node, const Context& context, const std::string& where,
                        ValueType& type)
        {
            const std::string* kindName = node != nullptr ? StringOf(MemberOf(*node, "kind")) : nullptr;
            if (kindName == nullptr)
            {
                return Failed(where + " has no kind.");
            }
            const std::optional<Kind> kind = KindNamed(*kindName);
            if (!kind)
            {
                return Failed(where + " is of the kind '" + *kindName + "', which the engine does not know.");
            }
            type.kind = *kind;
            const std::string* target = StringOf(MemberOf(*node, "target"));
            const auto element = [&]() -> Result {
                const Value* inner = MemberOf(*node, "element");
                if (inner == nullptr)
                {
                    return Failed(where + " is a " + std::string(KindName(*kind)) + " with no element type.");
                }
                ValueType& made = owner.elements.emplace_back();
                if (auto read = ReadType(owner, inner, context, where, made); !read.Succeeded())
                {
                    return read;
                }
                if (*kind == Kind::Optional && made.kind == Kind::Optional)
                {
                    return Failed(where + " is an optional of an optional, which the engine cannot describe.");
                }
                type.element = &made;
                return Succeeded();
            };
            switch (*kind)
            {
            case Kind::I8:
            case Kind::I16:
            case Kind::I32:
            case Kind::I64:
            case Kind::U8:
            case Kind::U16:
            case Kind::U32:
            case Kind::U64:
            case Kind::F32:
            case Kind::F64:
            case Kind::Bool:
            case Kind::Char16:
            case Kind::String:
            case Kind::Path:
                return Succeeded();
            case Kind::List:
            case Kind::Optional:
                return element();
            case Kind::Enum: {
                if (target == nullptr)
                {
                    return Failed(where + " is an enum with no target.");
                }
                Entry* ours = nullptr;
                if (const auto found = context.enums.find(*target); found != context.enums.end())
                {
                    ours = found->second;
                }
                else if (const TypeDescription* registered = context.types.Find(*target);
                         registered == nullptr || registered->role != Role::Enum)
                {
                    return Failed(where + " holds the enum '" + *target +
                                  "', which neither the script nor the engine describes.");
                }
                const std::string_view name = owner.Intern(*target);
                owner.pending.push_back({&type, name, ours});
                return Succeeded();
            }
            case Kind::Struct: {
                if (target == nullptr)
                {
                    return Failed(where + " is a struct with no target.");
                }
                Entry* ours = nullptr;
                if (const auto found = context.structs.find(*target); found != context.structs.end())
                {
                    ours = found->second;
                }
                else if (context.assets.contains(*target))
                {
                    return Failed(where + " holds the data asset type '" + *target +
                                  "' by value; a data asset names another through a reference (ADR-0075).");
                }
                else if (!context.componentTypes.contains(*target))
                {
                    const TypeDescription* registered = context.types.Find(*target);
                    if (registered == nullptr || registered->role == Role::Enum)
                    {
                        return Failed(where + " holds the struct '" + *target +
                                      "', which neither the script nor the engine describes.");
                    }
                    if (registered->role == Role::DataAsset)
                    {
                        return Failed(where + " holds the data asset type '" + *target +
                                      "' by value; a data asset names another through a reference (ADR-0075).");
                    }
                }
                const std::string_view name = owner.Intern(*target);
                owner.pending.push_back({&type, name, ours});
                return Succeeded();
            }
            case Kind::Reference: {
                if (target == nullptr)
                {
                    return Failed(where + " is a reference with no target.");
                }
                const TypeDescription* registered = context.types.Find(*target);
                if (!context.assets.contains(*target) && !context.store.IsAssetKind(*target) &&
                    (registered == nullptr || registered->role != Role::DataAsset))
                {
                    return Failed(where + " refers to '" + *target +
                                  "', which is neither a data asset type the engine or the script has nor an asset "
                                  "kind.");
                }
                type.target = owner.Intern(*target);
                return Succeeded();
            }
            case Kind::Entity:
                return Failed(where + " holds an entity, which a data asset never does (ADR-0075).");
            default:
                return Failed(where + " is of the kind " + std::string(KindName(*kind)) +
                              ", which a C# data asset type cannot hold.");
            }
        }

        // Reads the fields at `fields` into `owner`, whose kind of type `noun` names.
        Result ReadFields(Entry& owner, const Serialisation::Array* fields, const Context& context, const char* noun)
        {
            if (fields == nullptr)
            {
                return Failed("The script's " + std::string(noun) + " '" + owner.name + "' has no fields list.");
            }
            owner.fields.reserve(fields->elements.size());
            for (const auto& element : fields->elements)
            {
                const Value& field = element.value;
                const std::string* name = StringOf(MemberOf(field, "name"));
                if (name == nullptr || name->empty())
                {
                    return Failed("A field of the script's " + std::string(noun) + " '" + owner.name +
                                  "' has no name.");
                }
                const std::string where =
                    "The field '" + *name + "' of the script's " + std::string(noun) + " '" + owner.name + "'";
                // Made in place: the pending targets point at its type, and `fields` was reserved, so never moves.
                FieldInfo& info = owner.fields.emplace_back();
                info.name = owner.Intern(*name);
                if (auto read = ReadType(owner, MemberOf(field, "type"), context, where, info.type); !read.Succeeded())
                {
                    return read;
                }
                info.formerly = owner.Names(ArrayAt(field, "formerly"));
                if (const auto* range = ArrayAt(field, "range"))
                {
                    const auto min = range->elements.size() == 2 ? NumberOf(&range->elements[0].value) : std::nullopt;
                    const auto max = range->elements.size() == 2 ? NumberOf(&range->elements[1].value) : std::nullopt;
                    if (!min || !max)
                    {
                        return Failed(where + " has a range that is not two numbers.");
                    }
                    info.metadata.range = FieldRange{*min, *max};
                }
                info.metadata.step = NumberOf(MemberOf(field, "step"));
                if (const std::string* display = StringOf(MemberOf(field, "display")))
                {
                    info.metadata.display = owner.Intern(*display);
                }
                if (const std::string* group = StringOf(MemberOf(field, "group")))
                {
                    info.metadata.group = owner.Intern(*group);
                }
                if (const std::string* units = StringOf(MemberOf(field, "units")))
                {
                    info.metadata.units = owner.Intern(*units);
                }
                info.metadata.readonly = BoolOf(MemberOf(field, "readonly")).value_or(false);
                info.metadata.hidden = BoolOf(MemberOf(field, "hidden")).value_or(false);
                info.metadata.multiline = BoolOf(MemberOf(field, "multiline")).value_or(false);
            }
            return Succeeded();
        }

        // Why `name` cannot be a new script type's: a registered type has it. Nothing when it is free.
        std::optional<std::string> TakenInRegistry(const TypeRegistry& types, const std::string& name,
                                                   const std::string& what)
        {
            const TypeDescription* held = types.Find(name);
            if (held == nullptr)
            {
                return std::nullopt;
            }
            if (held->package == "FenrirScripting")
            {
                return "The script defines " + what + " '" + name +
                       "', but the engine still has a type of that name from an earlier script generation, kept while "
                       "live data holds it.";
            }
            return "The script defines " + what + " '" + name +
                   "', but the engine already has a type of that name, from " + std::string(held->package) + ".";
        }
    } // namespace

    ScriptDataAssets::ScriptDataAssets() = default;

    ScriptDataAssets::~ScriptDataAssets() = default;

    Result ScriptDataAssets::Build(const ScriptReport& report, const TypeRegistry& types, const DataAssetStore& store)
    {
        m_entries.clear();
        if (report.dataAssets.empty())
        {
            return Succeeded();
        }

        // Every entry as a tree.
        QuietLogger quiet;
        std::vector<Value> trees;
        trees.reserve(report.dataAssets.size());
        for (std::size_t at = 0; at < report.dataAssets.size(); ++at)
        {
            auto decoded = Serialisation::DecodeTree(report.dataAssets[at], "data asset entry", quiet);
            if (!decoded.tree || decoded.tree->Kind() != ValueKind::Object)
            {
                return Failed(
                    "The script's data asset entry " + std::to_string(at + 1) + " is not a description entry" +
                    (decoded.diagnostics.empty() ? std::string(".") : ": " + decoded.diagnostics.front().message));
            }
            trees.push_back(std::move(*decoded.tree));
        }

        // The names the rest of the report holds, and those of the report's components and the structs they hold,
        // which ScriptTypes describes and a data asset's field may hold.
        std::unordered_set<std::string> componentTypes;
        for (const auto* reported : {&report.components, &report.structs})
        {
            for (const auto& description : *reported)
            {
                componentTypes.insert(description.name);
            }
        }
        std::unordered_set<std::string> eventNames;
        for (const auto& description : report.events)
        {
            eventNames.insert(description.name);
        }

        // The data asset types' names, each once, none another type's.
        std::unordered_set<std::string> assets;
        std::vector<std::string> names;
        for (const Value& tree : trees)
        {
            const std::string* name = StringOf(MemberOf(tree, "name"));
            if (name == nullptr || name->empty())
            {
                return Failed("The script reported a data asset type with no name.");
            }
            if (!assets.insert(*name).second)
            {
                return Failed("The script reported the data asset type '" + *name + "' more than once.");
            }
            if (componentTypes.contains(*name) || eventNames.contains(*name))
            {
                return Failed("The script reported the data asset type '" + *name +
                              "', but also a component, struct or event of that name.");
            }
            if (auto taken = TakenInRegistry(types, *name, "a data asset type"))
            {
                return Failed(*taken);
            }
            for (const auto& former : [&] {
                     std::vector<std::string> list;
                     if (const auto* formerly = ArrayAt(tree, "formerly"))
                     {
                         for (const auto& element : formerly->elements)
                         {
                             if (const std::string* text = StringOf(&element.value))
                             {
                                 list.push_back(*text);
                             }
                         }
                     }
                     return list;
                 }())
            {
                if (auto taken = TakenInRegistry(types, former, "a data asset type formerly named"))
                {
                    return Failed(*taken + " ('" + *name + "')");
                }
            }
            names.push_back(*name);
        }

        // The structs and enums the entries describe: each once, however many entries hold it, the same every time.
        std::unordered_map<std::string, const Value*> structTrees;
        std::unordered_map<std::string, const Value*> enumTrees;
        std::vector<std::string> structOrder;
        std::vector<std::string> enumOrder;
        for (std::size_t at = 0; at < trees.size(); ++at)
        {
            const auto collect = [&](const char* key, const char* noun,
                                     std::unordered_map<std::string, const Value*>& by,
                                     std::vector<std::string>& order) -> Result {
                const auto* list = ArrayAt(trees[at], key);
                if (list == nullptr)
                {
                    return Failed("The script's data asset type '" + names[at] + "' has no " + key + " list.");
                }
                for (const auto& element : list->elements)
                {
                    const std::string* name = StringOf(MemberOf(element.value, "name"));
                    if (name == nullptr || name->empty())
                    {
                        return Failed("The script's data asset type '" + names[at] + "' holds a " + noun +
                                      " with no name.");
                    }
                    const auto [found, added] = by.emplace(*name, &element.value);
                    if (!added && !Serialisation::Equals(*found->second, element.value))
                    {
                        return Failed("The script describes the " + std::string(noun) + " '" + *name +
                                      "' two different ways.");
                    }
                    if (added)
                    {
                        order.push_back(*name);
                    }
                }
                return Succeeded();
            };
            if (auto collected = collect("structs", "struct", structTrees, structOrder); !collected.Succeeded())
            {
                return collected;
            }
            if (auto collected = collect("enums", "enum", enumTrees, enumOrder); !collected.Succeeded())
            {
                return collected;
            }
        }

        std::vector<std::unique_ptr<Entry>> enums;
        std::vector<std::unique_ptr<Entry>> structs;
        std::vector<std::unique_ptr<Entry>> dataAssets;
        std::unordered_map<std::string, Entry*> structsByName;
        std::unordered_map<std::string, Entry*> enumsByName;

        for (const auto& name : enumOrder)
        {
            if (assets.contains(name) || structTrees.contains(name) || componentTypes.contains(name) ||
                eventNames.contains(name))
            {
                return Failed("The script describes the enum '" + name + "', but also another type of that name.");
            }
            if (auto taken = TakenInRegistry(types, name, "an enum"))
            {
                return Failed(*taken);
            }
            const Value& tree = *enumTrees.at(name);
            auto entry = std::make_unique<Entry>();
            entry->what = Entry::What::Enum;
            entry->name = name;
            const std::string* underlyingName = StringOf(MemberOf(tree, "kind"));
            const std::optional<Kind> underlying =
                underlyingName != nullptr ? KindNamed(*underlyingName) : std::nullopt;
            if (!underlying || !IsInteger(*underlying))
            {
                return Failed("The script's enum '" + name + "' has no integer kind.");
            }
            const auto* enumerators = ArrayAt(tree, "enumerators");
            if (enumerators == nullptr)
            {
                return Failed("The script's enum '" + name + "' has no enumerators list.");
            }
            for (const auto& element : enumerators->elements)
            {
                const std::string* enumerator = StringOf(MemberOf(element.value, "name"));
                const std::optional<std::int64_t> value = IntegerOf(MemberOf(element.value, "value"));
                if (enumerator == nullptr || enumerator->empty() || !value)
                {
                    return Failed("An enumerator of the script's enum '" + name + "' has no name or value.");
                }
                entry->enumerators.push_back({entry->Intern(*enumerator), *value, {}});
            }
            const EnumOps ops = EnumOpsFor(*underlying);
            TypeDescription& description = entry->description;
            description.name = entry->Intern(name);
            description.package = "FenrirScripting";
            description.role = Role::Enum;
            description.size = ops.size;
            description.alignment = ops.size;
            description.construct = ops.construct;
            description.destroy = ops.destroy;
            description.enumerators = entry->enumerators;
            description.underlying = *underlying;
            enumsByName.emplace(name, entry.get());
            enums.push_back(std::move(entry));
        }

        // A struct the report's components hold too is theirs: ScriptTypes describes it, and a field names that one.
        for (const auto& name : structOrder)
        {
            if (componentTypes.contains(name))
            {
                continue;
            }
            if (assets.contains(name) || eventNames.contains(name))
            {
                return Failed("The script describes the struct '" + name + "', but also another type of that name.");
            }
            if (auto taken = TakenInRegistry(types, name, "a struct"))
            {
                return Failed(*taken);
            }
            auto entry = std::make_unique<Entry>();
            entry->what = Entry::What::Struct;
            entry->name = name;
            structsByName.emplace(name, entry.get());
            structs.push_back(std::move(entry));
        }

        const Context context{types, store, assets, structsByName, enumsByName, componentTypes};
        // A struct's own default is its holders' to give: each data asset's default holds the struct's whole value.
        const std::vector<std::byte> emptyObject =
            Serialisation::EncodeTree(Value(Serialisation::Object{}), Serialisation::Trivia::Drop);
        for (auto& entry : structs)
        {
            if (auto read = ReadFields(*entry, ArrayAt(*structTrees.at(entry->name), "fields"), context, "struct");
                !read.Succeeded())
            {
                return read;
            }
            entry->defaultTree = emptyObject;
            TypeDescription& description = entry->description;
            description.name = entry->Intern(entry->name);
            description.package = "FenrirScripting";
            description.role = Role::Nested;
            description.fields = entry->fields;
            description.treeBacked = true;
            description.defaultTree = entry->defaultTree;
        }

        std::unordered_map<std::string, Entry*> earlier;
        for (std::size_t at = 0; at < trees.size(); ++at)
        {
            const Value& tree = trees[at];
            auto entry = std::make_unique<Entry>();
            entry->what = Entry::What::DataAsset;
            entry->name = names[at];
            const std::string noun = "The C# data asset type '" + entry->name + "'";

            const std::optional<std::int64_t> version = IntegerOf(MemberOf(tree, "version"));
            if (!version || *version < 1 || *version > std::numeric_limits<std::uint32_t>::max())
            {
                return Failed(noun + " has no version of 1 or more.");
            }

            const Value* base = MemberOf(tree, "base");
            std::string_view baseName;
            if (base != nullptr && base->Kind() != ValueKind::Null)
            {
                const std::string* named = StringOf(base);
                if (named == nullptr || named->empty())
                {
                    return Failed(noun + " has a base that is not a name.");
                }
                if (const auto found = earlier.find(*named); found != earlier.end())
                {
                    entry->needs.push_back(found->second);
                }
                else if (const TypeDescription* registered = types.Find(*named);
                         registered == nullptr || registered->role != Role::DataAsset)
                {
                    return Failed(noun + " extends '" + *named +
                                  "', which is not a data asset type the engine has or the script reports before it.");
                }
                baseName = entry->Intern(*named);
            }

            if (auto read = ReadFields(*entry, ArrayAt(tree, "fields"), context, "data asset type"); !read.Succeeded())
            {
                return read;
            }

            const auto* upgraders = ArrayAt(tree, "upgraders");
            if (upgraders == nullptr)
            {
                return Failed(noun + " has no upgraders list.");
            }
            std::string listed;
            bool consecutive = true;
            for (const auto& element : upgraders->elements)
            {
                const std::optional<std::int64_t> from = IntegerOf(&element.value);
                if (!from)
                {
                    return Failed(noun + " has an upgrader whose version is not a number.");
                }
                listed += (listed.empty() ? "" : ", ") + std::to_string(*from);
                consecutive = consecutive && *from >= 1 && *from < *version &&
                              (entry->upgraders.empty() || *from == entry->upgraders.back() + 1);
                entry->upgraders.push_back(static_cast<std::uint32_t>(std::max<std::int64_t>(*from, 0)));
            }
            if (!consecutive || (!entry->upgraders.empty() && entry->upgraders.back() + 1 != *version))
            {
                return Failed(noun + " is at version " + std::to_string(*version) + " with upgraders from [" + listed +
                              "], which are not consecutive below it: they must step one version at a time, with no "
                              "gap, up to version " +
                              std::to_string(*version) + ".");
            }

            const Value* defaults = MemberOf(tree, "default");
            if (defaults == nullptr || defaults->Kind() != ValueKind::Object)
            {
                return Failed(noun + " has no default object.");
            }
            entry->defaultTree = Serialisation::EncodeTree(*defaults, Serialisation::Trivia::Drop);

            for (const auto& pending : entry->pending)
            {
                if (pending.ours != nullptr)
                {
                    entry->needs.push_back(pending.ours);
                }
            }
            TypeDescription& description = entry->description;
            description.name = entry->Intern(entry->name);
            description.package = "FenrirScripting";
            description.role = Role::DataAsset;
            description.version = static_cast<std::uint32_t>(*version);
            description.formerly = entry->Names(ArrayAt(tree, "formerly"));
            description.transient = BoolOf(MemberOf(tree, "transient")).value_or(false);
            description.editorOnly = BoolOf(MemberOf(tree, "editorOnly")).value_or(false);
            description.base = baseName;
            description.fields = entry->fields;
            description.treeBacked = true;
            description.defaultTree = entry->defaultTree;
            earlier.emplace(entry->name, entry.get());
            dataAssets.push_back(std::move(entry));
        }

        // The structs in the order they register: each after the structs of this arena it holds. A struct that holds
        // itself or one holding it, through a list, is placed where the walk meets it first.
        for (auto& entry : structs)
        {
            for (const auto& pending : entry->pending)
            {
                if (pending.ours != nullptr && pending.ours != entry.get())
                {
                    entry->needs.push_back(pending.ours);
                }
            }
        }
        std::vector<std::unique_ptr<Entry>> ordered;
        for (auto& entry : enums)
        {
            ordered.push_back(std::move(entry));
        }
        std::unordered_set<const Entry*> placed;
        std::unordered_map<const Entry*, std::unique_ptr<Entry>*> slots;
        for (auto& entry : structs)
        {
            slots.emplace(entry.get(), &entry);
        }
        const std::function<void(std::unique_ptr<Entry>&)> place = [&](std::unique_ptr<Entry>& entry) {
            if (entry == nullptr || !placed.insert(entry.get()).second)
            {
                return;
            }
            for (const Entry* needed : entry->needs)
            {
                if (const auto slot = slots.find(needed); slot != slots.end())
                {
                    place(*slot->second);
                }
            }
            ordered.push_back(std::move(entry));
        };
        for (auto& entry : structs)
        {
            place(entry);
        }
        for (auto& entry : dataAssets)
        {
            ordered.push_back(std::move(entry));
        }
        m_entries = std::move(ordered);
        return Succeeded();
    }

    bool ScriptDataAssets::HasUpgraders() const noexcept
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [](const std::unique_ptr<Entry>& entry) { return !entry->upgraders.empty(); });
    }

    Result ScriptDataAssets::Register(TypeRegistry& types, App& app, DataAssetUpgrade upgrade)
    {
        const auto refuse = [&](std::string diagnostic) {
            (void)Unregister(types, app, {});
            return Failed(std::move(diagnostic));
        };
        for (const auto& entry : m_entries)
        {
            for (const auto& pending : entry->pending)
            {
                pending.type->type = pending.ours != nullptr ? &pending.ours->description : types.Find(pending.target);
                if (pending.type->type == nullptr)
                {
                    return refuse("The script type '" + entry->name + "' holds '" + std::string(pending.target) +
                                  "', which the type registry does not have.");
                }
            }
            if (!types.Register(entry->description))
            {
                return refuse("The type registry refused the script type '" + entry->name + "'.");
            }
            entry->registered = true;
            for (const std::uint32_t from : entry->upgraders)
            {
                // One step, run in the script: the tree crosses in the tree encoding, without its comments, which the
                // managed reader drops.
                auto step = [upgrade, name = entry->name, app = &app](std::uint32_t fromVersion,
                                                                      Serialisation::Value& tree) {
                    const auto bytes = Serialisation::EncodeTree(tree, Serialisation::Trivia::Drop);
                    auto upgraded = upgrade(name, fromVersion, bytes);
                    if (!upgraded.Succeeded() || !upgraded.value)
                    {
                        app->Logger()->Error("Scripting: the upgrader of the C# data asset type '{}' from version {} "
                                             "failed, so the tree stays as it was: {}",
                                             name, fromVersion, upgraded.diagnostic);
                        return;
                    }
                    auto decoded = Serialisation::DecodeTree(*upgraded.value, name, *app->Logger());
                    if (!decoded.tree)
                    {
                        app->Logger()->Error("Scripting: the upgrader of the C# data asset type '{}' from version {} "
                                             "gave bytes that are not a tree, so the tree stays as it was.",
                                             name, fromVersion);
                        return;
                    }
                    tree = std::move(*decoded.tree);
                };
                entry->upgradersRegistered = true;
                if (!Serialisation::RegisterUpgrader(types, entry->name, from, std::move(step), *app.Logger()))
                {
                    return refuse("The upgrader of the script type '" + entry->name + "' from version " +
                                  std::to_string(from) + " could not be registered.");
                }
            }
        }
        return Succeeded();
    }

    bool ScriptDataAssets::Unregister(TypeRegistry& types, App& app, const Held& held)
    {
        for (const auto& entry : m_entries)
        {
            if (entry->upgradersRegistered)
            {
                (void)Serialisation::UnregisterUpgraders(types, entry->name);
                entry->upgradersRegistered = false;
            }
        }
        bool all = true;
        for (auto at = m_entries.rbegin(); at != m_entries.rend(); ++at)
        {
            Entry& entry = **at;
            if (!entry.registered)
            {
                continue;
            }
            // What a kept type extends or holds stays with it.
            const bool needed =
                std::any_of(m_entries.begin(), m_entries.end(), [&](const std::unique_ptr<Entry>& other) {
                    return other->registered && other.get() != &entry &&
                           std::find(other->needs.begin(), other->needs.end(), &entry) != other->needs.end();
                });
            if (needed)
            {
                all = false;
                continue;
            }
            if (entry.what == Entry::What::DataAsset && held)
            {
                if (const auto reason = held(entry.name))
                {
                    app.Logger()->Warn(
                        "Scripting: the C# data asset type '{}' stays registered, since {}; it goes when "
                        "a later script generation ends and nothing holds it.",
                        entry.name, *reason);
                    all = false;
                    continue;
                }
            }
            entry.registered = !types.Unregister(entry.name);
            all = all && !entry.registered;
        }
        return all;
    }

    std::vector<std::string> ScriptDataAssets::Names() const
    {
        std::vector<std::string> names;
        for (const auto& entry : m_entries)
        {
            if (entry->what == Entry::What::DataAsset)
            {
                names.push_back(entry->name);
            }
        }
        return names;
    }
} // namespace Fenrir::Detail
