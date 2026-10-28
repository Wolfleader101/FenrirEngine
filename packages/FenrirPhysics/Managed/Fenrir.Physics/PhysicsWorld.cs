using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// The facade over the generated PhysicsNative (Generated/, from Scripting/src/PhysicsScripting.cpp). Its pass-throughs
// (the forces, ContactsOf, IsSleeping and Bounds) are generated beside it from the natives' @facade marks
// (Generated/PhysicsWorld.cs); this part holds what a description cannot say: the class doc, the queries' default
// filter, the values returned from an output, the Gravity property and the handle packing.

/// <summary>
/// The active scene's physics world, from C#: forces, queries, motion tests, body state, gravity, debug lines and
/// collision meshes, as <c>Fenrir::PhysicsWorld</c> and <c>Fenrir::Physics</c> offer them in C++ (whose documentation
/// says what each does).
/// </summary>
/// <remarks>
/// Nothing here throws. When the active scene has no physics world yet (one is made the first tick it has a body), the
/// entity has no body, or a collision mesh was released, a function does nothing and returns <c>false</c> or zero, and
/// what it gives back through an <c>out</c> parameter is its default: with no world, <see cref="MoveCast"/> returns
/// <c>false</c>, <see cref="Penetrations"/> 0 and <see cref="Depenetrate"/> a zero offset. Collision meshes are made and
/// released through the active scene's world too, so with no world <see cref="CreateCollisionMesh"/> gives the none
/// handle and <see cref="ReleaseCollisionMesh"/> returns <c>false</c>: make meshes once a scene has a body. In a debug
/// build of the engine a warning is logged once for each place in the engine's native code that noticed and each
/// reason (not once per C# call site), for as long as scripting runs. Like every native function, none of these makes a
/// structural change: forces are queued for the next step, and queries and motion tests read the world as the last
/// step left it.
/// <para>
/// <see cref="GetJointState"/> reads the zeroed state with no physics world or on an entity with no Joint, with that
/// warning. It also reads zeroed, with no extra warning, for an inactive joint (physics already warns once each time one
/// goes inactive) and for a joint made since the last step, until its first step. Joints need no wake call from C#: every change
/// to a Joint wakes both its bodies.
/// </para>
/// </remarks>
public static partial class PhysicsWorld
{
    /// <summary>
    /// The nearest body a ray from <paramref name="origin"/> along <paramref name="direction"/> (any length) hits within
    /// <paramref name="maxDistance"/>. A ray that starts inside a box, sphere, capsule or convex hull hits it at
    /// distance 0. A triangle mesh is hit only on its triangles' front faces.
    /// </summary>
    /// <returns>Whether it hit a body; <paramref name="hit"/> is default when it did not.</returns>
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
                               QueryFilter filter = default)
    {
        return PhysicsNative.Raycast(in origin, in direction, maxDistance, OrDefault(filter), out hit);
    }

    /// <summary>
    /// Writes the hits of a ray, as for <see cref="Raycast"/>, to <paramref name="hits"/>, nearest first, and returns how
    /// many it wrote. When there are more hits than the span holds, the nearest ones are written.
    /// </summary>
    public static int RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, Span<RaycastHit> hits,
                                 QueryFilter filter = default) =>
        PhysicsNative.RaycastAll(in origin, in direction, maxDistance, OrDefault(filter), hits);

    /// <summary>
    /// The first body <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is), touches when
    /// swept along <paramref name="direction"/> (any length) for up to <paramref name="maxDistance"/>. A shape that starts
    /// touching a body hits it at distance 0.
    /// </summary>
    /// <returns>Whether it touched a body; <paramref name="hit"/> is default when it did not.</returns>
    public static bool ShapeCast(CollisionShape shape, Transform pose, Vector3 direction, float maxDistance,
                                 out ShapeCastHit hit, QueryFilter filter = default)
    {
        return PhysicsNative.ShapeCast(in shape, in pose, in direction, maxDistance, OrDefault(filter), out hit);
    }

    /// <summary>
    /// Writes the hits of a shape cast, as for <see cref="ShapeCast"/>, to <paramref name="hits"/>, nearest first, and
    /// returns how many it wrote. When there are more hits than the span holds, the nearest ones are written.
    /// </summary>
    public static int ShapeCastAll(CollisionShape shape, Transform pose, Vector3 direction, float maxDistance,
                                   Span<ShapeCastHit> hits, QueryFilter filter = default) =>
        PhysicsNative.ShapeCastAll(in shape, in pose, in direction, maxDistance, OrDefault(filter), hits);

    /// <summary>
    /// How far <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is), can move along
    /// <paramref name="motion"/> (a displacement: its length is how far) and stay clear. Moved
    /// <see cref="MoveCastHit.Distance"/> along <paramref name="motion"/>, the shape keeps a small gap (the skin, a fixed
    /// 0.02 m) from what blocked it, measured along that body's normal: a glancing hit stops it further back than a
    /// head-on one. Bodies the shape starts inside or touching, and bodies it moves away from, do not block it, so a
    /// shape resting on the ground can slide along it. A shape a MoveCast placed keeps the skin's gap, so moving it on
    /// into what blocked it is blocked at once (distance 0). Free a shape that starts inside a body with
    /// <see cref="Depenetrate"/>. A triangle-mesh shape (<see cref="MeshShape"/>) finds nothing.
    /// </summary>
    /// <remarks>
    /// To move a shape as a body would, filter as it does:
    /// <c>new QueryFilter { Mask = cf.Mask, Layers = cf.Layers, IgnoreBody = me }</c>, with <c>cf</c> its CollisionFilter
    /// (or a new one).
    /// </remarks>
    /// <returns>Whether something blocks the motion; <paramref name="hit"/> is default when nothing does.</returns>
    public static bool MoveCast(CollisionShape shape, Transform pose, Vector3 motion, out MoveCastHit hit,
                                QueryFilter filter = default)
    {
        return PhysicsNative.MoveCast(in shape, in pose, in motion, OrDefault(filter), out hit);
    }

    /// <summary>
    /// Writes the colliders <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is),
    /// overlaps to <paramref name="hits"/>, each with its body, and returns how many it wrote, stopping when the span is full.
    /// </summary>
    public static int Overlap(CollisionShape shape, Transform pose, Span<OverlapHit> hits, QueryFilter filter = default) =>
        PhysicsNative.Overlap(in shape, in pose, OrDefault(filter), hits);

    /// <summary>
    /// Writes the colliders <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is),
    /// overlaps to <paramref name="hits"/>, each with its body, which way out of it and how deep, and returns how many it
    /// wrote. Deepest first, a collider once at its deepest; when there are more than the span holds, the deepest are
    /// written. A shape only touching a collider does not overlap it. A triangle-mesh shape (<see cref="MeshShape"/>)
    /// finds nothing. Filter like a body as for <see cref="MoveCast"/>.
    /// </summary>
    public static int Penetrations(CollisionShape shape, Transform pose, Span<PenetrationHit> hits,
                                   QueryFilter filter = default) =>
        PhysicsNative.Penetrations(in shape, in pose, OrDefault(filter), hits);

    /// <summary>
    /// The offset that moves <paramref name="shape"/>, placed at <paramref name="pose"/> (scaled as a collider is), out
    /// of everything it overlaps, leaving the skin's gap <see cref="MoveCast"/> leaves; zero when it overlaps nothing.
    /// Several overlaps are resolved together, over a few tries. Wedged in a crevice the shape may still overlap
    /// something once moved: check with <see cref="Penetrations"/> when that matters. A triangle-mesh shape
    /// (<see cref="MeshShape"/>) finds nothing. Filter like a body as for <see cref="MoveCast"/>.
    /// </summary>
    public static Vector3 Depenetrate(CollisionShape shape, Transform pose, QueryFilter filter = default)
    {
        PhysicsNative.Depenetrate(in shape, in pose, OrDefault(filter), out Vector3 offset);
        return offset;
    }

    /// <summary>
    /// Writes the colliders containing <paramref name="point"/> to <paramref name="hits"/>, each with its body, and returns
    /// how many it wrote, stopping when the span is full.
    /// </summary>
    public static int OverlapPoint(Vector3 point, Span<OverlapHit> hits, QueryFilter filter = default) =>
        PhysicsNative.OverlapPoint(in point, OrDefault(filter), hits);

    // The filter a query runs with. A parameter's default must be a constant, so a query given no filter gets C#'s
    // `default`, every field zero, which as written would find nothing: that one stands for a new filter, which finds
    // every body except triggers. The natives take the engine's QueryFilter as it is.
    private static QueryFilter OrDefault(QueryFilter filter) =>
        filter.Mask == 0 && filter.Layers == 0 && !filter.HitTriggers && filter.IgnoreBody.Value == 0
            ? new QueryFilter()
            : filter;

    /// <summary>
    /// How the joint on <paramref name="joint"/> sits and how hard it pulls, as of the last step. Only reads: nothing is
    /// written into its Joint. While a joint's bodies sleep, the force and torque keep the values they had when they fell
    /// asleep; while it is disabled they are 0. The pose readings are always current.
    /// </summary>
    public static JointState GetJointState(Entity joint)
    {
        PhysicsNative.GetJointState(joint, out JointState state);
        return state;
    }

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

    /// <summary>
    /// Chooses which categories of debug lines the world draws, from the next step on, into its scene's debug drawing in
    /// the <c>Physics</c> group. This is physics's only debug call: show, hide or clear that group, and draw shapes of
    /// your own, with <c>Fenrir.DebugDraw</c>'s <c>DebugDraw</c>.
    /// </summary>
    /// <returns>Whether there was a world to draw them.</returns>
    public static bool SetDebugDraw(PhysicsDebugCategories categories) => PhysicsNative.SetDebugDraw((uint)categories);

    /// <summary>
    /// Cooks a collision mesh from <paramref name="vertices"/> and, for <see cref="MeshShape"/>, the triangles
    /// <paramref name="indices"/> lists (three per triangle, counter-clockwise seen from the front). Copies what it needs.
    /// Cooking runs in the calling system. Indices may be empty for a mesh used only as a hull.
    /// </summary>
    /// <returns>
    /// The mesh, shared by every collider that refers to it in every scene; the none handle (and an error logged) when there
    /// are no vertices, the indices are not whole triangles or one is out of range.
    /// </returns>
    public static CollisionMesh CreateCollisionMesh(ReadOnlySpan<Vector3> vertices, ReadOnlySpan<uint> indices) =>
        new() { Id = PhysicsNative.CreateCollisionMesh(vertices, indices) };

    /// <summary>
    /// Releases a collision mesh made by <see cref="CreateCollisionMesh"/>. A collider still referring to it has its body
    /// removed from the simulation at the next tick, with a warning.
    /// </summary>
    /// <returns>Whether the mesh was alive.</returns>
    public static bool ReleaseCollisionMesh(CollisionMesh mesh) => PhysicsNative.ReleaseCollisionMesh(mesh.Id);
}
