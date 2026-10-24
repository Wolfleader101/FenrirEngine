// ufbx's half of the model importer seam: FBX and OBJ files (ADR-0031).
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ufbx.h>

#include "Importers/ModelImporter.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // ufbx hands out its arrays C's way, as a pointer and a count; this is the one place they are trusted.
        template <typename List>
        auto CArray(const List& list)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return list.data != nullptr ? std::span(list.data, list.count)
                                        : decltype(std::span(list.data, list.count))();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        // An embedded blob, the same way.
        std::span<const std::byte> CBytes(const ufbx_blob& blob)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return blob.data != nullptr ? std::span(static_cast<const std::byte*>(blob.data), blob.size)
                                        : std::span<const std::byte>();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        struct SceneDeleter
        {
            void operator()(ufbx_scene* scene) const noexcept
            {
                ufbx_free_scene(scene);
            }
        };

        std::string Text(ufbx_string text)
        {
            return {text.data, text.length};
        }

        std::filesystem::path Utf8Path(std::string_view text)
        {
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        Math::Vec3 Vec3Of(ufbx_vec3 v)
        {
            return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
        }

        SamplerWrap WrapOf(ufbx_wrap_mode wrap)
        {
            return wrap == UFBX_WRAP_CLAMP ? SamplerWrap::ClampToEdge : SamplerWrap::Repeat;
        }

        float Real(const ufbx_material_map& map, float fallback)
        {
            return map.has_value ? static_cast<float>(map.value_real) : fallback;
        }

        // One corner of a triangle, with every stream ufbx gave, so equal corners can be merged into one vertex.
        struct Corner
        {
            Math::Vec3 position{0.0f};
            Math::Vec3 normal{0.0f};
            Math::Vec4 tangent{0.0f};
            Math::Vec2 uv0{0.0f};
            Math::Vec2 uv1{0.0f};
            Colour colour;
        };

        class UfbxReader
        {
          public:
            UfbxReader(const ufbx_scene& scene, const std::filesystem::path& file, ModelImport& out)
                : m_scene(scene), m_file(file), m_out(out)
            {
            }

            void Read()
            {
                for (const ufbx_material* material : CArray(m_scene.materials))
                {
                    m_materialIndices.emplace(material->element_id, static_cast<std::uint32_t>(m_out.materials.size()));
                    m_out.materials.push_back(MaterialOf(*material));
                }
                // ufbx's root node is its own, not the file's: its children are the file's roots.
                const ufbx_node& root = *m_scene.root_node;
                for (const ufbx_node* child : CArray(root.children))
                {
                    Visit(*child, -1);
                }
            }

          private:
            std::uint32_t ImageOf(const ufbx_texture& texture)
            {
                const ufbx_texture& file = texture.file_textures.count > 0 ? *texture.file_textures.data[0] : texture;
                if (const auto found = m_imageIndices.find(file.element_id); found != m_imageIndices.end())
                {
                    return found->second;
                }
                ImportedImage image;
                image.name = Text(file.relative_filename.length > 0 ? file.relative_filename : file.filename);
                if (file.content.size > 0)
                {
                    const std::span<const std::byte> content = CBytes(file.content);
                    image.bytes.assign(content.begin(), content.end());
                }
                else if (file.relative_filename.length > 0 || file.filename.length > 0)
                {
                    // The path as written beside the model first, then as ufbx resolved it.
                    std::filesystem::path path = m_file.parent_path() / Utf8Path(Text(file.relative_filename));
                    std::error_code error;
                    if (file.relative_filename.length == 0 || !std::filesystem::is_regular_file(path, error))
                    {
                        const std::filesystem::path resolved = Utf8Path(Text(file.filename));
                        path = resolved.is_absolute() ? resolved : m_file.parent_path() / resolved;
                    }
                    image.file = path.lexically_normal();
                }
                else
                {
                    image.error = "its texture names no file";
                }
                const auto index = static_cast<std::uint32_t>(m_out.images.size());
                m_imageIndices.emplace(file.element_id, index);
                m_out.images.push_back(std::move(image));
                return index;
            }

            void AddTexture(ImportedMaterial& material, std::string parameter, const ufbx_material_map& map)
            {
                if (map.texture == nullptr)
                {
                    return;
                }
                SamplerDescription sampler;
                sampler.wrapU = WrapOf(map.texture->wrap_u);
                sampler.wrapV = WrapOf(map.texture->wrap_v);
                material.textures.push_back({std::move(parameter), ImageOf(*map.texture), sampler});
            }

            // ufbx maps every shading model (FBX Lambert and Phong, the PBR shaders, OBJ's MTL) onto its PBR maps,
            // with each model's own defaults, and those are what is written.
            ImportedMaterial MaterialOf(const ufbx_material& source)
            {
                const ufbx_material_pbr_maps& pbr = source.pbr;
                ImportedMaterial material;
                material.name = Text(source.name);
                material.shader = source.features.unlit.enabled ? ImportedShader::Unlit : ImportedShader::Lit;
                material.doubleSided = source.features.double_sided.enabled;

                // Opacity is MTL's `d` and the PBR shaders' own; classic FBX materials give a transparency factor.
                float alpha = 1.0f;
                if (pbr.opacity.has_value)
                {
                    alpha = static_cast<float>(pbr.opacity.value_vec3.x);
                }
                else if (source.fbx.transparency_factor.has_value)
                {
                    alpha = 1.0f - static_cast<float>(source.fbx.transparency_factor.value_real);
                }
                if (alpha < 1.0f)
                {
                    material.blendMode = BlendMode::AlphaBlend;
                }

                const float baseFactor = Real(pbr.base_factor, 1.0f);
                const ufbx_vec4 base = pbr.base_color.value_vec4;
                material.factors.emplace_back("baseColour", Math::Vec4(static_cast<float>(base.x) * baseFactor,
                                                                       static_cast<float>(base.y) * baseFactor,
                                                                       static_cast<float>(base.z) * baseFactor, alpha));
                AddTexture(material, "baseColourTexture", pbr.base_color);
                if (material.shader == ImportedShader::Unlit)
                {
                    return material;
                }
                const float emissionFactor = Real(pbr.emission_factor, 1.0f);
                const ufbx_vec4 emission = pbr.emission_color.value_vec4;
                material.factors.emplace_back("metallic", Real(pbr.metalness, 0.0f));
                material.factors.emplace_back("roughness", Real(pbr.roughness, 0.5f));
                material.factors.emplace_back("emissive",
                                              Math::Vec4(static_cast<float>(emission.x) * emissionFactor,
                                                         static_cast<float>(emission.y) * emissionFactor,
                                                         static_cast<float>(emission.z) * emissionFactor, 1.0f));
                AddTexture(material, "normalTexture", pbr.normal_map);
                AddTexture(material, "emissiveTexture", pbr.emission_color);
                return material;
            }

            // FBX and OBJ give no default material; a mesh part with none draws with a plain white lit one.
            std::uint32_t DefaultMaterial()
            {
                if (!m_defaultMaterial)
                {
                    ImportedMaterial material;
                    material.name = "Default";
                    material.factors = {{"baseColour", Math::Vec4(1.0f)},
                                        {"metallic", 0.0f},
                                        {"roughness", 0.5f},
                                        {"emissive", Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f)}};
                    m_defaultMaterial = static_cast<std::uint32_t>(m_out.materials.size());
                    m_out.materials.push_back(std::move(material));
                }
                return *m_defaultMaterial;
            }

            const std::vector<std::uint32_t>& MeshesOf(const ufbx_mesh& mesh)
            {
                if (const auto found = m_meshIndices.find(mesh.element_id); found != m_meshIndices.end())
                {
                    return found->second;
                }
                std::vector<std::uint32_t>& indices = m_meshIndices[mesh.element_id];
                const std::string name = Text(mesh.name);
                if (mesh.skin_deformers.count > 0)
                {
                    m_out.warnings.push_back(std::format(
                        "mesh '{}' is skinned in its file and is left out: skinned meshes are not imported yet", name));
                    return indices;
                }
                for (const ufbx_mesh_part& part : CArray(mesh.material_parts))
                {
                    if (part.num_triangles == 0)
                    {
                        continue;
                    }
                    ImportedMesh imported = MeshOf(name, mesh, part);
                    indices.push_back(static_cast<std::uint32_t>(m_out.meshes.size()));
                    m_out.meshes.push_back(std::move(imported));
                }
                return indices;
            }

            ImportedMesh MeshOf(const std::string& name, const ufbx_mesh& mesh, const ufbx_mesh_part& part)
            {
                const bool normals = mesh.vertex_normal.exists;
                const bool tangents = normals && mesh.vertex_tangent.exists && mesh.vertex_bitangent.exists;
                const std::span<const ufbx_uv_set> uvSets = CArray(mesh.uv_sets);
                const bool uv0 = uvSets.size() > 0;
                const bool uv1 = uvSets.size() > 1;
                const bool colours = mesh.vertex_color.exists;

                std::vector<Corner> corners;
                corners.reserve(part.num_triangles * 3);
                std::vector<std::uint32_t> triangle(mesh.max_face_triangles * 3);
                const std::span<const ufbx_face> faces = CArray(mesh.faces);
                for (const std::uint32_t f : CArray(part.face_indices))
                {
                    const ufbx_face face = faces[f];
                    const std::uint32_t count = ufbx_triangulate_face(triangle.data(), triangle.size(), &mesh, face);
                    for (std::uint32_t c = 0; c < count * 3; ++c)
                    {
                        const std::size_t index = triangle[c];
                        Corner corner;
                        corner.position = Vec3Of(ufbx_get_vertex_vec3(&mesh.vertex_position, index));
                        if (normals)
                        {
                            corner.normal = Vec3Of(ufbx_get_vertex_vec3(&mesh.vertex_normal, index));
                        }
                        if (tangents)
                        {
                            const Math::Vec3 tangent = Vec3Of(ufbx_get_vertex_vec3(&mesh.vertex_tangent, index));
                            const Math::Vec3 bitangent = Vec3Of(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, index));
                            const float sign =
                                glm::dot(glm::cross(corner.normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                            corner.tangent = Math::Vec4(tangent, sign);
                        }
                        // FBX and OBJ put UV (0, 0) at the image's bottom-left; the engine's images start at the top.
                        if (uv0)
                        {
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&uvSets[0].vertex_uv, index);
                            corner.uv0 = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
                        }
                        if (uv1)
                        {
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&uvSets[1].vertex_uv, index);
                            corner.uv1 = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
                        }
                        if (colours)
                        {
                            const ufbx_vec4 colour = ufbx_get_vertex_vec4(&mesh.vertex_color, index);
                            corner.colour = {static_cast<float>(colour.x), static_cast<float>(colour.y),
                                             static_cast<float>(colour.z), static_cast<float>(colour.w)};
                        }
                        corners.push_back(corner);
                    }
                }

                std::vector<std::uint32_t> indices(corners.size());
                const ufbx_vertex_stream stream{corners.data(), corners.size(), sizeof(Corner)};
                ufbx_error error{};
                corners.resize(ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &error));

                ImportedMesh imported;
                imported.name = name;
                const std::span<ufbx_material* const> materials = CArray(mesh.materials);
                const ufbx_material* material = part.index < materials.size() ? materials[part.index] : nullptr;
                imported.material =
                    material != nullptr ? m_materialIndices.at(material->element_id) : DefaultMaterial();
                MeshData& data = imported.data;
                for (const Corner& corner : corners)
                {
                    data.positions.push_back(corner.position);
                    if (normals)
                    {
                        data.normals.push_back(corner.normal);
                    }
                    if (tangents)
                    {
                        data.tangents.push_back(corner.tangent);
                    }
                    if (uv0)
                    {
                        data.uv0.push_back(corner.uv0);
                    }
                    if (uv1)
                    {
                        data.uv1.push_back(corner.uv1);
                    }
                    if (colours)
                    {
                        data.colours.push_back(corner.colour);
                    }
                }
                data.indices = std::move(indices);
                data.submeshes = {{0, static_cast<std::uint32_t>(data.indices.size()), imported.material}};
                return imported;
            }

            void Visit(const ufbx_node& node, std::int32_t parent)
            {
                ImportedNode imported;
                imported.name = Text(node.name);
                imported.sourceId = node.element_id;
                imported.parent = parent;
                const ufbx_transform& local = node.local_transform;
                imported.rest.pos = Vec3Of(local.translation);
                imported.rest.rot =
                    Math::Quat(static_cast<float>(local.rotation.w), static_cast<float>(local.rotation.x),
                               static_cast<float>(local.rotation.y), static_cast<float>(local.rotation.z));
                imported.rest.scale = Vec3Of(local.scale);
                if (node.mesh != nullptr)
                {
                    imported.meshes = MeshesOf(*node.mesh);
                }
                const auto self = static_cast<std::int32_t>(m_out.nodes.size());
                m_out.nodes.push_back(std::move(imported));
                for (const ufbx_node* child : CArray(node.children))
                {
                    Visit(*child, self);
                }
            }

            const ufbx_scene& m_scene;
            const std::filesystem::path& m_file;
            ModelImport& m_out;
            std::optional<std::uint32_t> m_defaultMaterial;
            // By ufbx element id: each material's and file texture's index, and each mesh's imported meshes.
            std::unordered_map<std::uint32_t, std::uint32_t> m_materialIndices;
            std::unordered_map<std::uint32_t, std::uint32_t> m_imageIndices;
            std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> m_meshIndices;
        };
    } // namespace

    ModelImport ImportUfbx(const std::filesystem::path& file)
    {
        ModelImport model;
        ufbx_load_opts options{};
        // Into the engine's space: right-handed, +Y up, metres, baked into the geometry and transforms so no
        // conversion node sits above the file's roots.
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0;
        options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
        options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
        options.obj_axes = ufbx_axes_right_handed_y_up;
        options.obj_unit_meters = 1.0;
        // An OBJ file's materials are in the .mtl file it names; a missing one leaves its materials empty.
        options.load_external_files = true;
        options.ignore_missing_external_files = true;

        const std::u8string path = file.u8string();
        ufbx_error error{};
        const std::unique_ptr<ufbx_scene, SceneDeleter> scene(
            ufbx_load_file_len(reinterpret_cast<const char*>(path.data()), path.size(), &options, &error));
        if (scene == nullptr)
        {
            model.error = std::format("ufbx: {}", std::string_view(error.description.data, error.description.length));
            return model;
        }
        UfbxReader(*scene, file, model).Read();
        return model;
    }
} // namespace Fenrir::Detail
