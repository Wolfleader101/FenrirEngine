// Fenrir/CubeFaces.glsl: the renderer's own passes that draw into, or read every texel of, a cube texture's faces.
#ifndef FENRIR_CUBE_FACES_GLSL
#define FENRIR_CUBE_FACES_GLSL

// The world direction through the point `ndc` of face `face` (+X, -X, +Y, -Y, +Z, -Z), as GL and D3D lay a cubemap's
// faces out. Offscreen passes put row 0 at the top (NDC +y), which is a face's first row.
vec3 FenrirFaceDirection(uint face, vec2 ndc)
{
    float x = ndc.x;
    float y = ndc.y;
    switch (face)
    {
    case 0u:
        return vec3(1.0, y, -x);
    case 1u:
        return vec3(-1.0, y, x);
    case 2u:
        return vec3(x, 1.0, -y);
    case 3u:
        return vec3(x, -1.0, y);
    case 4u:
        return vec3(x, y, 1.0);
    default:
        return vec3(-x, y, -1.0);
    }
}

#endif
