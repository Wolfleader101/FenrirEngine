// cgltf's half of the model importer seam: glTF and GLB files (ADR-0031).
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <cgltf.h>

#include "Importers/ModelImporter.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // cgltf hands out its arrays C's way, as a pointer and a count; this is the one place they are trusted.
        template <typename T>
        std::span<T> CArray(T* items, std::size_t count)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return items != nullptr ? std::span<T>(items, count) : std::span<T>();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        std::filesystem::path Utf8Path(std::string_view text)
        {
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        std::string Utf8Text(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }

        // cgltf reads files through these, so paths are UTF-8 on every platform (its own reader uses fopen, which
        // takes the ANSI code page on Windows).
        cgltf_result ReadFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path,
                              cgltf_size* size, void** data)
        {
            std::ifstream stream(Utf8Path(path), std::ios::binary);
            if (!stream)
            {
                return cgltf_result_file_not_found;
            }
            const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            if (stream.bad())
            {
                return cgltf_result_io_error;
            }
            void* copy = std::malloc(bytes.empty() ? 1 : bytes.size());
            if (copy == nullptr)
            {
                return cgltf_result_out_of_memory;
            }
            std::memcpy(copy, bytes.data(), bytes.size());
            *size = bytes.size();
            *data = copy;
            return cgltf_result_success;
        }

        void ReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data)
        {
            std::free(data);
        }

        std::string_view ResultText(cgltf_result result)
        {
            switch (result)
            {
            case cgltf_result_data_too_short:
                return "the file ends early";
            case cgltf_result_unknown_format:
                return "the file is not glTF or GLB";
            case cgltf_result_invalid_json:
                return "its JSON is malformed";
            case cgltf_result_invalid_gltf:
                return "it breaks the glTF specification";
            case cgltf_result_invalid_options:
                return "cgltf was given invalid options";
            case cgltf_result_file_not_found:
                return "a file it names is missing";
            case cgltf_result_io_error:
                return "a file it names could not be read";
            case cgltf_result_out_of_memory:
                return "out of memory";
            case cgltf_result_legacy_gltf:
                return "it is glTF 1.0, and only glTF 2.0 is read";
            default:
                return "cgltf could not read it";
            }
        }

        struct DataDeleter
        {
            void operator()(cgltf_data* data) const noexcept
            {
                cgltf_free(data);
            }
        };

        std::string NameOf(const char* name)
        {
            return name != nullptr ? std::string(name) : std::string();
        }

        SamplerWrap WrapOf(cgltf_wrap_mode wrap)
        {
            if (wrap == cgltf_wrap_mode_clamp_to_edge)
            {
                return SamplerWrap::ClampToEdge;
            }
            return wrap == cgltf_wrap_mode_mirrored_repeat ? SamplerWrap::MirroredRepeat : SamplerWrap::Repeat;
        }

        // glTF filters are GL's enums: 9728 nearest, 9729 linear, 9984 to 9987 the mipmap modes.
        SamplerDescription SamplerOf(const cgltf_sampler* sampler)
        {
            SamplerDescription description;
            if (sampler == nullptr)
            {
                return description;
            }
            description.wrapU = WrapOf(sampler->wrap_s);
            description.wrapV = WrapOf(sampler->wrap_t);
            const int min = static_cast<int>(sampler->min_filter);
            const int mag = static_cast<int>(sampler->mag_filter);
            if (min == 9728 || min == 9984 || (min == 0 && mag == 9728))
            {
                description.filter = SamplerFilter::Nearest;
                description.anisotropy = 1;
            }
            else if (min == 9729 || min == 9985 || min == 9986)
            {
                description.filter = SamplerFilter::Bilinear;
            }
            return description;
        }

        Math::Vec4 Colour4(const cgltf_float (&rgba)[4])
        {
            const std::array<cgltf_float, 4> c = std::to_array(rgba);
            return {c[0], c[1], c[2], c[3]};
        }

        class GltfReader
        {
          public:
            GltfReader(const cgltf_data& data, const std::filesystem::path& file, ModelImport& out)
                : m_data(data), m_file(file), m_out(out)
            {
            }

            void Read(const cgltf_options& options)
            {
                ReadImages(options);
                for (const cgltf_material& material : CArray(m_data.materials, m_data.materials_count))
                {
                    m_out.materials.push_back(MaterialOf(material));
                }
                ReadMeshes();
                ReadNodes();
            }

          private:
            void ReadImages(const cgltf_options& options)
            {
                std::size_t i = 0;
                for (const cgltf_image& image : CArray(m_data.images, m_data.images_count))
                {
                    ImportedImage imported;
                    if (image.buffer_view != nullptr)
                    {
                        imported.name = std::format("embedded image {}", i);
                        const std::span<const std::byte> bytes =
                            CArray(reinterpret_cast<const std::byte*>(cgltf_buffer_view_data(image.buffer_view)),
                                   image.buffer_view->size);
                        if (bytes.empty())
                        {
                            imported.error = "its buffer view has no data";
                        }
                        else
                        {
                            imported.bytes.assign(bytes.begin(), bytes.end());
                        }
                    }
                    else if (image.uri != nullptr && std::string_view(image.uri).starts_with("data:"))
                    {
                        imported.name = std::format("embedded image {}", i);
                        imported.bytes = DecodeDataUri(options, image.uri, imported.error);
                    }
                    else if (image.uri != nullptr)
                    {
                        std::string uri(image.uri);
                        uri.resize(cgltf_decode_uri(uri.data()));
                        imported.name = uri;
                        imported.file = (m_file.parent_path() / Utf8Path(uri)).lexically_normal();
                    }
                    else
                    {
                        imported.name = std::format("image {}", i);
                        imported.error = "it names neither a file nor a buffer view";
                    }
                    m_out.images.push_back(std::move(imported));
                    ++i;
                }
            }

            static std::vector<std::byte> DecodeDataUri(const cgltf_options& options, const char* uri,
                                                        std::string& error)
            {
                const std::string_view text(uri);
                const std::size_t comma = text.find(";base64,");
                if (comma == std::string_view::npos)
                {
                    error = "its data URI is not base64";
                    return {};
                }
                const std::string_view base64 = text.substr(comma + 8);
                std::size_t size = base64.size() / 4 * 3;
                for (std::size_t i = base64.size(); i > 0 && base64[i - 1] == '='; --i)
                {
                    --size;
                }
                void* decoded = nullptr;
                if (cgltf_load_buffer_base64(&options, size, base64.data(), &decoded) != cgltf_result_success)
                {
                    error = "its data URI could not be decoded";
                    return {};
                }
                const std::span<const std::byte> bytes = CArray(static_cast<const std::byte*>(decoded), size);
                std::vector<std::byte> result(bytes.begin(), bytes.end());
                std::free(decoded);
                return result;
            }

            void AddTexture(ImportedMaterial& material, std::string parameter, const cgltf_texture_view& view)
            {
                if (view.texture == nullptr)
                {
                    return;
                }
                if (view.texture->image == nullptr)
                {
                    m_out.warnings.push_back(std::format("material '{}' has a {} with no image it can read; it is left "
                                                         "unset",
                                                         material.name, parameter));
                    return;
                }
                const auto image = static_cast<std::uint32_t>(cgltf_image_index(&m_data, view.texture->image));
                material.textures.push_back({std::move(parameter), image, SamplerOf(view.texture->sampler)});
            }

            ImportedMaterial MaterialOf(const cgltf_material& source)
            {
                ImportedMaterial material;
                material.name = NameOf(source.name);
                material.shader = source.unlit ? ImportedShader::Unlit : ImportedShader::Lit;
                material.doubleSided = source.double_sided;
                material.alphaCutoff = source.alpha_cutoff;
                if (source.alpha_mode == cgltf_alpha_mode_mask)
                {
                    material.blendMode = BlendMode::Masked;
                }
                else if (source.alpha_mode == cgltf_alpha_mode_blend)
                {
                    material.blendMode = BlendMode::AlphaBlend;
                }

                // cgltf fills glTF's defaults (base colour white, metallic 1, roughness 1, emissive black) for what
                // the file leaves out, so every factor is written whether or not the file gives it.
                const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
                material.factors.emplace_back("baseColour", Colour4(pbr.base_color_factor));
                AddTexture(material, "baseColourTexture", pbr.base_color_texture);
                if (material.shader == ImportedShader::Unlit)
                {
                    return material;
                }
                const float strength = source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;
                const std::array<cgltf_float, 3> emissive = std::to_array(source.emissive_factor);
                material.factors.emplace_back("metallic", pbr.metallic_factor);
                material.factors.emplace_back("roughness", pbr.roughness_factor);
                material.factors.emplace_back("emissive", Math::Vec4(emissive[0] * strength, emissive[1] * strength,
                                                                     emissive[2] * strength, 1.0f));
                AddTexture(material, "metallicRoughnessTexture", pbr.metallic_roughness_texture);
                AddTexture(material, "normalTexture", source.normal_texture);
                AddTexture(material, "emissiveTexture", source.emissive_texture);
                return material;
            }

            // glTF's default material, for primitives that name none: lit, white, metallic 1, roughness 1.
            std::uint32_t DefaultMaterial()
            {
                if (!m_defaultMaterial)
                {
                    ImportedMaterial material;
                    material.name = "Default";
                    material.factors = {{"baseColour", Math::Vec4(1.0f)},
                                        {"metallic", 1.0f},
                                        {"roughness", 1.0f},
                                        {"emissive", Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f)}};
                    m_defaultMaterial = static_cast<std::uint32_t>(m_out.materials.size());
                    m_out.materials.push_back(std::move(material));
                }
                return *m_defaultMaterial;
            }

            bool IsSkinned(const cgltf_mesh& mesh) const
            {
                for (const cgltf_node& node : CArray(m_data.nodes, m_data.nodes_count))
                {
                    if (node.mesh == &mesh && node.skin != nullptr)
                    {
                        return true;
                    }
                }
                for (const cgltf_primitive& primitive : CArray(mesh.primitives, mesh.primitives_count))
                {
                    if (cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0) != nullptr)
                    {
                        return true;
                    }
                }
                return false;
            }

            template <typename V>
            static std::vector<V> ReadStream(const cgltf_accessor* accessor, cgltf_size components)
            {
                std::vector<V> values;
                if (accessor == nullptr)
                {
                    return values;
                }
                values.resize(accessor->count);
                for (cgltf_size i = 0; i < accessor->count; ++i)
                {
                    std::array<float, 4> value = {0.0f, 0.0f, 0.0f, 1.0f};
                    cgltf_accessor_read_float(accessor, i, value.data(), components);
                    if constexpr (std::is_same_v<V, Colour>)
                    {
                        values[i] = Colour{value[0], value[1], value[2], value[3]};
                    }
                    else
                    {
                        for (cgltf_size c = 0; c < components; ++c)
                        {
                            values[i][static_cast<int>(c)] = value[c];
                        }
                    }
                }
                return values;
            }

            void ReadMeshes()
            {
                m_meshIndices.resize(m_data.meshes_count);
                std::size_t m = 0;
                for (const cgltf_mesh& mesh : CArray(m_data.meshes, m_data.meshes_count))
                {
                    const std::size_t index = m++;
                    const std::string name = NameOf(mesh.name);
                    if (IsSkinned(mesh))
                    {
                        m_out.warnings.push_back(std::format(
                            "mesh '{}' is skinned in its file and is left out: skinned meshes are not imported yet",
                            name));
                        continue;
                    }
                    for (const cgltf_primitive& primitive : CArray(mesh.primitives, mesh.primitives_count))
                    {
                        if (std::optional<ImportedMesh> imported = MeshOf(name, primitive))
                        {
                            m_meshIndices[index].push_back(static_cast<std::uint32_t>(m_out.meshes.size()));
                            m_out.meshes.push_back(std::move(*imported));
                        }
                    }
                }
            }

            std::optional<ImportedMesh> MeshOf(const std::string& name, const cgltf_primitive& primitive)
            {
                if (primitive.type != cgltf_primitive_type_triangles)
                {
                    m_out.warnings.push_back(
                        std::format("a primitive of mesh '{}' is not triangles and is left out", name));
                    return std::nullopt;
                }
                const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
                if (positions == nullptr)
                {
                    m_out.warnings.push_back(
                        std::format("a primitive of mesh '{}' has no positions and is left out", name));
                    return std::nullopt;
                }
                ImportedMesh mesh;
                mesh.name = name;
                MeshData& data = mesh.data;
                data.positions = ReadStream<Math::Vec3>(positions, 3);
                data.normals =
                    ReadStream<Math::Vec3>(cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0), 3);
                data.tangents =
                    ReadStream<Math::Vec4>(cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0), 4);
                data.uv0 = ReadStream<Math::Vec2>(cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0), 2);
                data.uv1 = ReadStream<Math::Vec2>(cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 1), 2);
                const cgltf_accessor* colours = cgltf_find_accessor(&primitive, cgltf_attribute_type_color, 0);
                data.colours =
                    ReadStream<Colour>(colours, colours != nullptr && colours->type == cgltf_type_vec3 ? 3 : 4);
                if (primitive.indices != nullptr)
                {
                    data.indices.resize(primitive.indices->count);
                    for (cgltf_size i = 0; i < primitive.indices->count; ++i)
                    {
                        data.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
                    }
                }
                else
                {
                    data.indices.resize(data.positions.size());
                    for (std::size_t i = 0; i < data.indices.size(); ++i)
                    {
                        data.indices[i] = static_cast<std::uint32_t>(i);
                    }
                }
                mesh.material = primitive.material != nullptr
                                    ? static_cast<std::uint32_t>(cgltf_material_index(&m_data, primitive.material))
                                    : DefaultMaterial();
                data.submeshes = {{0, static_cast<std::uint32_t>(data.indices.size()), mesh.material}};
                return mesh;
            }

            void ReadNodes()
            {
                std::vector<bool> visited(m_data.nodes_count, false);
                const cgltf_scene* scene = m_data.scene;
                if (scene == nullptr && m_data.scenes_count > 0)
                {
                    scene = &m_data.scenes[0];
                }
                if (scene != nullptr)
                {
                    for (const cgltf_node* node : CArray(scene->nodes, scene->nodes_count))
                    {
                        Visit(*node, -1, visited);
                    }
                }
                // Every node is kept, those outside the scene too, after the scene's.
                for (const cgltf_node& node : CArray(m_data.nodes, m_data.nodes_count))
                {
                    if (node.parent == nullptr)
                    {
                        Visit(node, -1, visited);
                    }
                }
            }

            void Visit(const cgltf_node& node, std::int32_t parent, std::vector<bool>& visited)
            {
                const cgltf_size index = cgltf_node_index(&m_data, &node);
                if (visited[index])
                {
                    return;
                }
                visited[index] = true;
                ImportedNode imported;
                imported.name = NameOf(node.name);
                imported.sourceId = index;
                imported.parent = parent;
                if (node.has_matrix)
                {
                    Math::Mat4 matrix;
                    std::memcpy(&matrix[0][0], node.matrix, sizeof(node.matrix));
                    Math::Decompose(matrix, imported.rest.pos, imported.rest.rot, imported.rest.scale);
                }
                else
                {
                    if (node.has_translation)
                    {
                        const std::array<cgltf_float, 3> t = std::to_array(node.translation);
                        imported.rest.pos = {t[0], t[1], t[2]};
                    }
                    if (node.has_rotation)
                    {
                        // glTF writes x, y, z, w; glm's constructor takes w first.
                        const std::array<cgltf_float, 4> r = std::to_array(node.rotation);
                        imported.rest.rot = Math::Quat(r[3], r[0], r[1], r[2]);
                    }
                    if (node.has_scale)
                    {
                        const std::array<cgltf_float, 3> s = std::to_array(node.scale);
                        imported.rest.scale = {s[0], s[1], s[2]};
                    }
                }
                if (node.mesh != nullptr)
                {
                    imported.meshes = m_meshIndices[cgltf_mesh_index(&m_data, node.mesh)];
                }
                const auto self = static_cast<std::int32_t>(m_out.nodes.size());
                m_out.nodes.push_back(std::move(imported));
                for (const cgltf_node* child : CArray(node.children, node.children_count))
                {
                    Visit(*child, self, visited);
                }
            }

            const cgltf_data& m_data;
            const std::filesystem::path& m_file;
            ModelImport& m_out;
            std::optional<std::uint32_t> m_defaultMaterial;
            // Each glTF mesh's imported meshes, one per primitive kept.
            std::vector<std::vector<std::uint32_t>> m_meshIndices;
        };
    } // namespace

    ModelImport ImportGltf(const std::filesystem::path& file)
    {
        ModelImport model;
        cgltf_options options{};
        options.file.read = &ReadFile;
        options.file.release = &ReleaseFile;
        const std::string path = Utf8Text(file);

        cgltf_data* parsed = nullptr;
        cgltf_result result = cgltf_parse_file(&options, path.c_str(), &parsed);
        const std::unique_ptr<cgltf_data, DataDeleter> data(parsed);
        if (result == cgltf_result_success)
        {
            result = cgltf_load_buffers(&options, data.get(), path.c_str());
            if (result != cgltf_result_success)
            {
                model.error = std::format("its buffers could not be loaded: {}", ResultText(result));
                return model;
            }
            result = cgltf_validate(data.get());
        }
        if (result != cgltf_result_success)
        {
            model.error = std::format("cgltf: {}", ResultText(result));
            return model;
        }
        GltfReader(*data, file, model).Read(options);
        return model;
    }
} // namespace Fenrir::Detail
