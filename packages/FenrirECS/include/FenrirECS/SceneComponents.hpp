#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Fenrir
{
    /**
     * @brief Tags an entity a scene file never holds: a writer skips it and its children (a free-fly camera, a debug
     * entity). An empty tag, undescribed and never registered, so it is reached only through Entity's typed
     * HasComponent, AddComponent and RemoveComponent (ComponentType::Native refuses an empty type), and C# cannot add
     * it (ADR-0078).
     */
    struct Transient
    {
    };

    /**
     * @brief One component a scene file held whose type the type registry lacked when the file was loaded, kept as it
     * was read so a save writes it back unchanged.
     */
    struct UnknownComponent
    {
        /** @brief The canonical type name the file spelled. */
        std::string typeName;
        /** @brief The type version the file recorded, 1 when it recorded none. */
        std::uint32_t version = 1;
        /**
         * @brief The component's entry as the file held it, `version` included, as FenrirSerialisation's EncodeTree
         * bytes, never read here.
         */
        std::vector<std::byte> tree;
    };

    /**
     * @brief The components of an entity's scene-file block whose types the type registry lacked at load, in file
     * order, kept opaque and written back unchanged. Undescribed: a load fills it and a save reads it, and it never
     * becomes a component of the type when that type is registered later (ADR-0078).
     */
    struct UnknownComponents
    {
        /** @brief One per opaque component, in the order the file held them. */
        std::vector<UnknownComponent> components;
    };

    /**
     * @brief Marks a scene instance's root: the entities under it were made from another scene file or a model file,
     * and the host's file writes them as that source plus overrides (ADR-0080). A file spells it as the block's
     * `instance`.
     */
    struct Instance
    {
        /** @brief The source's asset path, relative to the asset root, zero-terminated (so at most 255 bytes). */
        char source[256] = {0};
    };
} // namespace Fenrir
