using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// Physics components from a script. The tests call these through Iris, then play the changes back as the runner does
// after a system.
public static class Bodies
{
    // Makes the entity a dynamic sphere: a RigidBody with the C# defaults, and a collider whose shape is set by value.
    public static void MakeFallingSphere(uint entity, float radius)
    {
        var e = new Entity(entity);
        e.Add(new RigidBody());
        e.Add(new Collider { Shape = new SphereShape { Radius = radius } });
    }

    public static float Height(uint entity) => new Entity(entity).Get<Transform>().Value.Position.Y;

    // Edits the components in place: the box's height through its checked view, and the body's velocity and material.
    public static void Edit(uint entity, float halfHeight, float upSpeed, float friction)
    {
        var e = new Entity(entity);
        e.Get<Collider>().Value.Shape.Box.HalfExtents.Y = halfHeight;
        e.Get<RigidBody>().Value.LinearVelocity = new Vector3(0, upSpeed, 0);
        e.Add(new PhysicsMaterial { Friction = friction });
        e.Add(new CollisionFilter { Layers = 1u << 3, Mask = 1u << 4 });
        e.Add(new ContactEvents { Enabled = false });
    }

    // Reads the box's sphere view, which must throw: the exception's message, or null if it did not.
    public static string? WrongKindMessage(uint entity)
    {
        try
        {
            _ = new Entity(entity).Get<Collider>().Value.Shape.Sphere.Radius;
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }
}

// The physics world's native functions from a script, each a thin call the tests make through Iris.
public static class World
{
    public static bool AddImpulse(uint entity, float x, float y, float z) =>
        PhysicsWorld.AddImpulse(new Entity(entity), new Vector3(x, y, z));

    // The nearest hit of a ray, kept for the tests to read field by field.
    private static RaycastHit lastHit;

    public static bool Raycast(float x, float y, float z, float dx, float dy, float dz, float maxDistance) =>
        PhysicsWorld.Raycast(new Vector3(x, y, z), new Vector3(dx, dy, dz), maxDistance, out lastHit);

    // The entity of the active scene with this value, found as a script finds one, through a query (every entity has
    // a Transform), so it carries its scene: an entity named by id alone has none, and the engine would not match it
    // against a body. Entity.None when there is none.
    internal static Entity Owned(uint value)
    {
        foreach (var (entity, _) in Fenrir.Scripting.World.Query<Transform>())
            if (entity.Value == value)
                return entity;
        return Entity.None;
    }

    // A ray straight down from (0, y, 0) with every field of the filter given.
    public static uint RaycastDownFiltered(float y, uint mask, uint ignoreBody, bool hitTriggers)
    {
        var filter = new QueryFilter { Mask = mask, IgnoreBody = Owned(ignoreBody), HitTriggers = hitTriggers };
        return PhysicsWorld.Raycast(new Vector3(0, y, 0), new Vector3(0, -1, 0), 100, out RaycastHit hit, filter)
            ? hit.Body.Value
            : uint.MaxValue;
    }

    public static uint LastHitBody() => lastHit.Body.Value;

    public static uint LastHitCollider() => lastHit.Collider.Value;

    public static float LastHitDistance() => lastHit.Distance;

    public static float LastHitPointY() => lastHit.Point.Y;

    public static float LastHitNormalY() => lastHit.Normal.Y;

    // The hits RaycastAll wrote into a span of `capacity`, kept for the tests.
    private static RaycastHit[] allHits = [];

    public static int RaycastAll(float x, float y, float z, float dx, float dy, float dz, float maxDistance, int capacity)
    {
        Span<RaycastHit> hits = stackalloc RaycastHit[capacity];
        int count = PhysicsWorld.RaycastAll(new Vector3(x, y, z), new Vector3(dx, dy, dz), maxDistance, hits);
        allHits = hits[..count].ToArray();
        return count;
    }

    public static uint AllHitBody(int index) => allHits[index].Body.Value;

    public static float AllHitDistance(int index) => allHits[index].Distance;

    private static Transform At(float x, float y, float z) =>
        new() { Position = new Vector3(x, y, z), Rotation = Quaternion.Identity, Scale = Vector3.One };

    private static ShapeCastHit lastCast;

    // Sweeps a sphere of `radius` from (x, y, z) along (dx, dy, dz).
    public static bool SphereCast(float x, float y, float z, float radius, float dx, float dy, float dz, float maxDistance) =>
        PhysicsWorld.ShapeCast(new SphereShape { Radius = radius }, At(x, y, z), new Vector3(dx, dy, dz), maxDistance,
            out lastCast);

    public static uint LastCastBody() => lastCast.Body.Value;

    public static float LastCastDistance() => lastCast.Distance;

    public static float LastCastNormalY() => lastCast.Normal.Y;

    public static int SphereCastAll(float x, float y, float z, float radius, float dx, float dy, float dz, float maxDistance,
        int capacity)
    {
        Span<ShapeCastHit> hits = stackalloc ShapeCastHit[capacity];
        return PhysicsWorld.ShapeCastAll(new SphereShape { Radius = radius }, At(x, y, z), new Vector3(dx, dy, dz),
            maxDistance, hits);
    }

    private static OverlapHit[] overlaps = [];

    // The bodies a small box at (x, y, z) overlaps.
    public static int OverlapBox(float x, float y, float z, float halfExtent, int capacity)
    {
        Span<OverlapHit> hits = stackalloc OverlapHit[capacity];
        int count = PhysicsWorld.Overlap(new BoxShape { HalfExtents = new Vector3(halfExtent, halfExtent, halfExtent) },
            At(x, y, z), hits);
        overlaps = hits[..count].ToArray();
        return count;
    }

    public static int OverlapPoint(float x, float y, float z, int capacity)
    {
        Span<OverlapHit> hits = stackalloc OverlapHit[capacity];
        int count = PhysicsWorld.OverlapPoint(new Vector3(x, y, z), hits);
        overlaps = hits[..count].ToArray();
        return count;
    }

    public static uint OverlapBody(int index) => overlaps[index].Body.Value;

    public static uint OverlapCollider(int index) => overlaps[index].Collider.Value;

    public static float GravityY() => PhysicsWorld.Gravity.Y;

    public static void SetGravity(float x, float y, float z) => PhysicsWorld.Gravity = new Vector3(x, y, z);

    public static bool IsSleeping(uint entity) => PhysicsWorld.IsSleeping(new Entity(entity));

    private static Bounds lastBounds;

    public static bool Bounds(uint entity) => PhysicsWorld.Bounds(new Entity(entity), out lastBounds);

    public static float BoundsMinY() => lastBounds.Min.Y;

    public static float BoundsMaxY() => lastBounds.Max.Y;

    private static Contact[] contacts = [];

    public static int ContactsOf(uint entity, int capacity)
    {
        Span<Contact> found = stackalloc Contact[capacity];
        int count = PhysicsWorld.ContactsOf(new Entity(entity), found);
        contacts = found[..count].ToArray();
        return count;
    }

    public static uint ContactBody(int index) => contacts[index].Body.Value;

    public static uint ContactOtherBody(int index) => contacts[index].OtherBody.Value;

    public static uint ContactOtherCollider(int index) => contacts[index].OtherCollider.Value;

    public static float ContactNormalY(int index) => contacts[index].Normal.Y;

    public static float ContactPointY(int index) => contacts[index].Point.Y;

    public static bool DrawColliders() => PhysicsWorld.SetDebugDraw(PhysicsDebugCategories.Colliders);

    public static bool DrawJoints() => PhysicsWorld.SetDebugDraw(PhysicsDebugCategories.Joints);

    public static bool DrawCharacters() => PhysicsWorld.SetDebugDraw(PhysicsDebugCategories.Characters);

    public static bool DrawVehicles() => PhysicsWorld.SetDebugDraw(PhysicsDebugCategories.Vehicles);

    public static bool DrawAll() => PhysicsWorld.SetDebugDraw(PhysicsDebugCategories.All);

    // A 20 by 20 square at y = 0, its two triangles facing up, cooked from C# spans.
    public static ulong MakeGroundMesh()
    {
        ReadOnlySpan<Vector3> vertices =
        [
            new(-10, 0, -10), new(10, 0, -10), new(10, 0, 10), new(-10, 0, 10),
        ];
        ReadOnlySpan<uint> indices = [0, 2, 1, 0, 3, 2];
        return PhysicsWorld.CreateCollisionMesh(vertices, indices).Id;
    }

    // Three indices that are not a triangle of the one vertex given: refused, so the handle is none.
    public static ulong MakeBadMesh()
    {
        ReadOnlySpan<Vector3> vertices = [new(0, 0, 0)];
        ReadOnlySpan<uint> indices = [0, 1, 2];
        return PhysicsWorld.CreateCollisionMesh(vertices, indices).Id;
    }

    public static void GiveMeshCollider(uint entity, ulong mesh) =>
        new Entity(entity).Add(new Collider { Shape = new MeshShape { Mesh = new CollisionMesh { Id = mesh } } });

    public static bool ReleaseMesh(ulong mesh) => PhysicsWorld.ReleaseCollisionMesh(new CollisionMesh { Id = mesh });

    public static float GravityLength() => PhysicsWorld.Gravity.Length;

    public static bool AddForce(uint entity) => PhysicsWorld.AddForce(new Entity(entity), new Vector3(1, 0, 0));

    public static bool AddTorque(uint entity) => PhysicsWorld.AddTorque(new Entity(entity), new Vector3(1, 0, 0));

    public static bool AddAngularImpulse(uint entity) =>
        PhysicsWorld.AddAngularImpulse(new Entity(entity), new Vector3(1, 0, 0));
}
