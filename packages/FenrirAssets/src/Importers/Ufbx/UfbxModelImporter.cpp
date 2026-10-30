// ufbx's half of the model importer seam: FBX and OBJ files (ADR-0031). Skins come from ufbx's cluster binds and
// clips from its baking API; ResolveSkinning then applies the rules glTF shares.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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

        // FBX clips are baked to linear keys (ADR-0031): a stretch ufbx cannot keep linear (a cubic curve, or a
        // rotation, which FBX interpolates as Euler angles) is resampled at this many keys a second; keys the file
        // already gives at this rate or faster, and the ends of linear stretches, are kept as written.
        constexpr double ClipSampleRate = 30.0;
        // A stepped key becomes two keys this many seconds apart, as linear keys cannot step.
        constexpr double StepDuration = 0.001;
        // A skin's clusters agree on where its mesh was bound when their bind matrices match to this, relative to size.
        constexpr double BindAgreement = 1e-4;

        struct SceneDeleter
        {
            void operator()(ufbx_scene* scene) const noexcept
            {
                ufbx_free_scene(scene);
            }
        };

        struct BakedDeleter
        {
            void operator()(ufbx_baked_anim* baked) const noexcept
            {
                ufbx_free_baked_anim(baked);
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

        // ufbx's 3x4 affine matrix, column by column, as glm's column-major 4x4.
        Math::Mat4 Mat4Of(const ufbx_matrix& m)
        {
            Math::Mat4 out(1.0f);
            for (glm::length_t c = 0; c < 4; ++c)
            {
                out[c] = Math::Vec4(Vec3Of(m.cols[c]), c == 3 ? 1.0f : 0.0f);
            }
            return out;
        }

        bool Agree(const ufbx_matrix& a, const ufbx_matrix& b)
        {
            for (std::size_t c = 0; c < 4; ++c)
            {
                for (std::size_t r = 0; r < 3; ++r)
                {
                    const double x = a.cols[c].v[r];
                    const double y = b.cols[c].v[r];
                    if (!(std::abs(x - y) <= BindAgreement * std::max({1.0, std::abs(x), std::abs(y)})))
                    {
                        return false;
                    }
                }
            }
            return true;
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
            // A skinned mesh's four largest influences, by cluster index; all zero for a rigid one.
            std::array<std::uint32_t, 4> joints{};
            Math::Vec4 weights{0.0f};
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
                ReadSkinJoints();
                ReadClips();
            }

          private:
            // Each skin's joints are its clusters' bones, named once every node has its index. Its inverse binds are
            // ufbx's bind_to_world inverted: in ufbx's world, which the nodes' rest worlds compose to (ADR-0031).
            void ReadSkinJoints()
            {
                for (const auto& [index, deformer] : m_pendingSkins)
                {
                    ImportedSkin& skin = m_out.skins[index];
                    for (const ufbx_skin_cluster* cluster : CArray(deformer->clusters))
                    {
                        skin.joints.push_back(m_nodeIndices.at(cluster->bone_node->element_id));
                        skin.inverseBinds.push_back(Mat4Of(ufbx_matrix_invert(&cluster->bind_to_world)));
                    }
                }
            }

            void ReadClips()
            {
                ufbx_bake_opts options{};
                options.trim_start_time = true;
                options.resample_rate = ClipSampleRate;
                options.minimum_sample_rate = ClipSampleRate;
                options.step_handling = UFBX_BAKE_STEP_HANDLING_CUSTOM_DURATION;
                options.step_custom_duration = StepDuration;
                std::size_t a = 0;
                for (const ufbx_anim_stack* stack : CArray(m_scene.anim_stacks))
                {
                    ImportedClip clip;
                    clip.name = Text(stack->name);
                    if (clip.name.empty())
                    {
                        clip.name = std::format("animation {}", a);
                    }
                    ++a;
                    ufbx_error error{};
                    const std::unique_ptr<ufbx_baked_anim, BakedDeleter> baked(
                        ufbx_bake_anim(&m_scene, stack->anim, &options, &error));
                    if (baked == nullptr)
                    {
                        m_out.failures.push_back(
                            std::format("clip '{}' could not be baked: {}", clip.name,
                                        std::string_view(error.description.data, error.description.length)));
                        continue;
                    }
                    const std::span<ufbx_node* const> nodes = CArray(m_scene.nodes);
                    for (const ufbx_baked_node& bakedNode : CArray(baked->nodes))
                    {
                        const auto target = m_nodeIndices.find(nodes[bakedNode.typed_id]->element_id);
                        if (target == m_nodeIndices.end())
                        {
                            continue;
                        }
                        AddChannel(clip, target->second, ClipPath::Translation, CArray(bakedNode.translation_keys));
                        AddChannel(clip, target->second, ClipPath::Rotation, CArray(bakedNode.rotation_keys));
                        AddChannel(clip, target->second, ClipPath::Scale, CArray(bakedNode.scale_keys));
                    }
                    m_out.clips.push_back(std::move(clip));
                }
            }

            static Math::Vec4 KeyValue(const ufbx_baked_vec3& key)
            {
                return {Vec3Of(key.value), 0.0f};
            }

            static Math::Vec4 KeyValue(const ufbx_baked_quat& key)
            {
                return {static_cast<float>(key.value.x), static_cast<float>(key.value.y),
                        static_cast<float>(key.value.z), static_cast<float>(key.value.w)};
            }

            // Baked keys as one linear channel; a key whose time, as a float, does not follow the one before is
            // dropped, so that the times stay strictly increasing.
            template <typename Keys>
            static void AddChannel(ImportedClip& clip, std::uint32_t target, ClipPath path, const Keys& keys)
            {
                if (keys.empty())
                {
                    return;
                }
                ClipChannel channel;
                channel.path = path;
                channel.interpolation = ClipInterpolation::Linear;
                for (const auto& key : keys)
                {
                    const auto time = static_cast<float>(key.time);
                    if (!channel.times.empty() && !(time > channel.times.back()))
                    {
                        continue;
                    }
                    channel.times.push_back(time);
                    channel.values.push_back(KeyValue(key));
                }
                clip.data.channels.push_back(std::move(channel));
                clip.targets.push_back(target);
            }
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
                const ufbx_skin_deformer* skin =
                    mesh.skin_deformers.count > 0 ? CArray(mesh.skin_deformers).front() : nullptr;
                std::int32_t skinIndex = -1;
                ufbx_matrix bindToWorld = ufbx_identity_matrix;
                if (skin != nullptr)
                {
                    if (mesh.skin_deformers.count > 1)
                    {
                        m_out.warnings.push_back(std::format("mesh '{}' has {} skins; only its first is read", name,
                                                             mesh.skin_deformers.count));
                    }
                    const std::optional<ufbx_matrix> bound = MeshBindToWorld(name, *skin);
                    if (!bound)
                    {
                        return indices;
                    }
                    bindToWorld = *bound;
                    skinIndex = static_cast<std::int32_t>(m_out.skins.size());
                    ImportedSkin imported;
                    imported.name = skin->name.length > 0 ? Text(skin->name) : name;
                    m_out.skins.push_back(std::move(imported));
                    m_pendingSkins.emplace_back(static_cast<std::size_t>(skinIndex), skin);
                }
                for (const ufbx_mesh_part& part : CArray(mesh.material_parts))
                {
                    if (part.num_triangles == 0)
                    {
                        continue;
                    }
                    ImportedMesh imported = MeshOf(name, mesh, part, skin);
                    if (skin != nullptr)
                    {
                        // Into ufbx's world at the bind pose, where the skin's inverse binds take it (ADR-0031).
                        imported.skin = skinIndex;
                        BakeVertices(imported.data, Mat4Of(bindToWorld));
                    }
                    indices.push_back(static_cast<std::uint32_t>(m_out.meshes.size()));
                    m_out.meshes.push_back(std::move(imported));
                }
                return indices;
            }

            // Where the skin's mesh was bound in ufbx's world: bind_to_world x geometry_to_bone, which every cluster
            // must agree on. A cluster with no bone, or clusters that disagree, fail the skin with its mesh.
            std::optional<ufbx_matrix> MeshBindToWorld(const std::string& name, const ufbx_skin_deformer& skin)
            {
                std::optional<ufbx_matrix> bound;
                for (const ufbx_skin_cluster* cluster : CArray(skin.clusters))
                {
                    if (cluster->bone_node == nullptr)
                    {
                        m_out.failures.push_back(
                            std::format("the skin of mesh '{}' fails with its mesh: a cluster names no bone", name));
                        return std::nullopt;
                    }
                    const ufbx_matrix matrix = ufbx_matrix_mul(&cluster->bind_to_world, &cluster->geometry_to_bone);
                    if (!bound)
                    {
                        bound = matrix;
                    }
                    else if (!Agree(*bound, matrix))
                    {
                        m_out.failures.push_back(std::format("the skin of mesh '{}' fails with its mesh: its clusters "
                                                             "disagree on where the mesh was bound",
                                                             name));
                        return std::nullopt;
                    }
                }
                if (!bound)
                {
                    m_out.failures.push_back(
                        std::format("the skin of mesh '{}' fails with its mesh: it has no clusters", name));
                }
                return bound;
            }

            // A vertex's four largest influences, by cluster index; a negative or non-finite weight anywhere fails
            // the skin in ResolveSkinning.
            static void ReadInfluences(const ufbx_skin_deformer& skin, std::uint32_t vertex, Corner& corner)
            {
                if (vertex >= skin.vertices.count)
                {
                    return;
                }
                const ufbx_skin_vertex influence = CArray(skin.vertices)[vertex];
                const std::span<const ufbx_skin_weight> weights =
                    CArray(skin.weights).subspan(influence.weight_begin, influence.num_weights);
                std::vector<ufbx_skin_weight> sorted(weights.begin(), weights.end());
                std::stable_sort(
                    sorted.begin(), sorted.end(),
                    [](const ufbx_skin_weight& a, const ufbx_skin_weight& b) { return a.weight > b.weight; });
                bool malformed = false;
                for (std::size_t i = 0; i < sorted.size(); ++i)
                {
                    const auto weight = static_cast<float>(sorted[i].weight);
                    malformed = malformed || !(weight >= 0.0f) || !std::isfinite(weight);
                    if (i < 4)
                    {
                        corner.joints[i] = sorted[i].cluster_index;
                        corner.weights[static_cast<glm::length_t>(i)] = weight;
                    }
                }
                if (malformed)
                {
                    corner.weights[0] = -1.0f;
                }
            }

            ImportedMesh MeshOf(const std::string& name, const ufbx_mesh& mesh, const ufbx_mesh_part& part,
                                const ufbx_skin_deformer* skin)
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
                        if (skin != nullptr)
                        {
                            ReadInfluences(*skin, CArray(mesh.vertex_indices)[index], corner);
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
                    if (skin != nullptr)
                    {
                        imported.joints.push_back(corner.joints);
                        imported.weights.push_back(corner.weights);
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
                imported.carriesContent = node.mesh != nullptr || node.camera != nullptr || node.light != nullptr;
                imported.markedBone = node.bone != nullptr;
                if (node.mesh != nullptr)
                {
                    imported.meshes = MeshesOf(*node.mesh);
                }
                const auto self = static_cast<std::int32_t>(m_out.nodes.size());
                m_nodeIndices.emplace(node.element_id, static_cast<std::uint32_t>(self));
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
            // Each node's index, by ufbx element id; ufbx's own root has none.
            std::unordered_map<std::uint32_t, std::uint32_t> m_nodeIndices;
            // Each skin read, by index into ModelImport::skins, with its deformer, its joints named once all nodes are.
            std::vector<std::pair<std::size_t, const ufbx_skin_deformer*>> m_pendingSkins;
        };
    } // namespace

    ModelImport ImportUfbx(const std::filesystem::path& file)
    {
        ModelImport model;
        ufbx_load_opts options{};
        // Into the engine's space: right-handed, +Y up, metres. The axis and unit conversion is an adjustment on the
        // file's root nodes only (ADR-0031), so it sits above every skeleton owner. Nodes below keep their own
        // transforms and rigid meshes their own vertices, in the file's units; ufbx's world, which skin binds and
        // skinned vertices are read in, includes the conversion.
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0;
        options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
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
