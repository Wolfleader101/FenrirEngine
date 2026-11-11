using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;

namespace RendererScriptFixture;

// The culling switches, the level-of-detail thresholds and the frustum as C# calls them (Culling.cpp, ADR-0057): each
// setter's result, a camera's Frustum read by value, echoed back, and tested against boxes and spheres with the
// hand-written Intersects, which the tests compare with Math::Frustum's.
public static class CullingCalls
{
    public static bool GpuCulling(bool on) => Renderer.SetGpuCulling(on);

    public static bool OcclusionCulling(bool on) => Renderer.SetOcclusionCulling(on);

    public static bool CullingFrozen(bool on) => Renderer.SetCullingFrozen(on);

    public static bool LodSelection(float lodErrorPixels, float minProjectedPixels) =>
        Renderer.SetLodSelection(lodErrorPixels, minProjectedPixels);

    public static bool HasFrustum(uint camera) => Renderer.GetFrustum(new Entity(camera), out _);

    // The frustum GetFrustum gave, or one with no planes when it gave none.
    public static Frustum GetFrustum(uint camera)
    {
        Renderer.GetFrustum(new Entity(camera), out Frustum frustum);
        return frustum;
    }

    // A Frustum as C++ passed it, back: each plane in its place.
    public static Frustum EchoFrustum(Frustum frustum) => frustum;

    public static int PlaneCount(Frustum frustum) => frustum.Count;

    public static bool IntersectsBox(Frustum frustum, Bounds box) => frustum.Intersects(box);

    public static bool IntersectsMoved(Frustum frustum, Bounds box, Matrix4x4 transform) =>
        frustum.Intersects(box, transform);

    public static bool IntersectsSphere(Frustum frustum, Vector3 centre, float radius) =>
        frustum.Intersects(centre, radius);

    // What the cull passes drew, as the last frame read it back.
    public static uint InstancesDrawn()
    {
        Renderer.Stats(out RendererStats stats);
        return stats.InstancesDrawn;
    }
}
