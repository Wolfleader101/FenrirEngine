// Fenrir/Normals.glsl: unit normals packed into two signed components by the octahedral mapping (Cigolle et al. 2014,
// "A Survey of Efficient Representations for Independent Unit Vectors"), as the depth prepass writes view-space normals
// into its RG16F target (ADR-0049). Core/OctahedralNormals mirrors both functions on the CPU.
#ifndef FENRIR_NORMALS_GLSL
#define FENRIR_NORMALS_GLSL

// -1 for a negative component, else 1: sign() would give 0 on the axes and fold them away.
vec2 FenrirSignNotZero(vec2 v)
{
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// The unit vector n as a point of [-1, 1]^2: projected onto the octahedron |x| + |y| + |z| = 1, its lower half folded
// over the upper's corners.
vec2 FenrirOctEncode(vec3 n)
{
    vec2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
    return n.z >= 0.0 ? p : (1.0 - abs(p.yx)) * FenrirSignNotZero(p);
}

// The unit vector FenrirOctEncode made e from.
vec3 FenrirOctDecode(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0)
    {
        n.xy = (1.0 - abs(n.yx)) * FenrirSignNotZero(n.xy);
    }
    return normalize(n);
}

#endif
