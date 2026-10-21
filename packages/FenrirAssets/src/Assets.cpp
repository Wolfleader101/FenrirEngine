#include "FenrirAssets/Assets.hpp"

#include "FenrirApp/App.hpp"

#include <cmath>

namespace Fenrir
{
    Assets& Assets::Install(App& app)
    {
        return app.AddResource<Assets>();
    }

    namespace
    {
        void Bounds(MeshData& mesh)
        {
            if (mesh.vertices.empty())
            {
                return;
            }
            mesh.boundsMin = mesh.boundsMax = mesh.vertices[0].position;
            for (const Vertex& v : mesh.vertices)
            {
                mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
                mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
            }
        }
    } // namespace

    MeshData Primitives::Cube(float h)
    {
        MeshData mesh;
        // One face per axis direction: normal, and two in-plane axes u, v with u x v = normal (so CCW from outside).
        const Math::Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const Math::Vec3& n : normals)
        {
            const Math::Vec3 u = std::abs(n.y) > 0.5f ? Math::Vec3(1, 0, 0) * n.y : Math::Vec3(-n.z, 0, n.x);
            const Math::Vec3 v = glm::cross(n, u);
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (const auto& c : corners)
            {
                Vertex vertex;
                vertex.position = (n + u * c[0] + v * c[1]) * h;
                vertex.normal = n;
                vertex.uv = {(c[0] + 1) * 0.5f, (1 - c[1]) * 0.5f};
                mesh.vertices.push_back(vertex);
            }
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        Bounds(mesh);
        return mesh;
    }

    MeshData Primitives::Sphere(float radius, int segments, int rings)
    {
        MeshData mesh;
        const float pi = 3.14159265f;
        for (int r = 0; r <= rings; ++r)
        {
            const float phi = pi * static_cast<float>(r) / static_cast<float>(rings); // 0 at the top
            for (int s = 0; s <= segments; ++s)
            {
                const float theta = 2 * pi * static_cast<float>(s) / static_cast<float>(segments);
                Vertex v;
                v.normal = {std::sin(phi) * std::cos(theta), std::cos(phi), -std::sin(phi) * std::sin(theta)};
                v.position = v.normal * radius;
                v.uv = {static_cast<float>(s) / static_cast<float>(segments),
                        static_cast<float>(r) / static_cast<float>(rings)};
                mesh.vertices.push_back(v);
            }
        }
        const auto stride = static_cast<std::uint32_t>(segments + 1);
        for (std::uint32_t r = 0; r < static_cast<std::uint32_t>(rings); ++r)
        {
            for (std::uint32_t s = 0; s < static_cast<std::uint32_t>(segments); ++s)
            {
                const std::uint32_t a = r * stride + s;
                const std::uint32_t b = a + stride;
                // a is above b; theta increases towards -Z from +X, which seen from outside runs right to left... CCW:
                mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        }
        Bounds(mesh);
        return mesh;
    }

    MeshData Primitives::Plane(float h, float uvRepeat)
    {
        MeshData mesh;
        const float corners[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}}; // x, z; seen from above, CCW
        for (const auto& c : corners)
        {
            Vertex v;
            v.position = {c[0] * h, 0.0f, c[1] * h};
            v.normal = {0, 1, 0};
            v.uv = {(c[0] + 1) * 0.5f * uvRepeat, (c[1] + 1) * 0.5f * uvRepeat};
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        Bounds(mesh);
        return mesh;
    }
} // namespace Fenrir
