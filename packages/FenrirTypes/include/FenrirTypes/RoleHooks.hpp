#pragma once

namespace Fenrir
{
    class TypeRegistry;
    struct TypeDescription;

    /**
     * @brief The hooks a generated type registration hands each component and resource to once it registers, so the
     * package that knows the role hangs its attachments on the type (ADR-0074): FenrirECS a `ComponentType`,
     * FenrirScene and FenrirApp a resource's type-erased doors. No other role has a hook.
     *
     * Declared here, below every package that defines them, so a registration compiles against FenrirTypes alone. A
     * package that defines a hook does so in a header that also defines the hook's macro
     * (`FENRIR_TYPES_ATTACH_COMPONENT_DEFINED`, `FENRIR_TYPES_ATTACH_SCENE_RESOURCE_DEFINED`,
     * `FENRIR_TYPES_ATTACH_APP_RESOURCE_DEFINED`), and a registration that calls the hook lists that header in its
     * `[registration] includes`. A generated header defines each hook its registration calls whose macro is not defined
     * by then as a no-op returning true, so a package registers before the defining package exists.
     *
     * @return Whether the attachments were hung; false makes the registration function return false.
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
