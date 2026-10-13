#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Fenrir
{
    /**
     * @brief Describes a component type that is known only at run time: a block of plain bytes of a given size and
     * alignment, identified by a name. A scripting layer uses this for components that scripts define, which have no
     * C++ type.
     *
     * An EntityList stores the components of such a type like any other: one per entity, destroyed with the entity, and
     * reachable by a query beside the native components (see ComponentSelector). A new component is all zero bytes.
     *
     * Two descriptions with the same name are the same type, and must agree on size and alignment: an EntityList
     * refuses to add a component whose shape differs from the components of that name it already holds. This class says
     * nothing about how the ECS stores them.
     */
    class DynamicComponentType
    {
      public:
        /** @brief The largest component a dynamic type may have, in bytes. They are plain data that entities carry by
         * the thousand. */
        static constexpr std::size_t MaxSize = 64 * 1024;

        DynamicComponentType(std::string name, std::size_t size, std::size_t alignment);

        [[nodiscard]] const std::string& Name() const noexcept
        {
            return m_name;
        }
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_size;
        }
        [[nodiscard]] std::size_t Alignment() const noexcept
        {
            return m_alignment;
        }

        /** @brief A number derived from the name, equal for equal names. Two different names can in principle share
         * one. */
        [[nodiscard]] std::uint32_t Id() const noexcept
        {
            return m_id;
        }

        /**
         * @brief Whether this can describe a component: a name, a size up to MaxSize that is a multiple of the
         * alignment, and a power-of-two alignment.
         */
        [[nodiscard]] bool IsValid() const noexcept;

      private:
        std::string m_name;
        std::size_t m_size;
        std::size_t m_alignment;
        std::uint32_t m_id;
    };
} // namespace Fenrir
