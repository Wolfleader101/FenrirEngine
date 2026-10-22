#include "Importers/ModelImporter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <string>

namespace Fenrir::Detail
{
    namespace
    {
        std::string LowerExtension(const std::filesystem::path& file)
        {
            std::string extension = file.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        bool IsGltf(const std::string& extension)
        {
            return extension == ".gltf" || extension == ".glb";
        }

        // Smooth normals, each the area-weighted sum of the faces that share the vertex (a triangle's cross product
        // is twice its area). A vertex no triangle uses, or whose faces cancel out, faces +Z.
        void GenerateNormals(MeshData& mesh)
        {
            mesh.normals.assign(mesh.positions.size(), Math::Vec3(0.0f));
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            {
                const std::uint32_t a = mesh.indices[i];
                const std::uint32_t b = mesh.indices[i + 1];
                const std::uint32_t c = mesh.indices[i + 2];
                const Math::Vec3 face =
                    glm::cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
                mesh.normals[a] += face;
                mesh.normals[b] += face;
                mesh.normals[c] += face;
            }
            for (Math::Vec3& normal : mesh.normals)
            {
                const float length = glm::length(normal);
                normal = length > 0.0f ? normal / length : Math::Vec3(0.0f, 0.0f, 1.0f);
            }
        }

        void Normalise(ModelImport& model)
        {
            for (ImportedNode& node : model.nodes)
            {
                const float length = glm::length(node.rest.rot);
                if (!(length > 0.0f) || !std::isfinite(length))
                {
                    model.warnings.push_back(
                        std::format("node '{}' has a rotation of no length; it is read as no rotation", node.name));
                    node.rest.rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
                    continue;
                }
                node.rest.rot = node.rest.rot / length;
            }
            for (ImportedMesh& mesh : model.meshes)
            {
                if (mesh.data.normals.empty() && !mesh.data.positions.empty())
                {
                    GenerateNormals(mesh.data);
                }
            }
        }
    } // namespace

    bool IsModelFile(const std::filesystem::path& file)
    {
        const std::string extension = LowerExtension(file);
        return IsGltf(extension) || extension == ".fbx" || extension == ".obj";
    }

    ModelImport ImportModel(const std::filesystem::path& file)
    {
        ModelImport model = IsGltf(LowerExtension(file)) ? ImportGltf(file) : ImportUfbx(file);
        if (model.error.empty())
        {
            Normalise(model);
        }
        return model;
    }
} // namespace Fenrir::Detail
