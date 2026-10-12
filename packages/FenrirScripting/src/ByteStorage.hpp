#pragma once

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace Fenrir::Detail
{
    // Names the kind of storage a C# component lives in. The registry builds a storage from a type, so this tag stands for
    // "an opaque byte pool"; one pool per C# component type is made under a name (see ScriptComponentStorageId).
    struct ScriptComponentTag
    {
    };

    /**
     * An EnTT storage of opaque, fixed-size elements, whose size and alignment are known only at run time: the pool behind a
     * C# component. EnTT builds its typed storages from compile-time types, so a component type that exists only after a
     * script assembly loads needs this one class instead.
     *
     * It is an ordinary EnTT sparse set underneath, so it takes part in everything the registry does with a storage:
     * `registry.destroy` removes the entity's element, and runtime views iterate it beside native pools.
     *
     * Elements live in pages of a fixed number of elements, each page allocated once, never moved and aligned to the
     * element's alignment. Adding entities therefore never moves an existing element. (Removing one moves the last
     * element into its place, as EnTT's own storage does.) A new element is all zero bytes, which is a C# `default`.
     *
     * Pages are kept for the life of the storage: clearing it or removing every element empties it but returns no memory,
     * so a later element reuses a page. Only reshaping an empty storage (Configure) or destroying it releases them.
     */
    class ByteStorage final : public entt::basic_sparse_set<entt::entity>
    {
        using base_type = entt::basic_sparse_set<entt::entity>;
        using underlying_iterator = typename base_type::basic_iterator;

      public:
        static constexpr std::size_t ElementsPerPage = 256;

        // The largest element a script component may have. A component is plain data that an entity carries by the
        // thousand; this keeps a page (ElementsPerPage elements) to 16 MiB and the arithmetic far from overflow.
        static constexpr std::size_t MaxElementSize = 64 * 1024;

        explicit ByteStorage(const allocator_type& allocator)
            : base_type{entt::type_id<ScriptComponentTag>(), entt::deletion_policy::swap_and_pop, allocator}
        {
        }

        ByteStorage(const ByteStorage&) = delete;
        ByteStorage& operator=(const ByteStorage&) = delete;

        ~ByteStorage() override
        {
            ReleasePages();
        }

        // Sets the element size and alignment. Allowed while nothing is stored, and a repeat with the same values is
        // harmless. False if the storage is in use with different values, or the values cannot be a C# struct's.
        [[nodiscard]] bool Configure(std::size_t size, std::size_t alignment)
        {
            const bool valid = size > 0 && size <= MaxElementSize && alignment > 0 && (alignment & (alignment - 1)) == 0 &&
                               size % alignment == 0;
            if (!valid)
            {
                return false;
            }
            if (m_size == size && m_alignment == alignment)
            {
                return true;
            }
            if (base_type::size() != 0)
            {
                return false;
            }
            ReleasePages(); // Pages from an earlier, different configuration (the storage is empty).
            m_size = size;
            m_alignment = alignment;
            return true;
        }

        [[nodiscard]] bool IsConfigured() const noexcept
        {
            return m_size != 0;
        }

        [[nodiscard]] std::size_t ElementSize() const noexcept
        {
            return m_size;
        }

        // The entity's element, or null if it has none. The pointer is good until that element is removed or another
        // is removed from this storage (the last element is moved into the gap).
        [[nodiscard]] void* Find(entt::entity entity)
        {
            return base_type::contains(entity) ? ElementAt(base_type::index(entity)) : nullptr;
        }

        // Gives the entity an element of zero bytes, or returns the one it has. Null if the storage is not configured.
        [[nodiscard]] void* Emplace(entt::entity entity)
        {
            if (!IsConfigured())
            {
                return nullptr;
            }
            if (base_type::contains(entity))
            {
                return ElementAt(base_type::index(entity));
            }
            return ElementAt(static_cast<std::size_t>(base_type::push(entity).index()));
        }

      protected:
        // Erases entities, moving the last element into each gap.
        void pop(underlying_iterator first, underlying_iterator last) override
        {
            for (; first != last; ++first)
            {
                const std::size_t position = base_type::index(*first);
                const std::size_t lastPosition = base_type::size() - 1u;
                if (position != lastPosition)
                {
                    std::memcpy(ElementAt(position), ElementAt(lastPosition), m_size);
                }
                base_type::swap_and_pop(first);
            }
        }

        // Assigns an entity an element: a copy of `value`'s bytes, or zeros.
        underlying_iterator try_emplace(const entt::entity entity, const bool forceBack, const void* value) override
        {
            // Not configured: nothing can be stored (EnTT's convention for "could not emplace" is the end iterator). This
            // also keeps the inherited public push() from storing zero-sized elements.
            if (!IsConfigured())
            {
                return base_type::end();
            }
            // The page first. If allocating it throws, the entity has not been added, so no entity is left without an element.
            // (This storage appends: the new element's position is the current size.)
            AssureElement(base_type::size());
            const auto it = base_type::try_emplace(entity, forceBack);
            void* element = ElementAt(static_cast<std::size_t>(it.index()));
            if (value != nullptr)
            {
                std::memcpy(element, value, m_size);
            }
            else
            {
                std::memset(element, 0, m_size);
            }
            return it;
        }

      private:
        [[nodiscard]] std::size_t PageBytes() const noexcept
        {
            return m_size * ElementsPerPage;
        }

        [[nodiscard]] std::size_t PageAlignment() const noexcept
        {
            return m_alignment > alignof(std::max_align_t) ? m_alignment : alignof(std::max_align_t);
        }

        [[nodiscard]] void* ElementAt(std::size_t position) const
        {
            return payload[position / ElementsPerPage] + (position % ElementsPerPage) * m_size;
        }

        // The element at a position, allocating the pages up to it. Existing pages are never touched.
        void* AssureElement(std::size_t position)
        {
            const std::size_t page = position / ElementsPerPage;
            while (payload.size() <= page)
            {
                payload.push_back(static_cast<std::byte*>(::operator new(PageBytes(), std::align_val_t{PageAlignment()})));
            }
            return ElementAt(position);
        }

        void ReleasePages()
        {
            for (std::byte* page : payload)
            {
                ::operator delete(page, std::align_val_t{PageAlignment()});
            }
            payload.clear();
        }

        // Entities are swapped or moved by their positions when something sorts the storage.
        void swap_or_move(const std::size_t from, const std::size_t to) override
        {
            std::byte* left = static_cast<std::byte*>(ElementAt(from));
            std::byte* right = static_cast<std::byte*>(ElementAt(to));
            for (std::size_t index = 0; index < m_size; ++index)
            {
                std::swap(left[index], right[index]);
            }
        }

        const void* get_at(const std::size_t position) const override
        {
            return ElementAt(position);
        }

        std::vector<std::byte*> payload;
        std::size_t m_size = 0;
        std::size_t m_alignment = 0;
    };
} // namespace Fenrir::Detail

// Tells the registry which storage a ScriptComponentTag gets.
template <typename Entity, typename Allocator>
struct entt::storage_type<Fenrir::Detail::ScriptComponentTag, Entity, Allocator>
{
    using type = Fenrir::Detail::ByteStorage;
};
