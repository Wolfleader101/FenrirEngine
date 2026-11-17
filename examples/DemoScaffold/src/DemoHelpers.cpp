#include "FenrirDemoScaffold/DemoScaffold.hpp"

namespace FenrirDemo
{
    using Fenrir::Math::Vec3;
    using Fenrir::Math::Vec4;

    Fenrir::Math::Quat YawPitch(float yaw, float pitch)
    {
        // Yaw outermost, so the pitch turns about the X the yaw has already turned.
        return Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) *
               Fenrir::Math::AngleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
    }

    Fenrir::MeshData ScreenQuad(float width, float height, float repeat)
    {
        Fenrir::MeshData mesh;
        const float w = width * 0.5f;
        const float h = height * 0.5f;
        mesh.positions = {Vec3(-w, -h, 0.0f), Vec3(w, -h, 0.0f), Vec3(w, h, 0.0f), Vec3(-w, h, 0.0f)};
        mesh.normals = {Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1)};
        // u grows along +X and v down -Y, so the bitangent lit.frag wants, up the image, is +Y: cross(+Z, +X) times +1.
        mesh.tangents = {Vec4(1, 0, 0, 1), Vec4(1, 0, 0, 1), Vec4(1, 0, 0, 1), Vec4(1, 0, 0, 1)};
        mesh.uv0 = {{0.0f, repeat}, {repeat, repeat}, {repeat, 0.0f}, {0.0f, 0.0f}};
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }
} // namespace FenrirDemo
