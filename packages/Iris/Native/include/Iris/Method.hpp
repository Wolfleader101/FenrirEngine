#pragma once

#include "CallResult.hpp"
#include "Object.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Iris
{
    class Type;

    /**
     * @brief The primitive types: bool, 8/16/32/64-bit integers, float, double and `char16_t`, matched exactly
     * (no widening). A bool is one byte, and a `char16_t` is a UTF-16 code unit, a managed `char`. The other
     * character types are excluded because their managed meaning is ambiguous (`char` and `char8_t` are
     * accepted only as the elements of an inline array, where they are bytes).
     */
    template <typename T>
    concept PrimitiveValue =
        std::same_as<T, bool> || std::same_as<T, float> || std::same_as<T, double> || std::same_as<T, char16_t> ||
        (std::integral<T> && !std::same_as<T, char> && !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> &&
         !std::same_as<T, char16_t> && !std::same_as<T, char32_t> &&
         (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8));

    /**
     * @brief Declares how a C++ struct mirrors a managed blittable struct. Specialize it for each struct:
     *
     * ```cpp
     * template <> struct Iris::StructMapping<Vec3>
     * {
     *     static constexpr std::string_view typeName = "MyComponent.Vec3";
     *     static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Vec3, X), IRIS_FIELD(Vec3, Y));
     * };
     * ```
     *
     * List every field, in declaration order. A field can be a number, `bool`, `char16_t`, an enum with an
     * EnumMapping, another mapped struct, or a fixed-size array `T[N]` or `std::array<T, N>` of those (with
     * `char` and `char8_t` as bytes).
     * The managed side checks the size, field names, order, offsets and types the first time the struct
     * is used with a component; a mismatch, or a managed struct that is not blittable, fails the lookup
     * with a diagnostic and the call never happens.
     */
    template <typename T>
    struct StructMapping;

    /**
     * @brief A C++ struct with a StructMapping: trivially copyable standard-layout data that is passed to
     * managed code by value, returned by value, and used as a field or property value.
     */
    template <typename T>
    concept StructValue = !PrimitiveValue<T> && std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
                          std::default_initializable<T> && requires {
                              StructMapping<T>::typeName;
                              StructMapping<T>::fields;
                          };

    /**
     * @brief Declares which managed enum a C++ enum mirrors. Specialize it for each enum:
     *
     * ```cpp
     * enum class Mode : std::int32_t { Off, On };
     * template <> struct Iris::EnumMapping<Mode> { static constexpr std::string_view typeName = "MyComponent.Mode"; };
     * ```
     *
     * An enum crosses as its underlying integer, so the managed enum must have exactly the same underlying
     * type (a mismatch fails the lookup with a diagnostic and the call never happens). Values the managed enum
     * does not name are carried unchanged.
     */
    template <typename T>
    struct EnumMapping;

    /**
     * @brief A C++ enum with an EnumMapping, passed by value to and from managed code and used as a field,
     * property or mapped-struct field. Its underlying type must be a sized integer.
     */
    template <typename T>
    concept EnumValue =
        std::is_enum_v<T> && requires { EnumMapping<T>::typeName; } && std::integral<std::underlying_type_t<T>> &&
        !std::same_as<std::underlying_type_t<T>, bool> && !std::same_as<std::underlying_type_t<T>, char> &&
        !std::same_as<std::underlying_type_t<T>, wchar_t> && !std::same_as<std::underlying_type_t<T>, char8_t> &&
        !std::same_as<std::underlying_type_t<T>, char16_t> && !std::same_as<std::underlying_type_t<T>, char32_t>;

    /**
     * @brief What an Array can hold: a PrimitiveValue, a mapped struct (see StructMapping), or
     * `std::string` for a managed `string[]`.
     */
    template <typename T>
    concept ArrayElement = PrimitiveValue<T> || StructValue<T> || std::same_as<T, std::string>;

    template <ArrayElement T>
    class Array;

    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        // Array<E>, or `const Array<E>*` (a nullable array argument): `value` and the element type.
        template <typename T>
        struct ArrayInfo
        {
            static constexpr bool value = false;
        };
        template <ArrayElement E>
        struct ArrayInfo<Array<E>>
        {
            static constexpr bool value = true;
            using Element = E;
        };
        template <typename T>
        struct ArrayInfo<const T*> : ArrayInfo<T>
        {
        };
        // std::optional<Array<E>>, a returned array where empty is null.
        template <typename T>
        struct OptionalArrayInfo
        {
            static constexpr bool value = false;
        };
        template <ArrayElement E>
        struct OptionalArrayInfo<std::optional<Array<E>>>
        {
            static constexpr bool value = true;
            using Element = E;
        };
        // The library's access to an Array's internals: wrapping a returned `T[]` handle (only ever one whose
        // element type the signature already matched), and the Object an array argument travels as.
        struct ArrayAccess
        {
            template <typename ArrayType>
            [[nodiscard]] static ArrayType Adopt(Object object)
            {
                return ArrayType(std::move(object), 0);
            }
            // The Object an array argument travels as.
            template <typename ArrayType>
            [[nodiscard]] static const Object& ObjectOf(const ArrayType& array) noexcept
            {
                return array.object;
            }
        };

        // An array as declared in a signature: `const Array<E>&` or `const Array<E>*`.
        template <typename T>
        concept ArraySignature =
            (std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>> &&
             ArrayInfo<std::remove_cvref_t<T>>::value && !std::is_pointer_v<std::remove_cvref_t<T>>) ||
            (std::is_pointer_v<T> && ArrayInfo<T>::value);
    } // namespace Detail

    /**
     * @brief A string passed to managed code: `std::string_view` for a string that is never null, or
     * `std::optional<std::string_view>` where an empty optional is a null string. The text is UTF-8,
     * may contain null characters, and is borrowed only for the duration of the call; malformed UTF-8 is
     * InvalidArgument and no managed code runs.
     */
    template <typename T>
    concept StringArgument = std::same_as<std::remove_cvref_t<T>, std::string_view> ||
                             std::same_as<std::remove_cvref_t<T>, std::optional<std::string_view>>;

    /**
     * @brief What can be passed to a managed method or constructor: a PrimitiveValue, a string (see
     * StringArgument), a mapped struct (see StructMapping), a mapped enum (see EnumMapping), an Object, which
     * matches any managed class or interface parameter, or an Array (`const Array<T>*` passes a nullable array).
     */
    template <typename T>
    concept Argument = PrimitiveValue<std::remove_cvref_t<T>> || StringArgument<T> ||
                       StructValue<std::remove_cvref_t<T>> || EnumValue<std::remove_cvref_t<T>> ||
                       std::same_as<std::remove_cvref_t<T>, Object> || Detail::ArrayInfo<std::remove_cvref_t<T>>::value;

    /**
     * @brief An argument as declared in a method signature: a PrimitiveValue or mapped struct by value, a
     * string as `std::string_view` or `std::optional<std::string_view>`, an Object as `const Object&`, or an
     * array as `const Array<T>&` (never null) or `const Array<T>*` (null allowed). Owners cannot be copied, so
     * a by-value Object or Array is not a signature argument.
     */
    template <typename T>
    concept SignatureArgument = PrimitiveValue<T> || StringArgument<T> || StructValue<T> || EnumValue<T> ||
                                std::same_as<T, const Object&> || Detail::ArraySignature<T>;

    /**
     * @brief What a managed method can return: void, a PrimitiveValue, a mapped struct or enum,
     * `std::optional<Object>`, `std::optional<std::string>`, or `std::optional<Array<T>>`. In each optional an empty
     * value is a null return, which a string keeps distinct from the empty string and an array from an empty array. A
     * returned string is an owned copy; a returned Object or Array is a new, independently owned handle.
     */
    template <typename T>
    concept Returnable = std::is_void_v<T> || PrimitiveValue<T> || StructValue<T> || EnumValue<T> ||
                         std::same_as<T, std::optional<Object>> || std::same_as<T, std::optional<std::string>> ||
                         Detail::OptionalArrayInfo<T>::value;

    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        // Wire values, mirrored by the managed ValueKind. Void is a return type only.
        enum class ValueKind : std::int32_t
        {
            Void = 0,
            Bool = 1,
            Int8 = 2,
            UInt8 = 3,
            Int16 = 4,
            UInt16 = 5,
            Int32 = 6,
            UInt32 = 7,
            Int64 = 8,
            UInt64 = 9,
            Float = 10,
            Double = 11,
            Object = 12,
            String = 13,
            Struct = 14,
            Array = 15,   // Only in a KindSpec; on the wire it is combined with the element kind.
            Pointer = 17, // Only in internal call signatures.
            Char = 18,
            Enum = 19,       // On the wire combined with the enum's handle, like Struct.
            InlineArray = 20 // Only as a struct field: the element kind and length are described beside it.
        };

        struct StructInfo;

        // One field of a mapped struct: name, byte offset, kind, and for a struct or enum field its own mapping.
        // A fixed-size array field has kind InlineArray, and its element's kind, mapping (`nested`) and length.
        struct StructFieldInfo
        {
            std::string_view name;
            std::size_t offset;
            ValueKind kind;
            const StructInfo* nested;
            ValueKind element = ValueKind::Void;
            std::size_t length = 0;
        };

        // A struct mapping as the bridge needs it. An enum mapping has no fields and names its underlying
        // integer kind instead.
        struct StructInfo
        {
            std::string_view typeName;
            std::size_t size;
            std::span<const StructFieldInfo> fields;
            ValueKind underlying = ValueKind::Void;
        };

        template <StructValue T>
        inline constexpr StructInfo StructInfoOf{StructMapping<T>::typeName, sizeof(T),
                                                 std::span<const StructFieldInfo>(StructMapping<T>::fields)};

        template <typename Declared>
        constexpr ValueKind KindOf() noexcept;

        template <EnumValue T>
        inline constexpr StructInfo EnumInfoOf{
            EnumMapping<T>::typeName, sizeof(T), {}, KindOf<std::underlying_type_t<T>>()};

        // The struct or enum mapping a declared type needs registered, or null.
        template <typename Declared>
        constexpr const StructInfo* StructInfoPtr() noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (StructValue<T>)
                return &StructInfoOf<T>;
            else if constexpr (EnumValue<T>)
                return &EnumInfoOf<T>;
            else
                return nullptr;
        }

        // What a string argument's slot points to; mirrored by the managed StringArgument. It lives on the
        // caller's stack for the call, and the text it points to is borrowed from the caller.
        struct StringArgumentView
        {
            const std::uint8_t* data = nullptr;
            std::int32_t length = 0;
            std::int32_t isNull = 1;
        };

        inline StringArgumentView MakeStringView(std::optional<std::string_view> text) noexcept
        {
            StringArgumentView view;
            if (text)
            {
                view.data = reinterpret_cast<const std::uint8_t*>(text->data());
                // A string too long for the wire reports a length the bridge rejects.
                view.length = text->size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
                                  ? -1
                                  : static_cast<std::int32_t>(text->size());
                view.isNull = 0;
            }
            return view;
        }

        template <typename Declared>
        constexpr ValueKind KindOf() noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (std::is_void_v<T>)
                return ValueKind::Void;
            else if constexpr (std::same_as<T, Object> || std::same_as<T, std::optional<Object>>)
                return ValueKind::Object;
            else if constexpr (std::same_as<T, std::string_view> || std::same_as<T, std::optional<std::string_view>> ||
                               std::same_as<T, std::optional<std::string>>)
                return ValueKind::String;
            else if constexpr (StructValue<T>)
                return ValueKind::Struct;
            else if constexpr (EnumValue<T>)
                return ValueKind::Enum;
            else if constexpr (ArrayInfo<T>::value || OptionalArrayInfo<T>::value)
                return ValueKind::Array;
            else
            {
                static_assert(PrimitiveValue<T>,
                              "Iris cannot pass this C++ type to managed code. Use bool, a sized integer, "
                              "char16_t, float, double, std::string_view (std::optional<std::string> to "
                              "return), a struct with a StructMapping, an enum with an EnumMapping, "
                              "const Object& or Array<T>.");
                if constexpr (std::same_as<T, bool>)
                    return ValueKind::Bool;
                else if constexpr (std::same_as<T, char16_t>)
                    return ValueKind::Char;
                else if constexpr (std::same_as<T, float>)
                    return ValueKind::Float;
                else if constexpr (std::same_as<T, double>)
                    return ValueKind::Double;
                else if constexpr (sizeof(T) == 1)
                    return std::is_signed_v<T> ? ValueKind::Int8 : ValueKind::UInt8;
                else if constexpr (sizeof(T) == 2)
                    return std::is_signed_v<T> ? ValueKind::Int16 : ValueKind::UInt16;
                else if constexpr (sizeof(T) == 4)
                    return std::is_signed_v<T> ? ValueKind::Int32 : ValueKind::UInt32;
                else
                    return std::is_signed_v<T> ? ValueKind::Int64 : ValueKind::UInt64;
            }
        }

        // A kind as declared in C++, with the mapping when it is a struct (the bridge handle for the struct
        // is only known once the mapping is registered with a component).
        struct KindSpec
        {
            ValueKind kind;
            const StructInfo* info;              // The struct's mapping, or an array's struct element mapping.
            ValueKind element = ValueKind::Void; // An array's element kind.
        };

        // The kind of an array element: String for std::string, else as for a value.
        template <typename E>
        constexpr KindSpec ElementSpecOf() noexcept
        {
            if constexpr (std::same_as<E, std::string>)
                return {ValueKind::String, nullptr};
            else
                return {KindOf<E>(), StructInfoPtr<E>()};
        }

        template <typename Declared>
        constexpr KindSpec KindSpecOf() noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (ArrayInfo<T>::value)
            {
                constexpr KindSpec element = ElementSpecOf<typename ArrayInfo<T>::Element>();
                return {ValueKind::Array, element.info, element.kind};
            }
            else if constexpr (OptionalArrayInfo<T>::value)
            {
                constexpr KindSpec element = ElementSpecOf<typename OptionalArrayInfo<T>::Element>();
                return {ValueKind::Array, element.info, element.kind};
            }
            else
            {
                return {KindOf<Declared>(), StructInfoPtr<Declared>()};
            }
        }

        // A fixed-size array member: `T[N]` or `std::array<T, N>`.
        template <typename T>
        struct InlineArrayInfo
        {
            static constexpr bool value = false;
        };
        template <typename E, std::size_t N>
        struct InlineArrayInfo<E[N]>
        {
            static constexpr bool value = true;
            using Element = E;
            static constexpr std::size_t length = N;
        };
        template <typename E, std::size_t N>
        struct InlineArrayInfo<std::array<E, N>>
        {
            static constexpr bool value = true;
            using Element = E;
            static constexpr std::size_t length = N;
        };

        // What an inline array can hold: the values a struct field can, and `char` or `char8_t` as bytes.
        template <typename E>
        concept InlineArrayElement =
            PrimitiveValue<E> || StructValue<E> || EnumValue<E> || std::same_as<E, char> || std::same_as<E, char8_t>;

        // The kind of an inline array's elements: `char` and `char8_t` are bytes (a managed `byte`).
        template <typename E>
        constexpr ValueKind InlineElementKindOf() noexcept
        {
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t>)
                return ValueKind::UInt8;
            else
                return KindOf<E>();
        }

        template <typename Field>
        constexpr StructFieldInfo MakeField(std::string_view name, std::size_t offset) noexcept
        {
            if constexpr (InlineArrayInfo<Field>::value)
            {
                using Element = typename InlineArrayInfo<Field>::Element;
                static_assert(InlineArrayElement<Element>,
                              "Iris cannot map this array member. An array member can hold bool, a sized integer, "
                              "char, char8_t, char16_t, float, double, a struct with a StructMapping or an enum "
                              "with an EnumMapping.");
                return {name,
                        offset,
                        ValueKind::InlineArray,
                        StructInfoPtr<Element>(),
                        InlineElementKindOf<Element>(),
                        InlineArrayInfo<Field>::length};
            }
            else
            {
                return {name, offset, KindOf<Field>(), StructInfoPtr<Field>()};
            }
        }

        // Each value travels in one 64-bit slot: integers sign- or zero-extended, bool as 0 or 1, and
        // float and double as their raw IEEE bits (float in the low 32 bits). An Object's slot is filled
        // in from its handle when the call is made.
        template <typename T>
            requires(PrimitiveValue<T> || EnumValue<T>)
        constexpr std::uint64_t ToSlot(T value) noexcept
        {
            if constexpr (std::is_enum_v<T>)
                return ToSlot<std::underlying_type_t<T>>(static_cast<std::underlying_type_t<T>>(value));
            else if constexpr (std::same_as<T, float>)
                return std::bit_cast<std::uint32_t>(value);
            else if constexpr (std::same_as<T, double>)
                return std::bit_cast<std::uint64_t>(value);
            else if constexpr (std::same_as<T, bool>)
                return value ? 1U : 0U;
            else if constexpr (std::is_signed_v<T>)
                return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
            else
                return static_cast<std::uint64_t>(value);
        }

        template <typename T>
            requires(PrimitiveValue<T> || EnumValue<T>)
        constexpr T FromSlot(std::uint64_t slot) noexcept
        {
            if constexpr (std::is_enum_v<T>)
                return static_cast<T>(FromSlot<std::underlying_type_t<T>>(slot));
            else if constexpr (std::same_as<T, float>)
                return std::bit_cast<float>(static_cast<std::uint32_t>(slot));
            else if constexpr (std::same_as<T, double>)
                return std::bit_cast<double>(slot);
            else if constexpr (std::same_as<T, bool>)
                return slot != 0;
            else
                return static_cast<T>(slot);
        }

        // Records one argument: its slot for a primitive, the Object whose handle the call will use, or
        // for a string a view (kept by the caller until the call returns) that the slot points to.
        template <typename Declared>
        void PackArgument(std::uint64_t& slot, const Object*& object, StringArgumentView& text,
                          const Declared& value) noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (std::same_as<T, Object>)
                object = &value;
            else if constexpr (std::same_as<T, std::string_view> || std::same_as<T, std::optional<std::string_view>>)
            {
                text = MakeStringView(value);
                slot = reinterpret_cast<std::uintptr_t>(&text);
            }
            else if constexpr (StructValue<T>)
            {
                // The struct's bytes are borrowed from the caller for the call.
                slot = reinterpret_cast<std::uintptr_t>(&value);
            }
            else if constexpr (ArrayInfo<T>::value)
            {
                // An array travels as its object handle; a null pointer is a null array (slot zero).
                if constexpr (std::is_pointer_v<T>)
                    object = value != nullptr ? &ArrayAccess::ObjectOf(*value) : nullptr;
                else
                    object = &ArrayAccess::ObjectOf(value);
            }
            else
                slot = ToSlot<T>(value);
        }

        // An untyped method handle: arguments and result are raw slots. Use Method<Signature> instead.
        class RawMethod
        {
          public:
            // `objects[i]` is the Object for argument i, or null when `slots[i]` already holds the value.
            // Object arguments are checked (valid, same component) and their handles written into `slots`.
            [[nodiscard]] CallResult<std::uint64_t> Invoke(std::span<std::uint64_t> slots,
                                                           std::span<const Object* const> objects) const;
            [[nodiscard]] CallResult<std::uint64_t> InvokeInstance(const Object& target, std::span<std::uint64_t> slots,
                                                                   std::span<const Object* const> objects) const;

            // Takes ownership of an object handle the bridge returned from this method's component.
            [[nodiscard]] Object AdoptObject(std::uint64_t handle) const;
            // Copies and releases a string block the bridge returned from this method's component.
            [[nodiscard]] std::optional<std::string> TakeString(std::uint64_t block) const;
            // Copies and releases a struct block the bridge returned from this method's component.
            void TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept;

            // Why object arguments cannot be sent, or nothing: shared with Type::CreateObject.
            [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> ResolveObjects(
                std::uint64_t component, std::span<std::uint64_t> slots, std::span<const Object* const> objects);

          private:
            friend class ::Iris::Type;
            struct State;
            explicit RawMethod(std::shared_ptr<const State> state) noexcept;
            std::shared_ptr<const State> state;
        };

        template <typename Result, typename Handle>
        CallResult<Result> ToTyped(const Handle& method, CallResult<std::uint64_t> raw)
        {
            CallResult<Result> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if constexpr (!std::is_void_v<Result>)
            {
                if (raw.value)
                {
                    if constexpr (OptionalArrayInfo<Result>::value)
                    {
                        using Element = typename OptionalArrayInfo<Result>::Element;
                        result.value.emplace(*raw.value == 0 ? std::nullopt
                                                             : Result(ArrayAccess::Adopt<Array<Element>>(
                                                                   method.AdoptObject(*raw.value))));
                    }
                    else if constexpr (std::same_as<Result, std::optional<Object>>)
                    {
                        // Zero is a null return; anything else is a new, independently owned handle.
                        result.value.emplace(*raw.value == 0 ? std::nullopt
                                                             : std::optional<Object>(method.AdoptObject(*raw.value)));
                    }
                    else if constexpr (StructValue<Result>)
                    {
                        Result value;
                        method.TakeStruct(*raw.value, &value, sizeof(Result));
                        result.value = value;
                    }
                    else if constexpr (std::same_as<Result, std::optional<std::string>>)
                    {
                        // Zero is a null return; anything else is a block to copy and give back.
                        result.value.emplace(method.TakeString(*raw.value));
                    }
                    else
                    {
                        result.value = FromSlot<Result>(*raw.value);
                    }
                }
            }
            return result;
        }
    } // namespace Detail

    template <typename Signature>
    class Method;

    /**
     * @brief A handle to a public static method, resolved by signature.
     *
     * `Method<std::int32_t(std::int32_t, std::int32_t)>` is a method `int Name(int, int)`. Found with
     * Type::FindMethod and called any number of times with Invoke, which checks nothing at runtime
     * beyond the handle: the signature was matched when the method was found. Like Type, a Method is a
     * cheap, copyable value that holds no managed reference and stops working (InvalidHandle) when its
     * component is released. A managed exception thrown by the method is ManagedException.
     *
     * An `Object` parameter (`const Object&`) matches any class or interface parameter, so the object's
     * real type is checked when the call is made (InvalidArgument if it does not fit). A method returning
     * an object is declared as returning `std::optional<Object>`: each call gets a new, independently
     * owned handle, even when the method returns the same managed object again, and an empty optional is
     * a null return.
     *
     * Strings, mapped structs and arrays follow SignatureArgument and Returnable: a string is borrowed for the
     * call and returned as an owned copy, a struct's bytes are borrowed and returned by value, and an array is
     * passed as its handle (the callee sees the same elements).
     */
    template <typename... Args, typename Result>
        requires((SignatureArgument<Args> && ...) && Returnable<Result>)
    class Method<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(const Args&... arguments) const
        {
            std::uint64_t slots[sizeof...(Args) + 1] = {};
            const Object* objects[sizeof...(Args) + 1] = {};
            Detail::StringArgumentView texts[sizeof...(Args) + 1] = {};
            std::size_t index = 0;
            ((Detail::PackArgument(slots[index], objects[index], texts[index], arguments), ++index), ...);
            return Detail::ToTyped<Result>(method,
                                           method.Invoke(std::span<std::uint64_t>(slots, sizeof...(Args)),
                                                         std::span<const Object* const>(objects, sizeof...(Args))));
        }

      private:
        friend class Type;
        explicit Method(Detail::RawMethod rawMethod) noexcept : method(std::move(rawMethod))
        {
        }
        Detail::RawMethod method;
    };

    template <typename Signature>
    class InstanceMethod;

    /**
     * @brief A handle to a public instance method, resolved by signature.
     *
     * Found with Type::FindInstanceMethod and called on an Object that is an instance of the method's
     * declaring type (the type it was found on, or one of its subclasses). A virtual method dispatches
     * to the object's override. The object must belong to the same component, or the call is
     * InvalidArgument; a released object or component is InvalidHandle. Arguments and results follow the
     * rules of Method.
     */
    template <typename... Args, typename Result>
        requires((SignatureArgument<Args> && ...) && Returnable<Result>)
    class InstanceMethod<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(const Object& object, const Args&... arguments) const
        {
            std::uint64_t slots[sizeof...(Args) + 1] = {};
            const Object* objects[sizeof...(Args) + 1] = {};
            Detail::StringArgumentView texts[sizeof...(Args) + 1] = {};
            std::size_t index = 0;
            ((Detail::PackArgument(slots[index], objects[index], texts[index], arguments), ++index), ...);
            return Detail::ToTyped<Result>(
                method, method.InvokeInstance(object, std::span<std::uint64_t>(slots, sizeof...(Args)),
                                              std::span<const Object* const>(objects, sizeof...(Args))));
        }

      private:
        friend class Type;
        explicit InstanceMethod(Detail::RawMethod rawMethod) noexcept : method(std::move(rawMethod))
        {
        }
        Detail::RawMethod method;
    };
} // namespace Iris

namespace Iris
{
    /**
     * @brief The fields of a StructMapping, from IRIS_FIELD entries.
     */
    template <typename... Fields>
    constexpr std::array<Detail::StructFieldInfo, sizeof...(Fields)> StructFields(Fields... fields) noexcept
    {
        return {fields...};
    }
} // namespace Iris

/// One field of a mapped struct: `IRIS_FIELD(Vec3, X)`. The managed field has the same name as the C++ member.
#define IRIS_FIELD(Type, member) ::Iris::Detail::MakeField<decltype(Type::member)>(#member, offsetof(Type, member))

/// One field of a mapped struct whose managed name differs from the C++ member's: `IRIS_FIELD_AS(Transform, pos,
/// "Position")`. Fields are matched by position, then checked by name, so the name is what the managed struct calls it.
#define IRIS_FIELD_AS(Type, member, managedName) \
    ::Iris::Detail::MakeField<decltype(Type::member)>(managedName, offsetof(Type, member))
