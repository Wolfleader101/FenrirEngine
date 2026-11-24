using Fenrir.Assets;
using Fenrir.Math;

namespace AssetsScriptFixture;

// A loaded mesh's positions from C# (MeshPositions.cpp): the count asked with an empty span, every position read into a
// span of that size, a span too short for them, and a mesh whose CPU copy is gone. Handles cross as TestHandle packs
// them.
public static class PositionCalls
{
    public static int Count(ulong mesh) => Assets.GetMeshPositions(TestHandle.To<Mesh>(mesh), []);

    // Component `axis` (0 x, 1 y, 2 z) of position `at`, every position read into a span of the size the count said.
    public static float At(ulong mesh, int at, int axis)
    {
        AssetHandle<Mesh> handle = TestHandle.To<Mesh>(mesh);
        var positions = new Vector3[Assets.GetMeshPositions(handle, [])];
        Assets.GetMeshPositions(handle, positions);
        return Component(positions[at], axis);
    }

    // What GetMeshPositions gives for a span of `room` entries.
    public static int ReadInto(ulong mesh, int room)
    {
        Span<Vector3> positions = stackalloc Vector3[room];
        return Assets.GetMeshPositions(TestHandle.To<Mesh>(mesh), positions);
    }

    // Component `axis` of entry `at` of a span of `room` entries, each first set to -7 on every axis, after
    // GetMeshPositions reads into it.
    public static float EntryAfterRead(ulong mesh, int room, int at, int axis)
    {
        Span<Vector3> positions = stackalloc Vector3[room];
        positions.Fill(new Vector3(-7.0f, -7.0f, -7.0f));
        Assets.GetMeshPositions(TestHandle.To<Mesh>(mesh), positions);
        return Component(positions[at], axis);
    }

    private static float Component(Vector3 position, int axis) =>
        axis switch { 0 => position.X, 1 => position.Y, _ => position.Z };
}
