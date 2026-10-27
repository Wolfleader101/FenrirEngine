#pragma once

// FenrirPhysics's scripting extension (ADR-0009), in its own library so FenrirPhysics never links FenrirScripting.
// Built only with FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_SCRIPTING on. The extension shares the Fenrir.Physics
// contract assembly, which fenrir_deploy_scripting(<target> EXTENSIONS Physics) copies next to an executable (beside
// the scripting contract, where Install looks for it).

#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief What physics gives C#: the physics components and events, the native functions of
     * `Fenrir.Physics.PhysicsWorld` and the `Fenrir.Physics` contract. Add it to ScriptingConfig::extensions:
     *
     *     auto physics = Fenrir::Physics::Install(app);
     *     auto scripting = Fenrir::Scripting::Install(app, {.scriptAssembly = ..., .extensions =
     *     {Fenrir::PhysicsScriptingExtension()}});
     *
     * Its native functions act on the active scene's physics world, collision meshes included (they are the Physics
     * owner's, reached through the world), so they need no owner. With no world, no body or a released mesh they do
     * nothing and return false or zero, and an output they write reads as its default.
     *
     * In debug builds each such case is warned about once per extension made here, for each native function that
     * notices it and each reason, under the native's name (`[physics scripting] PhysicsNative.Raycast: ...`): not once
     * per C# call site, so a second script calling the same function the same wrong way is not warned about again.
     * Each call makes a new extension with its own record, which lasts as long as the Scripting given it.
     */
    [[nodiscard]] ScriptingExtension PhysicsScriptingExtension();
} // namespace Fenrir
