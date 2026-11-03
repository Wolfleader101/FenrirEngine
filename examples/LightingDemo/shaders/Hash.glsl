// Hash.glsl: the neon city's shaders' hash, so the facade's windows and the signs' glyphs vary from one integer seed.
#ifndef LIGHTING_DEMO_HASH_GLSL
#define LIGHTING_DEMO_HASH_GLSL

// 0 to 1, from four integers.
float Hash(uvec4 v)
{
    uint h = v.x * 0x8da6b343u ^ v.y * 0xd8163841u ^ v.z * 0xcb1ab31fu ^ v.w * 0x165667b1u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h) / 4294967295.0;
}

#endif
