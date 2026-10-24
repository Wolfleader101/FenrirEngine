#pragma once

#include "CallResult.hpp"
#include "Member.hpp"
#include "Method.hpp"
#include "Object.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Iris
{
    struct TypeInfo
    {
        std::string fullName;
        std::string assemblyName;
    };

    /**
     * @brief One argument of a custom attribute, as recorded in the assembly's metadata.
     *
     * `name` is empty for a constructor argument and the property or field name for a named one. `typeName` is
     * the managed type the argument was declared as. `value` is `std::monostate` for null and for an
     * argument that has no native form (an array), a `bool`, an `std::int64_t` (signed integers and enums),
     * an `std::uint64_t` (unsigned integers and chars), a `double` (`float` and `double`), or a
     * `std::string` (a string, or the full name of a `Type` argument).
     */
    struct AttributeArgument
    {
        std::string name;
        std::string typeName;
        std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string> value;
    };

    /**
     * @brief A custom attribute applied to a type: its type and its arguments, constructor arguments first.
     * Read as metadata; the attribute is never instantiated, so no component code runs.
     */
    struct AttributeInfo
    {
        TypeInfo type;
        std::vector<AttributeArgument> arguments;
    };

    /**
     * @brief What kind of managed type a member has, for display and serialization. Enums and structs are
     * reported with their type name in `type`; a class or interface is `Object`; a generic type, pointer or
     * anything else is `Other`.
     */
    enum class TypeKind : std::int32_t
    {
        Other = 0,
        Void = 1,
        Bool = 2,
        Char = 3,
        Int8 = 4,
        UInt8 = 5,
        Int16 = 6,
        UInt16 = 7,
        Int32 = 8,
        UInt32 = 9,
        Int64 = 10,
        UInt64 = 11,
        Float = 12,
        Double = 13,
        String = 14,
        Enum = 15,
        Struct = 16,
        Object = 17,
        Array = 18
    };

    /**
     * @brief A member's declared accessibility. For a property it is the most accessible accessor's.
     */
    enum class MemberVisibility : std::int32_t
    {
        Private = 0,
        PrivateProtected = 1,
        Internal = 2,
        Protected = 3,
        ProtectedInternal = 4,
        Public = 5
    };

    /**
     * @brief A field of a type. `offset` is the byte offset in memory for an instance field of a blittable
     * struct (the layout struct-mapping validation checks), and empty for anything else.
     */
    struct FieldInfo
    {
        std::string name;
        TypeInfo type;
        TypeKind kind = TypeKind::Other;
        bool isStatic = false;
        bool isReadOnly = false; // readonly or const
        MemberVisibility visibility = MemberVisibility::Private;
        std::optional<std::size_t> offset;
        std::vector<AttributeInfo> attributes;
    };

    /**
     * @brief A property of a type. `canRead` and `canWrite` say whether an accessor exists, whatever its
     * accessibility. Indexers are not listed.
     */
    struct PropertyInfo
    {
        std::string name;
        TypeInfo type;
        TypeKind kind = TypeKind::Other;
        bool isStatic = false;
        bool canRead = false;
        bool canWrite = false;
        MemberVisibility visibility = MemberVisibility::Private;
        std::vector<AttributeInfo> attributes;
    };

    struct ParameterInfo
    {
        std::string name;
        TypeInfo type;
        TypeKind kind = TypeKind::Other;
    };

    /**
     * @brief A method of a type. Constructors, property and event accessors, operators and compiler-generated
     * methods are not listed.
     */
    struct MethodInfo
    {
        std::string name;
        TypeInfo returnType;
        TypeKind returnKind = TypeKind::Void;
        bool isStatic = false;
        MemberVisibility visibility = MemberVisibility::Private;
        std::vector<ParameterInfo> parameters;
        std::vector<AttributeInfo> attributes;
    };

    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        template <typename Signature>
        struct MethodTraits;

        template <typename Result, typename... Args>
        struct MethodTraits<Result(Args...)>
        {
            using Handle = Method<Result(Args...)>;
            using InstanceHandle = InstanceMethod<Result(Args...)>;
            static constexpr KindSpec returnKind = KindSpecOf<Result>();
            static constexpr std::array<KindSpec, sizeof...(Args)> parameterKinds = {KindSpecOf<Args>()...};
        };
    } // namespace Detail

    /**
     * @brief A handle to a managed type found in a Component.
     *
     * Cheap to copy; it holds no managed reference. The handle is valid while its Component is loaded:
     * after the component is released, every call returns InvalidHandle. Handles are never reused, and
     * a handle only works with the component it was found in. It does not keep the component alive and
     * may outlive both the Component owner object and the Host.
     */
    class Type
    {
      public:
        [[nodiscard]] CallResult<TypeInfo> GetInfo() const;

        /**
         * @brief The base type, or an empty optional for a type without one (an interface, `System.Object`).
         * A snapshot: it names the type and does not need to be a type of this component.
         */
        [[nodiscard]] CallResult<std::optional<TypeInfo>> GetBaseType() const;

        /**
         * @brief Every interface the type implements, inherited ones included, sorted by full name.
         */
        [[nodiscard]] CallResult<std::vector<TypeInfo>> GetInterfaces() const;

        /**
         * @brief The custom attributes applied to the type itself, in metadata order, with their arguments.
         * Inherited attributes are not included.
         */
        [[nodiscard]] CallResult<std::vector<AttributeInfo>> GetAttributes() const;

        /**
         * @brief The type's fields, properties and methods, each with its name, type, kind, static-ness,
         * visibility and attributes, in declaration order. They are owning snapshots that hold no reference
         * to the component.
         *
         * Only members declared on this type are listed (inherited ones are found on the base type, see
         * GetBaseType), public and non-public alike, so an editor can show a private field marked for
         * serialization. Compiler-generated members (auto-property backing fields, accessors) are not listed.
         * Attributes are read as metadata and never instantiated, and have the shape of GetAttributes.
         */
        [[nodiscard]] CallResult<std::vector<FieldInfo>> GetFields() const;
        [[nodiscard]] CallResult<std::vector<PropertyInfo>> GetProperties() const;
        [[nodiscard]] CallResult<std::vector<MethodInfo>> GetMethods() const;

        /**
         * @brief Whether an instance of this type can be used as `other` (the same type, a subclass, or an
         * implementation of an interface). `other` must be a type of the same component.
         */
        [[nodiscard]] CallResult<bool> IsAssignableTo(const Type& other) const;

        /**
         * @brief Like the overload taking a Type, for a target named by full name: a type of the component
         * assembly, the framework (`System.IDisposable`), or an assembly the component references, such as
         * a shared contract. A name that cannot be resolved is NotFound.
         */
        [[nodiscard]] CallResult<bool> IsAssignableTo(std::string_view fullName) const;

        /**
         * @brief Finds a public static, non-generic method by name and exact signature.
         *
         * `FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add")` finds `int Add(int, int)`;
         * parameter and return types are derived from the C++ types (see SignatureArgument and Returnable), and
         * any other type does not compile. Overloads are told apart by their exact parameter types. A method
         * with another name, return type, parameter types or count, a generic or UnmanagedCallersOnly
         * method, or one that is not public is NotFound. The name is case-sensitive UTF-8 without null
         * characters, borrowed for the call. Finding the same method again returns an equivalent handle.
         */
        template <typename Signature>
        [[nodiscard]] CallResult<typename Detail::MethodTraits<Signature>::Handle> FindMethod(
            std::string_view name) const;

        /**
         * @brief Finds a public instance method by name and exact signature, possibly inherited.
         *
         * Like FindMethod, but the method is called on an Object (see InstanceMethod). A virtual method
         * found here dispatches to the override of the object's real type.
         */
        template <typename Signature>
        [[nodiscard]] CallResult<typename Detail::MethodTraits<Signature>::InstanceHandle> FindInstanceMethod(
            std::string_view name) const;

        /**
         * @brief Finds a public field of type `T` by name, static or instance, declared or inherited.
         *
         * `T` is a PrimitiveValue matched exactly (`std::int32_t` for an `int` field, no widening), a mapped
         * struct, `std::optional<std::string>`, or `std::optional<Object>` for a class- or interface-typed field. A
         * missing, private or differently typed field is NotFound. The name is case-sensitive UTF-8 without null
         * characters.
         */
        template <MemberValue T>
        [[nodiscard]] CallResult<Member<T>> FindField(std::string_view name) const;

        /**
         * @brief Finds a public property of type `T` by name; the same rules as FindField. Indexers are
         * not found, and a property needs at least one public accessor.
         */
        template <MemberValue T>
        [[nodiscard]] CallResult<Member<T>> FindProperty(std::string_view name) const;

        /**
         * @brief Constructs an object with the public constructor whose parameters match exactly.
         *
         * `CreateObject(std::int32_t{5}, 2.5)` calls `T(int, double)`; no arguments call the parameterless
         * constructor. Arguments follow Argument; their types are deduced, so pass a string as
         * `std::string_view` rather than a literal. An abstract or static type, a struct, or no matching
         * constructor, is NotFound; an exception from the constructor is ManagedException and leaves
         * nothing rooted. The returned Object owns the new instance.
         */
        template <typename... Args>
            requires(Argument<Args> && ...)
        [[nodiscard]] CallResult<Object> CreateObject(const Args&... arguments) const;

      private:
        friend class Component;
        struct State;
        explicit Type(std::shared_ptr<const State> state) noexcept;
        [[nodiscard]] CallResult<Detail::RawMethod> FindMethodRaw(std::string_view name, Detail::KindSpec returnKind,
                                                                  std::span<const Detail::KindSpec> parameterKinds,
                                                                  bool isStatic) const;
        [[nodiscard]] CallResult<Detail::RawMember> FindMemberRaw(std::string_view name, bool isProperty,
                                                                  Detail::KindSpec kind) const;
        [[nodiscard]] CallResult<Object> CreateObjectRaw(std::span<const Detail::KindSpec> parameterKinds,
                                                         std::span<std::uint64_t> slots,
                                                         std::span<const Object* const> objects) const;
        std::shared_ptr<const State> state;
    };

    template <typename Signature>
    CallResult<typename Detail::MethodTraits<Signature>::Handle> Type::FindMethod(std::string_view name) const
    {
        using Traits = Detail::MethodTraits<Signature>;
        auto raw =
            FindMethodRaw(name, Traits::returnKind, std::span<const Detail::KindSpec>(Traits::parameterKinds), true);
        CallResult<typename Traits::Handle> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(typename Traits::Handle(std::move(*raw.value)));
        }
        return result;
    }

    template <typename Signature>
    CallResult<typename Detail::MethodTraits<Signature>::InstanceHandle> Type::FindInstanceMethod(
        std::string_view name) const
    {
        using Traits = Detail::MethodTraits<Signature>;
        auto raw =
            FindMethodRaw(name, Traits::returnKind, std::span<const Detail::KindSpec>(Traits::parameterKinds), false);
        CallResult<typename Traits::InstanceHandle> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(typename Traits::InstanceHandle(std::move(*raw.value)));
        }
        return result;
    }

    template <typename... Args>
        requires(Argument<Args> && ...)
    CallResult<Object> Type::CreateObject(const Args&... arguments) const
    {
        const std::array<Detail::KindSpec, sizeof...(Args)> kinds = {Detail::KindSpecOf<Args>()...};
        std::uint64_t slots[sizeof...(Args) + 1] = {};
        const Object* objects[sizeof...(Args) + 1] = {};
        Detail::StringArgumentView texts[sizeof...(Args) + 1] = {};
        std::size_t index = 0;
        ((Detail::PackArgument(slots[index], objects[index], texts[index], arguments), ++index), ...);
        return CreateObjectRaw(std::span<const Detail::KindSpec>(kinds),
                               std::span<std::uint64_t>(slots, sizeof...(Args)),
                               std::span<const Object* const>(objects, sizeof...(Args)));
    }

    template <MemberValue T>
    CallResult<Member<T>> Type::FindField(std::string_view name) const
    {
        auto raw = FindMemberRaw(name, false, Detail::KindSpecOf<T>());
        CallResult<Member<T>> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(Member<T>(std::move(*raw.value)));
        }
        return result;
    }

    template <MemberValue T>
    CallResult<Member<T>> Type::FindProperty(std::string_view name) const
    {
        auto raw = FindMemberRaw(name, true, Detail::KindSpecOf<T>());
        CallResult<Member<T>> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(Member<T>(std::move(*raw.value)));
        }
        return result;
    }
} // namespace Iris
