using Fenrir.Scripting;

namespace Fenrir.Physics;

/// <summary>
/// The active scene's physics world, from C#: forces, queries, body state, gravity, debug lines and collision meshes,
/// as <c>Fenrir::PhysicsWorld</c> and <c>Fenrir::Physics</c> offer them in C++ (whose documentation says what each does).
/// </summary>
/// <remarks>
/// Nothing here throws. When the active scene has no physics world yet (one is made the first tick it has a body), the
/// entity has no body, or a collision mesh was released, a function does nothing and returns <c>false</c> or zero, and in
/// a debug build of the engine a warning is logged once per place in the engine that noticed. Like every native function,
/// none of these makes a structural change: forces are queued for the next step, and queries read the world as the last
/// step left it.
/// </remarks>
public static class PhysicsWorld
{
    /// <summary>Pushes the entity's body with <paramref name="force"/> (newtons, world space) for the next step only.</summary>
    /// <returns>Whether the force was queued: false with no physics world or no body.</returns>
    public static bool AddForce(Entity entity, Vector3 force) => PhysicsNative.AddForce(entity, in force);

    /// <summary>Changes the velocity of the entity's body by <paramref name="impulse"/> (newton seconds) over its mass, at the next step.</summary>
    /// <returns>Whether the impulse was queued: false with no physics world or no body.</returns>
    public static bool AddImpulse(Entity entity, Vector3 impulse) => PhysicsNative.AddImpulse(entity, in impulse);

    /// <summary>Twists the entity's body with <paramref name="torque"/> (newton metres, world space) for the next step only.</summary>
    /// <returns>Whether the torque was queued: false with no physics world or no body.</returns>
    public static bool AddTorque(Entity entity, Vector3 torque) => PhysicsNative.AddTorque(entity, in torque);

    /// <summary>Changes the angular velocity of the entity's body by <paramref name="impulse"/> over its moment of inertia, at the next step.</summary>
    /// <returns>Whether the impulse was queued: false with no physics world or no body.</returns>
    public static bool AddAngularImpulse(Entity entity, Vector3 impulse) =>
        PhysicsNative.AddAngularImpulse(entity, in impulse);

    /// <summary>
    /// The nearest body a ray from <paramref name="origin"/> along <paramref name="direction"/> (any length) hits within
    /// <paramref name="maxDistance"/>. A ray that starts inside a box or sphere hits it at distance 0.
    /// </summary>
    /// <returns>Whether it hit a body; <paramref name="hit"/> is default when it did not.</returns>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
                               QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        return PhysicsNative.Raycast(in origin, in direction, maxDistance, in chosen, out hit);
    }

    /// <summary>
    /// Writes the hits of a ray, as for <see cref="Raycast"/>, to <paramref name="hits"/>, nearest first, and returns how
    /// many it wrote. When there are more hits than the span holds, the nearest ones are written.
    /// </summary>
    public static unsafe int RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, Span<RaycastHit> hits,
                                        QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        fixed (RaycastHit* first = hits)
            return PhysicsNative.RaycastAll(in origin, in direction, maxDistance, in chosen, first, hits.Length);
    }

    /// <summary>
    /// The first body <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is), touches when
    /// swept along <paramref name="direction"/> (any length) for up to <paramref name="maxDistance"/>. A shape that starts
    /// touching a body hits it at distance 0.
    /// </summary>
    /// <returns>Whether it touched a body; <paramref name="hit"/> is default when it did not.</returns>
    public static bool ShapeCast(CollisionShape shape, Transform pose, Vector3 direction, float maxDistance,
                                 out ShapeCastHit hit, QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        return PhysicsNative.ShapeCast(in shape, in pose, in direction, maxDistance, in chosen, out hit);
    }

    /// <summary>
    /// Writes the hits of a shape cast, as for <see cref="ShapeCast"/>, to <paramref name="hits"/>, nearest first, and
    /// returns how many it wrote. When there are more hits than the span holds, the nearest ones are written.
    /// </summary>
    public static unsafe int ShapeCastAll(CollisionShape shape, Transform pose, Vector3 direction, float maxDistance,
                                          Span<ShapeCastHit> hits, QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        fixed (ShapeCastHit* first = hits)
            return PhysicsNative.ShapeCastAll(in shape, in pose, in direction, maxDistance, in chosen, first, hits.Length);
    }

    /// <summary>
    /// Writes the colliders <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is),
    /// overlaps to <paramref name="hits"/>, each with its body, and returns how many it wrote, stopping when the span is full.
    /// </summary>
    public static unsafe int Overlap(CollisionShape shape, Transform pose, Span<OverlapHit> hits, QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        fixed (OverlapHit* first = hits)
            return PhysicsNative.Overlap(in shape, in pose, in chosen, first, hits.Length);
    }

    /// <summary>
    /// Writes the colliders containing <paramref name="point"/> to <paramref name="hits"/>, each with its body, and returns
    /// how many it wrote, stopping when the span is full.
    /// </summary>
    public static unsafe int OverlapPoint(Vector3 point, Span<OverlapHit> hits, QueryFilter? filter = null)
    {
        QueryFilter chosen = filter ?? new QueryFilter();
        fixed (OverlapHit* first = hits)
            return PhysicsNative.OverlapPoint(in point, in chosen, first, hits.Length);
    }

    /// <summary>
    /// Writes the contacts the rigid body <paramref name="body"/> has now (as of the last step) to
    /// <paramref name="contacts"/>, and returns how many it wrote, stopping when the span is full. Overlaps with triggers
    /// are not contacts.
    /// </summary>
    public static unsafe int ContactsOf(Entity body, Span<Contact> contacts)
    {
        fixed (Contact* first = contacts)
            return PhysicsNative.ContactsOf(body, first, contacts.Length);
    }

    /// <summary>
    /// Whether the body of <paramref name="body"/> (a child collider's entity stands for its compound body) is asleep. False
    /// for an entity with no body.
    /// </summary>
    public static bool IsSleeping(Entity body) => PhysicsNative.IsSleeping(body);

    /// <summary>The world-space box around the body of <paramref name="body"/>.</summary>
    /// <returns>Whether the entity has a body; <paramref name="bounds"/> is default when it does not.</returns>
    public static bool Bounds(Entity body, out Aabb bounds) => PhysicsNative.Bounds(body, out bounds);

    /// <summary>
    /// The world's gravity, in metres per second squared. Setting it takes effect from the next step on and wakes the
    /// world's bodies. Zero, and setting it does nothing, while the active scene has no physics world.
    /// </summary>
    public static Vector3 Gravity
    {
        get
        {
            PhysicsNative.Gravity(out Vector3 gravity);
            return gravity;
        }
        set => PhysicsNative.SetGravity(in value);
    }

    /// <summary>Chooses which categories of debug lines the world draws, from the next step on.</summary>
    /// <returns>Whether there was a world to draw them.</returns>
    public static bool SetDebugDraw(DebugDraw categories) => PhysicsNative.SetDebugDraw((uint)categories);

    /// <summary>
    /// Cooks a collision mesh from <paramref name="vertices"/> and, for <see cref="MeshShape"/>, the triangles
    /// <paramref name="indices"/> lists (three per triangle, counter-clockwise seen from the front). Copies what it needs.
    /// Cooking runs in the calling system. Indices may be empty for a mesh used only as a hull.
    /// </summary>
    /// <returns>
    /// The mesh, shared by every collider that refers to it in every scene; the none handle (and an error logged) when there
    /// are no vertices, the indices are not whole triangles or one is out of range.
    /// </returns>
    public static unsafe CollisionMesh CreateCollisionMesh(ReadOnlySpan<Vector3> vertices, ReadOnlySpan<uint> indices)
    {
        fixed (Vector3* firstVertex = vertices)
        fixed (uint* firstIndex = indices)
            return new CollisionMesh { Id = PhysicsNative.CreateCollisionMesh(firstVertex, vertices.Length, firstIndex, indices.Length) };
    }

    /// <summary>
    /// Releases a collision mesh made by <see cref="CreateCollisionMesh"/>. A collider still referring to it has its body
    /// removed from the simulation at the next tick, with a warning.
    /// </summary>
    /// <returns>Whether the mesh was alive.</returns>
    public static bool ReleaseCollisionMesh(CollisionMesh mesh) => PhysicsNative.ReleaseCollisionMesh(mesh.Id);
}

// The native functions behind PhysicsWorld, each named "Fenrir.Physics.PhysicsNative.<Method>" in FenrirPhysicsScripting's
// extension. The generator writes each body and the field the host binds; sharing the contract fails if a method has no
// C++ function or a different signature. Internal: PhysicsWorld is what scripts call.
internal static unsafe partial class PhysicsNative
{
    [NativeFunction]
    public static partial bool AddForce(Entity entity, in Vector3 force);

    [NativeFunction]
    public static partial bool AddImpulse(Entity entity, in Vector3 impulse);

    [NativeFunction]
    public static partial bool AddTorque(Entity entity, in Vector3 torque);

    [NativeFunction]
    public static partial bool AddAngularImpulse(Entity entity, in Vector3 impulse);

    [NativeFunction]
    public static partial bool Raycast(in Vector3 origin, in Vector3 direction, float maxDistance, in QueryFilter filter,
                                       out RaycastHit hit);

    // The all-hit queries write at most `capacity` hits from `hits` on and return how many they wrote.
    [NativeFunction]
    public static partial int RaycastAll(in Vector3 origin, in Vector3 direction, float maxDistance, in QueryFilter filter,
                                         RaycastHit* hits, int capacity);

    [NativeFunction]
    public static partial bool ShapeCast(in CollisionShape shape, in Transform pose, in Vector3 direction, float maxDistance,
                                         in QueryFilter filter, out ShapeCastHit hit);

    [NativeFunction]
    public static partial int ShapeCastAll(in CollisionShape shape, in Transform pose, in Vector3 direction,
                                           float maxDistance, in QueryFilter filter, ShapeCastHit* hits, int capacity);

    [NativeFunction]
    public static partial int Overlap(in CollisionShape shape, in Transform pose, in QueryFilter filter, OverlapHit* hits,
                                      int capacity);

    [NativeFunction]
    public static partial int OverlapPoint(in Vector3 point, in QueryFilter filter, OverlapHit* hits, int capacity);

    [NativeFunction]
    public static partial int ContactsOf(Entity body, Contact* contacts, int capacity);

    [NativeFunction]
    public static partial bool IsSleeping(Entity body);

    [NativeFunction]
    public static partial bool Bounds(Entity body, out Aabb bounds);

    [NativeFunction]
    public static partial bool Gravity(out Vector3 gravity);

    [NativeFunction]
    public static partial bool SetGravity(in Vector3 gravity);

    [NativeFunction]
    public static partial bool SetDebugDraw(uint categories);

    // The mesh's id, 0 for none. `indices` may be null when `indexCount` is 0.
    [NativeFunction]
    public static partial ulong CreateCollisionMesh(Vector3* vertices, int vertexCount, uint* indices, int indexCount);

    [NativeFunction]
    public static partial bool ReleaseCollisionMesh(ulong mesh);
}
