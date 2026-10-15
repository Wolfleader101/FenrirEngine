#pragma once

#include <cstddef>
#include <memory>

#include "FenrirECS/DynamicComponent.hpp"

namespace Fenrir
{
    class EntityList;

    namespace Detail
    {
        struct WatchRecord;
    }

    /**
     * @brief Names one kind of component, native (a C++ type) or dynamic (a DynamicComponentType), so that code which
     * knows a component only at run time can reach it through EntityList: get, has, add, remove, count, clear and
     * queries all take one.
     *
     * Whoever holds one need not know which kind it is. It holds no reference to any EntityList, so make it once and
     * use it with every scene. Cheap to copy. It carries no name: naming a component (for scripts, an inspector or a
     * file) is the caller's business; a dynamic type's name is part of its DynamicComponentType.
     *
     * Native is defined with EntityList: include "FenrirECS/EntityList.hpp" to make a native one.
     */
    class ComponentType
    {
      public:
        /** @brief The C++ component type `T`, which must be default constructible and hold data. */
        template <typename T>
        [[nodiscard]] static ComponentType Native();

        /** @brief A component type known only at run time. Nothing is checked until it is used. */
        [[nodiscard]] static ComponentType Dynamic(DynamicComponentType type);

        /** @brief Bytes in one component. */
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_size;
        }

        /** @brief Alignment of one component. */
        [[nodiscard]] std::size_t Alignment() const noexcept
        {
            return m_alignment;
        }

        /** @brief Whether this is a dynamic type rather than a C++ one. */
        [[nodiscard]] bool IsDynamic() const noexcept
        {
            return m_dynamic != nullptr;
        }

      private:
        friend class EntityList;

        // What EntityList needs that only the C++ type knows: where its components are. The storage comes back as an
        // opaque pointer so that no ECS library type appears here.
        using NativeStorageFinder = void* (*)(EntityList& entities);

        // Starts (or stops) a watch's record recording the adds and removes of a native storage, which only the C++
        // type can reach the signals of.
        using NativeWatchHook = void (*)(void* storage, Detail::WatchRecord& record, bool start);

        ComponentType(NativeStorageFinder findNativeStorage, NativeWatchHook hookNativeWatch,
                      std::shared_ptr<const DynamicComponentType> dynamic, std::size_t size, std::size_t alignment)
            : m_findNativeStorage(findNativeStorage), m_hookNativeWatch(hookNativeWatch), m_dynamic(std::move(dynamic)),
              m_size(size), m_alignment(alignment)
        {
        }

        NativeStorageFinder m_findNativeStorage;
        NativeWatchHook m_hookNativeWatch;
        std::shared_ptr<const DynamicComponentType> m_dynamic;
        std::size_t m_size;
        std::size_t m_alignment;
    };

    inline ComponentType ComponentType::Dynamic(DynamicComponentType type)
    {
        const std::size_t size = type.Size();
        const std::size_t alignment = type.Alignment();
        return ComponentType(nullptr, nullptr, std::make_shared<const DynamicComponentType>(std::move(type)), size,
                             alignment);
    }
} // namespace Fenrir
