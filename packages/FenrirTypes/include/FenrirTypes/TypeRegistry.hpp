#pragma once

#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fenrir
{
    /**
     * @brief A tag type an attachment is keyed by: it names the attached value's type as `Value`.
     *
     * A package above FenrirTypes defines its own tag, so the facts it records about a type (the ECS's component
     * type, the scene's doors, the serialiser's upgraders) reach the registry without FenrirTypes naming them.
     */
    template <typename Tag>
    concept AttachmentTag = requires { typename Tag::Value; } && std::movable<typename Tag::Value>;

    /**
     * @brief The run-time table of every described type the running engine knows, by canonical name (ADR-0074).
     *
     * It holds the descriptions packages register, not copies: each must outlive the registry, or its removal by
     * `Unregister`. A file reader, writer or inspector reaches a type's fields only through it. It never throws: a
     * refused registration, removal or attachment goes to the diagnostics function it was made with and returns false.
     */
    class TypeRegistry
    {
      public:
        /** @brief Where the registry reports what it refused, one message per refusal. */
        using Diagnostics = std::function<void(std::string_view message)>;

        /** @brief An empty registry, reporting to `diagnostics` (or nowhere, when it is empty). */
        explicit TypeRegistry(Diagnostics diagnostics = {});

        ~TypeRegistry();
        TypeRegistry(const TypeRegistry&) = delete;
        TypeRegistry& operator=(const TypeRegistry&) = delete;
        TypeRegistry(TypeRegistry&&) noexcept;
        TypeRegistry& operator=(TypeRegistry&&) noexcept;

        /**
         * @brief Adds `description` under its canonical name and its former names.
         *
         * Refused with a diagnostic, leaving the registry as it was, when one of those names is already a registered
         * type's (the diagnostic names both types and their packages) or appears twice among them, when its version is
         * below 1, when it has a base and is not a data asset, when its base is not a registered data asset type, when
         * a field holds an owning kind (itself, or through its elements or nested structs) in a role that does not
         * admit one, or when a field is of an owning kind and the type has no visit function. A `Nested` type's owning
         * kinds are judged in the role of the type that holds it.
         *
         * A tree-backed description (`TypeDescription::treeBacked`) needs no visit function for its owning fields, and
         * may extend a C++ data asset type. It is refused when it is not a data asset or nested type, when it has a
         * `size`, `construct`, `destroy` or `visit`, a field with an offset, alternatives or enumerators, or no
         * `defaultTree`. A type that is not tree-backed is refused when its base is tree-backed, or when a field holds
         * a tree-backed struct (itself, or through its elements or nested structs), since its layout would hold one.
         *
         * @return Whether it was added.
         */
        bool Register(const TypeDescription& description);

        /**
         * @brief Removes the type registered under `name`, its canonical or a former one, with every name it holds and
         * every attachment hung on it, so the same names can be registered again.
         *
         * For a type registered at run time, such as a C# data asset type a script reload drops or changes; not for
         * engine types, which their packages' code still names, though nothing but the base check below guards them.
         * Refused with a diagnostic, changing nothing, when no type is registered under `name` or while another
         * registered type names it as its base.
         *
         * @return Whether it was removed.
         */
        bool Unregister(std::string_view name);

        /**
         * @brief The type registered under `name`, its canonical or a former one; null when none is.
         *
         * The description's `name` is the current canonical name, whichever name found it.
         */
        [[nodiscard]] const TypeDescription* Find(std::string_view name) const noexcept;

        /** @brief Every registered type in `role`, in registration order. */
        [[nodiscard]] std::vector<const TypeDescription*> Types(Role role) const;

        /**
         * @brief Whether the type named `type` is the type named `base` or extends it, over its base chain.
         *
         * False when either is not registered. Either name may be canonical or former.
         */
        [[nodiscard]] bool IsA(std::string_view type, std::string_view base) const noexcept;

        /**
         * @brief Hangs `value` on the type registered under `name`, keyed by `Tag`, replacing any value already there.
         *
         * Refused with a diagnostic when no type is registered under `name`.
         *
         * @return Whether it was hung.
         */
        template <AttachmentTag Tag>
        bool Attach(std::string_view name, typename Tag::Value value)
        {
            return AttachSlot(name, std::type_index(typeid(Tag)),
                              std::make_unique<Slot<typename Tag::Value>>(std::move(value)));
        }

        /**
         * @brief Removes the value keyed by `Tag` from the type registered under `name`.
         *
         * @return Whether there was one to remove: false, with no diagnostic, when there was none or no type is
         * registered under `name`.
         */
        template <AttachmentTag Tag>
        bool Detach(std::string_view name) noexcept
        {
            return DetachSlot(name, std::type_index(typeid(Tag)));
        }

        /** @brief The value keyed by `Tag` on the type registered under `name`; null when there is none. */
        template <AttachmentTag Tag>
        [[nodiscard]] const typename Tag::Value* Attachment(std::string_view name) const noexcept
        {
            const auto* slot =
                static_cast<const Slot<typename Tag::Value>*>(FindSlot(name, std::type_index(typeid(Tag))));
            return slot != nullptr ? &slot->value : nullptr;
        }

        /** @brief The value keyed by `Tag` on the type registered under `name`, to change; null when there is none. */
        template <AttachmentTag Tag>
        [[nodiscard]] typename Tag::Value* Attachment(std::string_view name) noexcept
        {
            auto* slot = static_cast<Slot<typename Tag::Value>*>(FindSlot(name, std::type_index(typeid(Tag))));
            return slot != nullptr ? &slot->value : nullptr;
        }

      private:
        struct SlotBase
        {
            virtual ~SlotBase() = default;
        };

        template <typename Value>
        struct Slot final : SlotBase
        {
            explicit Slot(Value v) : value(std::move(v))
            {
            }
            Value value;
        };

        struct Entry
        {
            const TypeDescription* description = nullptr;
            std::unordered_map<std::type_index, std::unique_ptr<SlotBase>> attachments;
        };

        bool AttachSlot(std::string_view name, std::type_index tag, std::unique_ptr<SlotBase> slot);
        bool DetachSlot(std::string_view name, std::type_index tag) noexcept;
        [[nodiscard]] SlotBase* FindSlot(std::string_view name, std::type_index tag) const noexcept;
        [[nodiscard]] const Entry* FindEntry(std::string_view name) const noexcept;
        void Report(const std::string& message) const;

        Diagnostics m_diagnostics;
        // Held by pointer, so an attachment's address stays the same as more types register.
        std::vector<std::unique_ptr<Entry>> m_entries;
        // Every canonical and former name, to the entry of the type registered under it.
        std::map<std::string, std::size_t, std::less<>> m_names;
    };
} // namespace Fenrir
