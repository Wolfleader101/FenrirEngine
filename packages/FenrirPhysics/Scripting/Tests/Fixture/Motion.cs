using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// The motion tests from a script, each a thin call the tests make through Iris. The hit read last is kept for the tests
// to read field by field.
public static class Motion
{
    private static Entity hitBody;
    private static Entity hitCollider;
    private static Vector3 hitPoint;
    private static Vector3 hitNormal;
    private static float hitScalar;

    private static Transform At(float x, float y, float z) =>
        new() { Position = new Vector3(x, y, z), Rotation = Quaternion.Identity, Scale = Vector3.One };

    private static QueryFilter Filter(uint ignoreBody, uint layers) =>
        new() { Layers = layers, IgnoreBody = World.Owned(ignoreBody) };

    // Moves a unit box from (x, y, z) by (dx, dy, dz).
    public static bool MoveBox(float x, float y, float z, float dx, float dy, float dz, uint ignoreBody, uint layers)
    {
        bool found = PhysicsWorld.MoveCast(new BoxShape(), At(x, y, z), new Vector3(dx, dy, dz), out MoveCastHit hit,
            Filter(ignoreBody, layers));
        (hitBody, hitCollider, hitPoint, hitNormal, hitScalar) = (hit.Body, hit.Collider, hit.Point, hit.Normal, hit.Distance);
        return found;
    }

    private static PenetrationHit[] penetrations = [];

    // The bodies a unit box at (x, y, z) overlaps, written to a span of `capacity`.
    public static int PenetrateBox(float x, float y, float z, int capacity, uint ignoreBody, uint layers)
    {
        Span<PenetrationHit> hits = stackalloc PenetrationHit[capacity];
        int count = PhysicsWorld.Penetrations(new BoxShape(), At(x, y, z), hits, Filter(ignoreBody, layers));
        penetrations = hits[..count].ToArray();
        return count;
    }

    public static uint PenetrationBody(int index) => penetrations[index].Body.Value;

    public static uint PenetrationCollider(int index) => penetrations[index].Collider.Value;

    // Makes overlap `index` the hit read last.
    public static void ReadPenetration(int index)
    {
        PenetrationHit hit = penetrations[index];
        (hitBody, hitCollider, hitPoint, hitNormal, hitScalar) = (hit.Body, hit.Collider, hit.Point, hit.Normal, hit.Depth);
    }

    private static Vector3 offset;

    // The offset that frees a unit box at (x, y, z), kept for the tests.
    public static void DepenetrateBox(float x, float y, float z, uint ignoreBody, uint layers) =>
        offset = PhysicsWorld.Depenetrate(new BoxShape(), At(x, y, z), Filter(ignoreBody, layers));

    public static float Offset(int axis) => axis switch { 0 => offset.X, 1 => offset.Y, _ => offset.Z };

    // The body a ray from the origin along +x hits first, or none.
    public static uint RaycastEast(uint ignoreBody, uint layers) =>
        PhysicsWorld.Raycast(Vector3.Zero, new Vector3(1, 0, 0), 100, out RaycastHit hit, Filter(ignoreBody, layers))
            ? hit.Body.Value
            : uint.MaxValue;

    public static uint HitBody() => hitBody.Value;

    public static uint HitCollider() => hitCollider.Value;

    // The point's x, y and z, the normal's, then the distance or depth.
    public static float HitValue(int value) => value switch
    {
        0 => hitPoint.X,
        1 => hitPoint.Y,
        2 => hitPoint.Z,
        3 => hitNormal.X,
        4 => hitNormal.Y,
        5 => hitNormal.Z,
        _ => hitScalar,
    };
}
