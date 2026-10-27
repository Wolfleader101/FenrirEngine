#pragma once

#include <cstdint>

namespace Fenrir
{
    /**
     * @brief Why a lookup of a component, event or resource by name gave no id. A lookup native returns the id when
     * it found one, otherwise one of these; the binding's generated enum lets C# name the reason it throws for.
     * Described for the C# binding (api/FenrirScripting.api.toml), so the values below are the contract's.
     */
    enum class RegistryError : std::int32_t
    {
        /** Nothing is registered under the name. */
        NotRegistered = -1,
        /** The name is registered for a different C# type than the one asking, so the layout check would be skipped. */
        WrongType = -2,
    };

    /**
     * @brief What a script asked for that the change buffer refused at once: the result of every native that records
     * a structural change. C# throws on anything but None, with a message that says which change was attempted, so
     * one value serves a component and a resource alike. Described for the C# binding, so the values are the
     * contract's.
     */
    enum class ChangeError : std::int32_t
    {
        /** The change was recorded, to play back when the system returns. */
        None = 0,
        /** The entity is not alive in the active scene. */
        EntityNotAlive = -1,
        /** No component, event or resource has the id. */
        Unknown = -2,
        /** Scripts may only read the component or resource, so they cannot add, remove, insert or remove it. */
        ReadOnly = -3,
        /** The engine needs every entity to have the component (Transform, Name, Relationship), so it cannot be
         * removed. */
        Required = -4,
        /** Scripting is not running, so there is no scene to change. */
        NotActive = -5,
    };
} // namespace Fenrir
