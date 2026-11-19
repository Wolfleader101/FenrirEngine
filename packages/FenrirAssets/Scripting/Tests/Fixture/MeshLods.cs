using Fenrir.Assets;
using Fenrir.Math;

namespace AssetsScriptFixture;

// A mesh's levels of detail from C# (MeshLods.cpp, ADR-0057): a grid mesh added here, its levels made by GenerateLods
// under settings the tests pass or C++'s defaults, a level built by hand from its own first triangles, and the levels'
// errors and indices read back. Handles cross as TestHandle packs them.
public static class LodCalls
{
    // A `cells` x `cells` grid of quads over x and y, two triangles a cell, with a gentle height so no step is
    // lossless, positions only: every interior vertex can collapse, so it simplifies far.
    public static ulong AddGrid(int cells)
    {
        int side = cells + 1;
        var positions = new Vector3[side * side];
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                positions[y * side + x] = new Vector3(x, y, 0.1f * MathF.Sin(x * 0.7f) * MathF.Cos(y * 0.5f));
        var indices = new uint[cells * cells * 6];
        int at = 0;
        for (int y = 0; y < cells; ++y)
        {
            for (int x = 0; x < cells; ++x)
            {
                uint corner = (uint)(y * side + x);
                uint right = corner + 1;
                uint up = corner + (uint)side;
                uint upRight = up + 1;
                indices[at++] = corner;
                indices[at++] = right;
                indices[at++] = upRight;
                indices[at++] = corner;
                indices[at++] = upRight;
                indices[at++] = up;
            }
        }
        return TestHandle.Of(Assets.AddMesh(positions, [], [], indices));
    }

    public static bool Generate(ulong mesh, float ratio, uint maxLevels, uint minTriangles, bool lockSeams) =>
        Assets.GenerateLods(TestHandle.To<Mesh>(mesh),
            new LodSettings { Ratio = ratio, MaxLevels = maxLevels, MinTriangles = minTriangles, LockSeams = lockSeams });

    public static bool GenerateDefault(ulong mesh) => Assets.GenerateLods(TestHandle.To<Mesh>(mesh));

    // A level of the mesh's own first two triangles, read back through GetMeshLodIndices, at `error`.
    public static bool AddLevel(ulong mesh, float error)
    {
        AssetHandle<Mesh> handle = TestHandle.To<Mesh>(mesh);
        Span<uint> first = stackalloc uint[6];
        if (Assets.GetMeshLodIndices(handle, 0, first) < first.Length)
            return false;
        return Assets.AddMeshLod(handle, first, error);
    }

    public static int LevelCount(ulong mesh) => Assets.GetMeshLodErrors(TestHandle.To<Mesh>(mesh), []);

    // Level `level + 1`'s error (the entry GetMeshLodErrors gives it); NaN past the last.
    public static float LevelError(ulong mesh, int level)
    {
        Span<float> errors = stackalloc float[16];
        int count = Assets.GetMeshLodErrors(TestHandle.To<Mesh>(mesh), errors);
        return level < count ? errors[level] : float.NaN;
    }

    public static int LevelIndexCount(ulong mesh, int level) => Assets.GetMeshLodIndices(TestHandle.To<Mesh>(mesh), level, []);

    // Index `at` of level `level`, every index of the level read into a span of the size the count said.
    public static uint LevelIndex(ulong mesh, int level, int at)
    {
        AssetHandle<Mesh> handle = TestHandle.To<Mesh>(mesh);
        var indices = new uint[Assets.GetMeshLodIndices(handle, level, [])];
        Assets.GetMeshLodIndices(handle, level, indices);
        return indices[at];
    }
}
