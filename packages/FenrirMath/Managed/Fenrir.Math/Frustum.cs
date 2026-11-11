namespace Fenrir.Math;

// The behaviour of the generated Frustum (Generated/Frustum.cs, from FenrirMath's Math::Frustum, ADR-0057): the planes
// that bound a view's volume, each a Vector4 of its unit inward normal and distance, which Renderer.GetFrustum fills.
// As in C++, the planes and their count are private, read through Count and the indexer, and a new Frustum has no
// planes, so everything is inside it. The tests mirror Math::Frustum's exactly, so a game culls its own chunks and
// cells as the renderer does, by the same arithmetic.
public partial struct Frustum
{
    /// <summary>How many planes bound the volume: five for a perspective camera, whose far plane is at infinity, six for
    /// an orthographic one, and none for a new Frustum.</summary>
    public readonly int Count => (int)count;

    /// <summary>Plane <paramref name="index"/> of <see cref="Count"/>, in C++'s order (left, right, bottom, top, near,
    /// then far): xyz its unit normal pointing into the volume, w its distance, so a point p is inside it when
    /// dot(xyz, p) + w &gt;= 0.</summary>
    public readonly Vector4 this[int index] =>
        (uint)index < (uint)Count ? planes[index] : throw new ArgumentOutOfRangeException(nameof(index));

    /// <summary>Whether any of the sphere is inside every plane: false only when it is wholly outside one.</summary>
    public readonly bool Intersects(Vector3 centre, float radius)
    {
        for (int i = 0; i < Count; ++i)
        {
            Vector4 plane = planes[i];
            if (Vector3.Dot(new Vector3(plane.X, plane.Y, plane.Z), centre) + plane.W < -radius)
                return false;
        }
        return true;
    }

    /// <summary>Whether <paramref name="box"/>, in world space, may be seen: false only when its eight corners are all
    /// outside one plane. A box outside the volume that straddles two planes at a corner is kept.</summary>
    public readonly bool Intersects(Bounds box) => Intersects(box, Matrix4x4.Identity);

    /// <summary>Whether <paramref name="box"/> under <paramref name="transform"/> may be seen: its eight corners are
    /// moved by the transform and the box is dropped only when every one is outside one plane. The test the renderer's
    /// cull shader repeats.</summary>
    public readonly bool Intersects(Bounds box, Matrix4x4 transform)
    {
        Span<Vector4> corners = stackalloc Vector4[8];
        for (int corner = 0; corner < 8; ++corner)
        {
            // The bits pick each axis's minimum or maximum in Math::Corners' order, so this is C++'s test corner for corner.
            var local = new Vector4((corner & 1) != 0 ? box.Max.X : box.Min.X, (corner & 2) != 0 ? box.Max.Y : box.Min.Y,
                                    (corner & 4) != 0 ? box.Max.Z : box.Min.Z, 1);
            Vector4 moved = transform.Column0 * local.X + transform.Column1 * local.Y + transform.Column2 * local.Z +
                            transform.Column3 * local.W;
            corners[corner] = new Vector4(moved.X, moved.Y, moved.Z, 1);
        }
        for (int i = 0; i < Count; ++i)
        {
            Vector4 plane = planes[i];
            bool allOutside = true;
            foreach (Vector4 corner in corners)
            {
                if (Vector4.Dot(plane, corner) >= 0)
                {
                    allOutside = false;
                    break;
                }
            }
            if (allOutside)
                return false;
        }
        return true;
    }

    public override readonly string ToString()
    {
        var text = new System.Text.StringBuilder("(");
        for (int i = 0; i < Count; ++i)
            text.Append(i == 0 ? "" : ", ").Append(planes[i]);
        return text.Append(')').ToString();
    }
}
