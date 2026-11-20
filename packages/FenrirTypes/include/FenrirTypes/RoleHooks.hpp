#pragma once

namespace Fenrir
{
    class TypeRegistry;
    struct TypeDescription;

    /**
     * @brief The hooks a generated type registration hands each component and resource to once it registers, so the
     * package that knows the role hangs its attachments on the type (ADR-0074): FenrirECS a `ComponentType`,
     * FenrirScene and FenrirApp a resource's type-erased doors.
     *
     * Declared here, below every package that defines them, so a registration compiles against FenrirTypes alone. Until
     * those packages define them, each generated header defines the hooks its registration calls as no-ops returning
     * true. No other role has a hook.
     *
     * @return Whether the attachments were hung; the registry reports each it refused.
     */
    template <typename T>
    bool AttachComponent(TypeRegistry& registry, const TypeDescription& description);

    /** @brief The hook a generated registration hands each `scene-resource` type to; see `AttachComponent`. */
    template <typename T>
    bool AttachSceneResource(TypeRegistry& registry, const TypeDescription& description);

    /** @brief The hook a generated registration hands each `app-resource` type to; see `AttachComponent`. */
    template <typename T>
    bool AttachAppResource(TypeRegistry& registry, const TypeDescription& description);
} // namespace Fenrir
