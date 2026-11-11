#include "Core/SceneRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "Core/BuiltinShaders.hpp"
#include "Core/ColourGrading.hpp"
#include "Core/ImageReads.hpp"
#include "Core/ImageUpload.hpp"
#include "Core/Skinning.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The frame block, as `Fenrir/Frame.glsl` declares it (std140). */
        struct FrameBlock
        {
            Math::Vec4 lightDirection;
            Math::Vec4 lightColour;
            Math::Vec4 time;
            /** @brief The Sky drawn: its intensity, and the cosine and sine of its rotation. */
            Math::Vec4 sky;
            /** @brief The sky the scene is lit by: as `sky`, and its specular cube's highest mip. */
            Math::Vec4 skyLight;
            /**
             * @brief x: how many records the light buffer holds this frame; y: how many cast spot shadows, first; z:
             * how many cast point shadows, after them.
             */
            std::array<std::uint32_t, 4> lights{};
        };
        static_assert(sizeof(FrameBlock) == 96);

        /** @brief The view block, as `Fenrir/View.glsl` declares it (std140). */
        struct ViewBlock
        {
            Math::Mat4 view;
            Math::Mat4 projection;
            Math::Mat4 viewProjection;
            Math::Mat4 inverseViewProjection;
            Math::Vec4 cameraPosition;
            Math::Vec4 viewport;
            /** @brief The sun's cascades (ADR-0050): world to each one's tile of the shadow atlas. */
            std::array<Math::Mat4, MaxCascades> shadowMatrices{};
            /** @brief The view depth each cascade reaches to. */
            Math::Vec4 shadowSplits{0.0f};
            /** @brief The world size of each cascade's texels. */
            Math::Vec4 shadowTexels{0.0f};
            /** @brief Normal-offset and slope-scaled biases in texels, the filter's radius in texels, the blend share.
             */
            Math::Vec4 shadowBias{0.0f};
            /**
             * @brief x: how many cascades (0: no shadow); y: 1 to tint pixels by their cascade; z: in a point light's
             * shadow pass, its cube of the point shadow cubes.
             */
            std::array<std::uint32_t, 4> shadowInfo{};
            /** @brief Its light clusters (ADR-0049): the first slice's view depth and 24 / ln(far / near). */
            Math::Vec4 clusterSlices{0.0f};
            /**
             * @brief x: 1 when set 1 binding 3 holds its clusters; y: 1 to show their heat; z: each cluster's room for
             * light indices.
             */
            std::array<std::uint32_t, 4> clusterInfo{};
            /**
             * @brief Its reflections (ADR-0056): x 1 when it has them; y their intensity; z their roughness cutoff;
             * w the history's last mip.
             */
            Math::Vec4 reflectionInfo{0.0f};
        };
        static_assert(sizeof(ViewBlock) == 656);

        /** @brief One draw record, as `Fenrir/Draw.glsl` declares it (std430). */
        struct DrawBlock
        {
            Math::Mat4 world;
            Math::Mat4 normal;
            /** @brief A skinned item's first palette entry, in bones. */
            std::uint32_t paletteOffset = 0;
            /** @brief The sign of the world matrix's determinant: -1 when it mirrors, else 1. */
            float worldSign = 1.0f;
            /** @brief An instance list's first copy in the instance-list buffer, and its copies; 0 for one item. */
            std::uint32_t instanceFirst = 0;
            std::uint32_t instanceCount = 0;
            /** @brief Linear, multiplying the vertex colour: white for every record the CPU writes. */
            Math::Vec4 colour{1.0f};
            /**
             * @brief The item's box for culling (ADR-0057), xyz with w unused: for an item its world bounds, so a
             * cull tests it under the identity; for an instance list its mesh's local bounds, moved by each copy.
             */
            Math::Vec4 boundsMin{0.0f};
            Math::Vec4 boundsMax{0.0f};
        };
        static_assert(sizeof(DrawBlock) == 192);
        static_assert(offsetof(DrawBlock, paletteOffset) == 128 && offsetof(DrawBlock, worldSign) == 132 &&
                      offsetof(DrawBlock, instanceCount) == 140 && offsetof(DrawBlock, colour) == 144 &&
                      offsetof(DrawBlock, boundsMin) == 160 && offsetof(DrawBlock, boundsMax) == 176);

        /** @brief One copy of an instance list, as `Fenrir/Draw.glsl`'s FenrirInstance declares it (std430). */
        struct InstanceBlock
        {
            /** @brief Relative to the drawing entity. */
            Math::Mat4 transform;
            /** @brief The inverse transpose of the transform's 3x3, a column a vec4 as std430 lays out a mat3. */
            std::array<Math::Vec4, 3> normal;
            Math::Vec4 colour{1.0f};
        };
        static_assert(sizeof(InstanceBlock) == 128);

        /**
         * @brief A Skinning dispatch's job block, as `builtin:skinning` declares it (std140): where its mesh's streams
         * start in the mesh's vertex buffer and its skinned vertices in the frame's skinned vertex buffer, in 32-bit
         * words, and its palette range's first entry, in bones.
         */
        struct SkinJobBlock
        {
            std::uint32_t vertexCount = 0;
            std::uint32_t paletteOffset = 0;
            std::uint32_t skinnedFirst = 0;
            /** @brief Bit 0: the mesh has normals; bit 1: it has tangents. */
            std::uint32_t flags = 0;
            std::uint32_t positions = 0;
            std::uint32_t normals = 0;
            std::uint32_t tangents = 0;
            std::uint32_t joints = 0;
            std::uint32_t weights = 0;
            std::array<std::uint32_t, 3> padding{};
        };
        static_assert(sizeof(SkinJobBlock) == 48);

        /** @name The standard vertex locations (`Fenrir/Vertex.glsl`, `Fenrir/Skinning.glsl`) compute skinning names.
         * @{
         */
        constexpr std::uint32_t PositionLocation = 0;
        constexpr std::uint32_t NormalLocation = 1;
        constexpr std::uint32_t TangentLocation = 2;
        constexpr std::uint32_t JointLocation = 6;
        constexpr std::uint32_t WeightLocation = 7;
        /** @} */

        /** @brief The standard streams a compute-skinned draw reads from the skinned vertex buffer, as bits. */
        constexpr std::uint32_t SkinnedStreams =
            (1u << PositionLocation) | (1u << NormalLocation) | (1u << TangentLocation);
        /**
         * @brief Where each skinned stream starts within a mesh's skinned vertices, in bytes a vertex: its positions,
         * then its normals, then its tangents, by location.
         */
        constexpr std::array<std::size_t, 3> SkinnedStreamStarts{0, 12, 24};
        /** @brief The bytes of one skinned vertex: its position, normal and tangent, each in a stream of its own. */
        constexpr std::size_t SkinnedVertexBytes = SkinnedStreamStarts.back() + 16;
        /** @brief builtin:skinning's work group size, one invocation a vertex. */
        constexpr std::uint32_t SkinningGroupSize = 64;

        /** @brief A LightClusters pass's job block, as `builtin:light-clusters` declares it (std140). */
        struct ClusterJobBlock
        {
            Math::Mat4 view{1.0f};
            Math::Mat4 inverseProjection{1.0f};
            /** @brief The first slice's near depth and the last's far one. */
            Math::Vec4 depths{0.0f};
            /** @brief How many lights the light buffer holds, and each cluster's room for light indices. */
            std::array<std::uint32_t, 4> counts{};
        };
        static_assert(sizeof(ClusterJobBlock) == 160);
        /** @brief builtin:light-clusters' work group size, one invocation a cluster. */
        constexpr std::uint32_t LightClustersGroupSize = 64;

        /** @brief builtin:cull's work group size, one invocation a candidate; no group straddles two runs. */
        constexpr std::uint32_t CullGroupSize = 64;

        /** @brief A Cull pass's job block, as `builtin:cull` declares it (std140). */
        struct CullJobBlock
        {
            /**
             * @brief The view's first entry in the group table, how many it has, how many of `planes` are the view's
             * (0 keeps every candidate: RendererConfig::gpuCulling off), and an unused word.
             */
            std::array<std::uint32_t, 4> groups{};
            /**
             * @brief The view's planes (Math::Frustum::Planes, world space, `vec4(n, d)`, inside when
             * `dot(n, p) + d >= 0`): five for a camera, six for an orthographic view; the rest unused.
             */
            std::array<Math::Vec4, 6> planes{};
        };
        static_assert(sizeof(CullJobBlock) == 112);
        static_assert(offsetof(CullJobBlock, planes) == 16);

        /** @brief One entry of a view's indirection, as `Fenrir/DrawRecord.glsl` lays it: a record and a copy. */
        struct IndirectionEntry
        {
            std::uint32_t record = 0;
            std::uint32_t copy = 0;
        };
        static_assert(sizeof(IndirectionEntry) == 8);
        /** @brief `Fenrir/DrawRecord.glsl`'s FenrirNoCopy and FenrirNoIndirection. */
        constexpr std::uint32_t NoCopy = 0xFFFFFFFFu;
        constexpr std::uint32_t NoIndirection = 0xFFFFFFFFu;

        /** @brief The background block, as `builtin:background` declares it (std140). */
        struct BackgroundBlock
        {
            Math::Vec4 colour;
        };

        /** @brief The Output block, as `builtin:output` declares it at set 1 (std140). */
        struct OutputBlock
        {
            /** @brief 2^exposure (ADR-0049). */
            float exposureScale = 1.0f;
            /** @brief The Tonemapper's value, which the shader switches on. */
            std::uint32_t tonemapper = 0;
            /** @brief The channel the OCCLUSION variant draws as grey: 0 red, 2 blue (reflection confidence). */
            std::uint32_t greyChannel = 0;
            /** @brief 1 when the view's ColourGrade is applied, 0 for an identity grade (ADR-0059). */
            std::uint32_t grade = 0;
            /** @brief The grade's white balance, column by column, each padded to a vec4 as std140 lays out a mat3. */
            std::array<Math::Vec4, 3> whiteBalance{Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f),
                                                   Math::Vec4(0.0f, 1.0f, 0.0f, 0.0f),
                                                   Math::Vec4(0.0f, 0.0f, 1.0f, 0.0f)};
            /** @brief The shadow tint, and the contrast in w. */
            Math::Vec4 shadowTint{1.0f};
            /** @brief The highlight tint, and the split in w. */
            Math::Vec4 highlightTint{1.0f, 1.0f, 1.0f, 0.0f};
            /** @brief The CDL's slope, and the saturation in w. */
            Math::Vec4 slope{1.0f};
            Math::Vec3 offset{0.0f};
            float padding0 = 0.0f;
            /** @brief The CDL's power. */
            Math::Vec3 power{1.0f};
            /** @brief The chromatic aberration's intensity (ADR-0059), which the ABERRATION variant reads. */
            float aberration = 0.0f;
            /** @brief The vignette's intensity (0 skips it), radius, smoothness and roundness. */
            Math::Vec4 vignette{0.0f};
            /** @brief The film grain's intensity (0 skips it), response and size. */
            Math::Vec3 grain{0.0f};
            /** @brief The frame's index, low 32 bits, which the grain's noise is a hash of. */
            std::uint32_t frameIndex = 0;
        };
        static_assert(sizeof(OutputBlock) == 176);

        /** @brief A Masked material's depth prepass block, as `builtin:depth-prepass` declares it (std140). */
        struct PrepassMaskBlock
        {
            /** @brief Its base colour's alpha. */
            float alpha = 1.0f;
            float cutoff = 0.5f;
            std::uint32_t padding0 = 0;
            std::uint32_t padding1 = 0;
        };
        static_assert(sizeof(PrepassMaskBlock) == 16);

        /** @brief The material members and textures the prepass cuts a Masked item out by, as builtin:lit names them.
         */
        constexpr std::string_view BaseColourMember = "baseColour";
        constexpr std::string_view BaseColourTexture = "baseColourTexture";

        /** @brief One binding of a fixed set as the renderer declares it: a block's kind and size (0: any size). */
        struct FixedBinding
        {
            std::uint32_t set = 0;
            std::uint32_t binding = 0;
            ShaderBindingKind kind = ShaderBindingKind::UniformBuffer;
            std::uint32_t size = 0;
        };

        // Sets 0, 1 and 3, declared in C++ (ADR-0024, ADR-0035, ADR-0048). The draw and palette buffers hold as many
        // records as they need; reflection sizes such a block by one record, so their record layouts are checked. Set
        // 0's textures are the sky's light (ADR-0047): its spherical harmonics, its specular cube and the BRDF table,
        // then the rect lights' two LTC tables; its storage blocks are the point, spot and rect lights (ADR-0048), as
        // many as the frame has, and the shadowed spot lights' records, with their atlas, then the shadowed point
        // lights' records, with the point shadow cubes (ADR-0050). Set 1's textures are the view's ambient occlusion
        // (ADR-0049), its sun's shadow atlas (ADR-0050), and its reflections' hit target and history (ADR-0056); its
        // storage block the view's light clusters (ADR-0049). Set 3's fourth storage block is the view's indirection
        // (ADR-0057).
        [[maybe_unused]] constexpr std::array<FixedBinding, 21> FixedBindings{{
            {0, 0, ShaderBindingKind::UniformBuffer, sizeof(FrameBlock)},
            {0, 1, ShaderBindingKind::Texture, 0},
            {0, 2, ShaderBindingKind::Texture, 0},
            {0, 3, ShaderBindingKind::Texture, 0},
            {0, 4, ShaderBindingKind::StorageBuffer, sizeof(GpuLight)},
            {0, 5, ShaderBindingKind::Texture, 0},
            {0, 6, ShaderBindingKind::Texture, 0},
            {0, 7, ShaderBindingKind::StorageBuffer, sizeof(GpuSpotShadow)},
            {0, 8, ShaderBindingKind::Texture, 0},
            {0, 9, ShaderBindingKind::StorageBuffer, sizeof(GpuPointShadow)},
            {0, 10, ShaderBindingKind::Texture, 0},
            {1, 0, ShaderBindingKind::UniformBuffer, sizeof(ViewBlock)},
            {1, 1, ShaderBindingKind::Texture, 0},
            {1, 2, ShaderBindingKind::Texture, 0},
            {1, 3, ShaderBindingKind::StorageBuffer, sizeof(std::uint32_t)},
            {1, 4, ShaderBindingKind::Texture, 0},
            {1, 5, ShaderBindingKind::Texture, 0},
            {3, 0, ShaderBindingKind::StorageBuffer, sizeof(DrawBlock)},
            {3, 1, ShaderBindingKind::StorageBuffer, sizeof(InstanceBlock)},
            {3, 2, ShaderBindingKind::StorageBuffer, sizeof(GpuBone)},
            {3, 3, ShaderBindingKind::StorageBuffer, sizeof(IndirectionEntry)},
        }};

        /** @brief The binding set of the material. */
        constexpr std::uint32_t MaterialSet = 2;

        /** @brief One standard vertex stream (`Fenrir/Vertex.glsl`), by location: its name, format and size. */
        struct StreamFormat
        {
            std::string_view name;
            VertexFormat format = VertexFormat::Float3;
            std::size_t size = 0;
            /** @brief Where its constant default sits in the default stream buffer. */
            std::size_t defaultOffset = 0;
            std::string_view defaultName;
            /** @brief Whether a mesh lacking it is logged: not when its default itself tells a shader it is absent. */
            bool logMissing = true;
        };

        // Joints and weights (ADR-0035) are a skinned mesh's; their zero defaults weigh nothing, the identity. Tangents
        // are optional (ADR-0048): the zero tangent's handedness 0 tells a shader there are none, so they go unlogged.
        constexpr std::array<StreamFormat, 8> Streams{{
            {"positions", VertexFormat::Float3, 12, 0, "the origin"},
            {"normals", VertexFormat::Float3, 12, 12, "normal +Y"},
            {"tangents", VertexFormat::Float4, 16, 24, "the zero tangent", false},
            {"uv0", VertexFormat::Float2, 8, 40, "UV 0"},
            {"uv1", VertexFormat::Float2, 8, 48, "UV 0"},
            {"colours", VertexFormat::Float4, 16, 56, "white"},
            {"joints", VertexFormat::Uint16x4, 8, 72, "bone 0"},
            {"weights", VertexFormat::Unorm16x4, 8, 80, "no weight"},
        }};

        // The constants above, back to back: position, normal, tangent, UV0, UV1, colour, then the joints' and weights'
        // zero bits.
        constexpr std::array<float, 22> DefaultStreams{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                                       1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};

        constexpr std::uint8_t OpaqueStage = static_cast<std::uint8_t>(RenderStage::Opaque);
        constexpr std::uint8_t TransparentStage = static_cast<std::uint8_t>(RenderStage::Transparent);

        /** @brief The variant key the renderer sets for a `Masked` material (ADR-0024). */
        constexpr std::string_view AlphaMaskKey = "ALPHA_MASK";
        /** @brief The variant key the renderer sets for a skinned draw (ADR-0035). */
        constexpr std::string_view SkinnedKey = "SKINNED";
        /** @brief The variant key of a point light's shadow: six cube faces as instances, or its distance (ADR-0050).
         */
        constexpr std::string_view PointShadowKey = "POINT_SHADOW";
        /** @brief The block member a `Masked` material's alpha cutoff is written into. */
        constexpr std::string_view AlphaCutoffMember = "alphaCutoff";

        /** @brief Whether a material of `mode` draws in the Transparent pass, blended and without writing depth. */
        constexpr bool IsTransparent(Fenrir::BlendMode mode) noexcept
        {
            return mode == Fenrir::BlendMode::AlphaBlend || mode == Fenrir::BlendMode::Additive;
        }

        /** @brief How the device blends a material of `mode`. */
        constexpr Graphics::BlendMode DeviceBlendOf(Fenrir::BlendMode mode) noexcept
        {
            if (mode == Fenrir::BlendMode::AlphaBlend)
            {
                return Graphics::BlendMode::Alpha;
            }
            return mode == Fenrir::BlendMode::Additive ? Graphics::BlendMode::Additive : Graphics::BlendMode::None;
        }

        template <typename T>
        std::span<const std::byte> BytesOf(const T& value)
        {
            return std::as_bytes(std::span(&value, 1));
        }

        /**
         * @brief Where each view block sits in the frame's view block ring: each view's, then each view's cascades'
         * (ADR-0050), MaxCascades slots a view, then each spot light tile's, then each point light's: its cube's, then
         * its faces'.
         */
        struct ViewBlockSlots
        {
            /** @brief How many views the frame draws. */
            std::size_t views = 0;
            /** @brief How many spot lights keep their shadows: one tile each. */
            std::size_t spotTiles = 0;
            /** @brief How many point lights keep their shadows: one cube each. */
            std::size_t pointCubes = 0;

            /** @brief View `v`'s block. */
            [[nodiscard]] constexpr std::size_t View(std::size_t v) const noexcept
            {
                return v;
            }
            /** @brief View `v`'s cascade `c`'s block. */
            [[nodiscard]] constexpr std::size_t Cascade(std::size_t v, std::size_t c) const noexcept
            {
                return views + v * MaxCascades + c;
            }
            /** @brief Spot light tile `t`'s block. */
            [[nodiscard]] constexpr std::size_t Tile(std::size_t t) const noexcept
            {
                return views * (1 + MaxCascades) + t;
            }
            /** @brief Point light cube `c`'s block, from the light and not turned. */
            [[nodiscard]] constexpr std::size_t Cube(std::size_t c) const noexcept
            {
                return Tile(spotTiles) + c * (1 + CubeFaceCount);
            }
            /** @brief Face `f` of point light cube `c`'s block. */
            [[nodiscard]] constexpr std::size_t Face(std::size_t c, std::size_t f) const noexcept
            {
                return Cube(c) + 1 + f;
            }
            /** @brief How many slots the frame takes. */
            [[nodiscard]] constexpr std::size_t Count() const noexcept
            {
                return Cube(pointCubes);
            }
        };

        /** @brief The standard vertex locations a variant reads, as bits. */
        std::uint32_t ReadMask(const ShaderVariant& variant)
        {
            std::uint32_t mask = 0;
            for (const ShaderVertexInput& input : variant.vertexInputs)
            {
                if (input.location < Streams.size())
                {
                    mask |= 1u << input.location;
                }
            }
            return mask;
        }

        /** @brief The standard vertex locations a mesh has a stream for, as bits, from its streams' offsets. */
        std::uint32_t StreamsOf(const std::array<std::optional<std::size_t>, 8>& streams)
        {
            std::uint32_t present = 0;
            for (std::uint32_t location = 0; location < streams.size(); ++location)
            {
                if (streams.at(location).has_value())
                {
                    present |= 1u << location;
                }
            }
            return present;
        }

        /**
         * @brief One vertex buffer slot per input the variant reads, at stride 0 for a stream not in `streams`
         * (ADR-0024), and the location each slot feeds; why not, if an input is at no standard location.
         */
        std::string VertexBuffersOf(const ShaderVariant& variant, std::uint32_t streams,
                                    std::vector<VertexBufferLayout>& buffers, std::vector<std::uint32_t>& locations)
        {
            for (const ShaderVertexInput& input : variant.vertexInputs)
            {
                if (input.location >= Streams.size())
                {
                    return std::format("it reads vertex input '{}' at location {}, which no mesh stream feeds",
                                       input.name, input.location);
                }
                const StreamFormat& stream = Streams.at(input.location);
                // A stream the mesh lacks reads one constant: a stride of 0.
                const bool has = (streams & (1u << input.location)) != 0;
                buffers.push_back(
                    {has ? static_cast<std::uint32_t>(stream.size) : 0u, {{input.location, stream.format, 0}}});
                locations.push_back(input.location);
            }
            return {};
        }

        [[maybe_unused]] std::string_view KindName(ShaderBindingKind kind)
        {
            // By ShaderBindingKind: uniform block, storage block, texture.
            constexpr std::array<std::string_view, 3> names{"uniform block", "storage block", "texture"};
            return names.at(static_cast<std::size_t>(kind));
        }

        /**
         * @brief Why a variant's sets 0, 1 and 3 disagree with what the renderer binds there, or empty. Checked in
         * debug builds only (ADR-0024).
         */
        std::string CheckFixedSets([[maybe_unused]] const ShaderVariant& variant)
        {
#ifndef NDEBUG
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set == MaterialSet)
                {
                    continue;
                }
                const auto declared = std::find_if(FixedBindings.begin(), FixedBindings.end(), [&](const auto& fixed) {
                    return fixed.set == binding.set && fixed.binding == binding.binding;
                });
                if (declared == FixedBindings.end())
                {
                    return std::format("set {} binding {} ('{}') is not a binding the renderer declares", binding.set,
                                       binding.binding, binding.name);
                }
                if (declared->kind != binding.kind)
                {
                    return std::format("set {} binding {} ('{}') is a {} in the shader, but the renderer declares a {}",
                                       binding.set, binding.binding, binding.name, KindName(binding.kind),
                                       KindName(declared->kind));
                }
                if (declared->size != 0 && declared->size != binding.size)
                {
                    return std::format("set {} binding {} ('{}') is {} bytes in the shader, but the renderer declares "
                                       "{}",
                                       binding.set, binding.binding, binding.name, binding.size, declared->size);
                }
            }
#endif
            return {};
        }

        /** @brief The parameter default of the texture parameter `name`, white if the variant does not say. */
        DefaultTexture DefaultTextureOf(const ShaderVariant& variant, std::string_view name)
        {
            const auto found = std::find_if(variant.parameters.begin(), variant.parameters.end(), [&](const auto& p) {
                return p.type == ShaderParameterType::Texture && p.name == name;
            });
            return found == variant.parameters.end() ? DefaultTexture::White : found->texture;
        }

        /** @brief Writes `material`'s number overrides that the variant's block has, at their std140 offsets. */
        void WriteOverrides(const ShaderVariant& variant, const MaterialData& material, std::vector<std::byte>& block)
        {
            for (const auto& [name, value] : material.overrides)
            {
                const auto member = std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                                 [&](const ShaderBlockMember& m) { return m.name == name; });
                if (member == variant.materialMembers.end())
                {
                    continue;
                }
                // An override of another type than the member is kept by the store and ignored here.
                std::visit(
                    [&](const auto& v) {
                        using V = std::decay_t<decltype(v)>;
                        std::size_t components = 0;
                        ShaderValueType type = ShaderValueType::Other;
                        if constexpr (std::is_same_v<V, float>)
                        {
                            components = 1;
                            type = ShaderValueType::Float;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec2>)
                        {
                            components = 2;
                            type = ShaderValueType::Vec2;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec3>)
                        {
                            components = 3;
                            type = ShaderValueType::Vec3;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec4>)
                        {
                            components = 4;
                            type = ShaderValueType::Vec4;
                        }
                        if (components != 0 && member->type == type &&
                            member->offset + components * sizeof(float) <= block.size())
                        {
                            std::memcpy(std::span(block).subspan(member->offset).data(), &v,
                                        components * sizeof(float));
                        }
                    },
                    value);
            }
        }

        /** @brief Writes a `Masked` material's alpha cutoff into the variant's `alphaCutoff` float member, if it has
         * one. */
        void WriteAlphaCutoff(const ShaderVariant& variant, float cutoff, std::vector<std::byte>& block)
        {
            const auto member = std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                             [](const ShaderBlockMember& m) { return m.name == AlphaCutoffMember; });
            if (member != variant.materialMembers.end() && member->type == ShaderValueType::Float &&
                member->offset + sizeof(float) <= block.size())
            {
                std::memcpy(std::span(block).subspan(member->offset).data(), &cutoff, sizeof(float));
            }
        }

        const ShaderVariant* BaseVariant(const ShaderContainer& container)
        {
            return container.FindVariant({});
        }

        /** @brief The container's variant with `SKINNED` added to `keys`; null if it has no such key. */
        const ShaderVariant* SkinnedVariant(const ShaderContainer& container, std::span<const std::string_view> keys)
        {
            if (std::ranges::find(container.keys, SkinnedKey) == container.keys.end())
            {
                return nullptr;
            }
            std::vector<std::string_view> wanted(keys.begin(), keys.end());
            wanted.push_back(SkinnedKey);
            return container.FindVariant(wanted);
        }

        /** @brief `of`'s keys with POINT_SHADOW too, in `container`; null if the container has no such key. */
        const ShaderVariant* PointShadowVariant(const ShaderContainer& container, const ShaderVariant& of)
        {
            if (std::ranges::find(container.keys, PointShadowKey) == container.keys.end())
            {
                return nullptr;
            }
            std::vector<std::string_view> wanted(of.keys.begin(), of.keys.end());
            wanted.push_back(PointShadowKey);
            return container.FindVariant(wanted);
        }

        /** @brief -1 when `world` mirrors (a negative determinant of its 3x3), else 1. */
        float WorldSign(const Math::Mat4& world)
        {
            return glm::determinant(Math::Mat3(world)) < 0.0f ? -1.0f : 1.0f;
        }

        /** @brief The device sampler a material texture parameter's description asks for. */
        SamplerDesc ToSamplerDesc(const SamplerDescription& description)
        {
            // By SamplerWrap: repeat, mirrored repeat, clamp to edge.
            constexpr std::array<AddressMode, 3> wraps{AddressMode::Repeat, AddressMode::MirroredRepeat,
                                                       AddressMode::ClampToEdge};
            SamplerDesc desc;
            desc.addressU = wraps.at(static_cast<std::size_t>(description.wrapU));
            desc.addressV = wraps.at(static_cast<std::size_t>(description.wrapV));
            desc.addressW = desc.addressU;
            const bool nearest = description.filter == SamplerFilter::Nearest;
            desc.minFilter = nearest ? Filter::Nearest : Filter::Linear;
            desc.magFilter = desc.minFilter;
            desc.mipFilter = description.filter == SamplerFilter::Trilinear ? Filter::Linear : Filter::Nearest;
            desc.maxAnisotropy = static_cast<float>(std::max<std::uint32_t>(description.anisotropy, 1));
            return desc;
        }

        /** @brief How a log line names a view's target: an image by its asset index, or the window. */
        std::string TargetName(AssetKey target)
        {
            return target.IsEmpty() ? std::string("the window") : std::format("image {}", target.index);
        }
    } // namespace

    void SceneRenderer::GrownRing::Reserve(GraphicsDevice& device, std::size_t bytes, BufferUsage usage,
                                           std::string_view label)
    {
        if (ring != nullptr && bytes <= capacity)
        {
            return;
        }
        capacity = std::max(bytes, capacity * 2);
        ring = std::make_unique<FrameRingBuffer>(device, BufferDesc{capacity, usage}, label);
    }

    // ---- SceneRenderer ----

    SceneRenderer::SceneRenderer(GraphicsDevice& device, ILogger& logger, std::uint32_t samples,
                                 bool automaticInstancing, bool computeSkinning, CascadeConfig cascades,
                                 ShadowBudgetConfig spotShadows, ShadowBudgetConfig pointShadows)
        : m_device(device), m_logger(logger), m_samples(std::max<std::uint32_t>(samples, 1)),
          m_automaticInstancing(automaticInstancing), m_computeSkinning(computeSkinning), m_cascades(cascades),
          m_clusterTargetGroups(device), m_cullTargetGroups(device), m_skyGroups(device), m_drawGroups(device),
          m_viewGroups(device), m_outputGroups(device), m_reflectionOutputGroups(device)
    {
        m_cascades.count = std::clamp(m_cascades.count, 1u, MaxCascades);
        m_cascades.mapSize = std::max(m_cascades.mapSize, 1u);
        m_frameLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer},
                                                            {1, ShaderBindingKind::Texture},
                                                            {2, ShaderBindingKind::Texture},
                                                            {3, ShaderBindingKind::Texture},
                                                            {4, ShaderBindingKind::StorageBuffer},
                                                            {5, ShaderBindingKind::Texture},
                                                            {6, ShaderBindingKind::Texture},
                                                            {7, ShaderBindingKind::StorageBuffer},
                                                            {8, ShaderBindingKind::Texture},
                                                            {9, ShaderBindingKind::StorageBuffer},
                                                            {10, ShaderBindingKind::Texture}}},
                                                          "Frame");
        m_viewLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer},
                                                           {1, ShaderBindingKind::Texture},
                                                           {2, ShaderBindingKind::Texture},
                                                           {3, ShaderBindingKind::StorageBuffer},
                                                           {4, ShaderBindingKind::Texture},
                                                           {5, ShaderBindingKind::Texture}}},
                                                         "View");
        m_outputBlockLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Output");
        m_drawLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::StorageBuffer},
                                                           {1, ShaderBindingKind::StorageBuffer},
                                                           {2, ShaderBindingKind::StorageBuffer},
                                                           {3, ShaderBindingKind::StorageBuffer}}},
                                                         "Draw");

        // The default textures (ADR-0024): 1x1 white, black and the flat tangent-space normal, linear.
        constexpr std::array<std::array<std::uint8_t, 4>, 3> texels{{
            {255, 255, 255, 255},
            {0, 0, 0, 255},
            {128, 128, 255, 255},
        }};
        constexpr std::array<std::string_view, 3> names{"Default white", "Default black", "Default normal"};
        for (std::size_t i = 0; i < texels.size(); ++i)
        {
            TextureDesc desc;
            desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            m_defaultTextures.at(i) = m_device.CreateTexture(desc, names.at(i));
            m_device.WriteTexture(m_defaultTextures.at(i), 0, 0, std::as_bytes(std::span(texels.at(i))));
        }
        // The rect lights' linearly transformed cosines (ADR-0048): two 64 x 64 RGBA16F tables, written once.
        const auto makeLtcTable = [&](std::span<const std::uint16_t> texels, std::string_view name) {
            TextureDesc desc;
            desc.format = TextureFormat::Rgba16Float;
            desc.width = LtcSide;
            desc.height = LtcSide;
            desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            const TextureHandle texture = m_device.CreateTexture(desc, name);
            m_device.WriteTexture(texture, 0, 0, std::as_bytes(texels));
            return texture;
        };
        m_ltcMatrix = makeLtcTable(LtcMatrixTexels(), "LTC matrix");
        m_ltcAmplitude = makeLtcTable(LtcAmplitudeTexels(), "LTC amplitude");
        // SamplerDescription's default: repeat, trilinear, anisotropy 8.
        SamplerDesc sampler;
        sampler.maxAnisotropy = 8.0f;
        m_defaultSampler = m_device.CreateSampler(sampler, "Default sampler");
        SamplerDesc clamp;
        clamp.addressU = AddressMode::ClampToEdge;
        clamp.addressV = AddressMode::ClampToEdge;
        clamp.addressW = AddressMode::ClampToEdge;
        m_outputSampler = m_device.CreateSampler(clamp, "Output sampler");
        // Clamped, so a face's edge does not wrap round to its opposite edge, and trilinear across the mips.
        m_skySampler = m_device.CreateSampler(clamp, "Sky sampler");
        // Lit where the reference is at least the stored depth: nearer the light under reversed-Z. Bilinear, so each
        // read is itself a 2 x 2 filter.
        SamplerDesc shadow = clamp;
        shadow.mipFilter = Filter::Nearest;
        shadow.compare = CompareOp::GreaterEqual;
        m_shadowSampler = m_device.CreateSampler(shadow, "Shadow sampler");
        TextureDesc noShadow;
        noShadow.format = TextureFormat::Depth32Float;
        noShadow.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        m_noShadow = m_device.CreateTexture(noShadow, "No shadow");
        constexpr float farthest = 0.0f;
        m_device.WriteTexture(m_noShadow, 0, 0, BytesOf(farthest));
        m_defaultStreams = m_device.CreateBuffer({sizeof(DefaultStreams), BufferUsage::Vertex},
                                                 std::as_bytes(std::span(DefaultStreams)), "Default vertex streams");

        m_fallback.container = ReadBuiltin(BuiltinProgram::Fallback, m_logger);
        m_fallback.variant = BaseVariant(m_fallback.container);
        if (m_fallback.variant != nullptr)
        {
            m_fallback.materialLayout = MaterialLayoutOf(*m_fallback.variant);
            m_fallback.variants.push_back({m_fallback.variant, m_fallback.materialLayout});
            // Its SKINNED variant, which skinned draws fall back to: never drawn unskinned (ADR-0035).
            if (const ShaderVariant* skinned = SkinnedVariant(m_fallback.container, {}))
            {
                m_fallback.variants.push_back({skinned, m_fallback.materialLayout});
            }
        }
        m_fallback.id = 0;
        m_fallback.computeSkinned = true;

        m_background = ReadBuiltin(BuiltinProgram::Background, m_logger);
        m_output = ReadBuiltin(BuiltinProgram::Output, m_logger);
        m_prepass = ReadBuiltin(BuiltinProgram::DepthPrepass, m_logger);
        m_shadow = ReadBuiltin(BuiltinProgram::Shadow, m_logger);
        m_prepassMaskLayout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::UniformBuffer}, {1, ShaderBindingKind::Texture}}}, "Prepass mask");
        m_skyImages = std::make_unique<SkyImages>(m_device, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_background))
        {
            m_backgroundLayout = MaterialLayoutOf(*variant);
        }
        if (const ShaderVariant* variant = BaseVariant(m_output))
        {
            m_outputLayout = MaterialLayoutOf(*variant);
        }
        m_backgroundPipeline = MakeInternalPipeline(m_background, BaseVariant(m_background), m_backgroundLayout,
                                                    TextureFormat::Rgba16Float, true, m_samples);
        constexpr std::array<std::string_view, 1> cubemap{"CUBEMAP"};
        if (const ShaderVariant* variant = m_background.FindVariant(cubemap))
        {
            m_skyLayout = MaterialLayoutOf(*variant);
            m_skyPipeline = MakeInternalPipeline(m_background, variant, m_skyLayout, TextureFormat::Rgba16Float, true,
                                                 m_samples, m_viewLayout, m_frameLayout);
        }
        // With MSAA, Output reads every sample of SceneColor and averages them: the resolve.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        const ShaderVariant* output = m_samples > 1 ? m_output.FindVariant(msaa) : BaseVariant(m_output);
        m_outputPipeline = MakeInternalPipeline(m_output, output, m_outputLayout, TextureFormat::Rgba8Srgb, false, 1,
                                                m_outputBlockLayout);
        constexpr std::array<std::string_view, 1> occlusion{"OCCLUSION"};
        m_occlusionOutputPipeline = MakeInternalPipeline(m_output, m_output.FindVariant(occlusion), m_outputLayout,
                                                         TextureFormat::Rgba8Srgb, false, 1, m_outputBlockLayout);
        // Chromatic aberration reads each channel at its own point: with MSAA, red and blue each resolve four texels.
        constexpr std::array<std::string_view, 1> aberration{"ABERRATION"};
        constexpr std::array<std::string_view, 2> msaaAberration{"MSAA", "ABERRATION"};
        m_aberrationOutputPipeline = MakeInternalPipeline(
            m_output, m_samples > 1 ? m_output.FindVariant(msaaAberration) : m_output.FindVariant(aberration),
            m_outputLayout, TextureFormat::Rgba8Srgb, false, 1, m_outputBlockLayout);
        constexpr std::array<std::string_view, 1> reflections{"REFLECTIONS"};
        if (const ShaderVariant* variant = m_output.FindVariant(reflections))
        {
            m_reflectionOutputLayout = MaterialLayoutOf(*variant);
            m_reflectionOutputPipeline = MakeInternalPipeline(m_output, variant, m_reflectionOutputLayout,
                                                              TextureFormat::Rgba8Srgb, false, 1, m_outputBlockLayout);
        }

        // The frame set's groups bind the frame block with the sky's textures, so the ring makes none of its own.
        m_frameBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, false, sizeof(FrameBlock), "Frame");
        m_lightBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, true, sizeof(GpuLight), "Lights");
        m_spotShadows = std::make_unique<ShadowPool<SpotShadowKind>>(m_device, m_logger, spotShadows, m_noShadow);
        m_pointShadows = std::make_unique<ShadowPool<PointShadowKind>>(m_device, m_logger, pointShadows);
        m_skyLighting = std::make_unique<SkyLighting>(m_device, m_logger);
        m_bloom = std::make_unique<BloomChain>(m_device, m_logger, m_samples);
        m_gtao = std::make_unique<Gtao>(m_device, m_logger, m_samples);
        m_sceneHistory = std::make_unique<SceneHistory>(m_device, m_logger, m_samples);
        m_reflectionTrace = std::make_unique<ReflectionTrace>(m_device, m_logger, m_samples);
        // The view set's groups bind a view block with its ambient occlusion, so the ring makes none of its own.
        m_viewBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, false, sizeof(ViewBlock), "View");
        m_backgroundBlocks =
            std::make_unique<BlockRing>(m_device, m_backgroundLayout, false, sizeof(BackgroundBlock), "Background");
        m_outputBlocks =
            std::make_unique<BlockRing>(m_device, m_outputBlockLayout, false, sizeof(OutputBlock), "Output");
        // The draw set's groups bind both of these rings, so the rings make none of their own.
        m_drawBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, true, sizeof(DrawBlock), "Draw");
        m_paletteBlocks =
            std::make_unique<BlockRing>(m_device, BindingGroupLayoutHandle{}, true, sizeof(GpuBone), "Palette");

        // Compute skinning (ADR-0050): without its pipeline, skinned draws skin in their vertex stages.
        m_skinJobLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Skin job");
        m_skinSourceLayout =
            m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::StorageBuffer}}}, "Skin source");
        m_skinTargetLayout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::StorageBuffer}, {1, ShaderBindingKind::StorageBuffer}}}, "Skin target");
        m_skinJobs = std::make_unique<BlockRing>(m_device, m_skinJobLayout, false, sizeof(SkinJobBlock), "Skin jobs");
        m_skinning = ReadBuiltin(BuiltinProgram::Skinning, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_skinning))
        {
            PipelineResult made = m_device.CreateComputePipeline(
                {variant, {m_skinJobLayout, m_skinSourceLayout, m_skinTargetLayout, {}}}, "Skinning");
            m_skinningPipeline = made.pipeline;
            if (m_skinningPipeline.IsEmpty())
            {
                m_logger.Error("FenrirRenderer: no compute skinning pipeline, so skinned meshes skin in their vertex "
                               "stages: {}",
                               made.diagnostic);
            }
        }
        // Light clustering (ADR-0049): without its pipeline, every pixel loops over the whole light buffer.
        m_clusterJobLayout =
            m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Cluster job");
        m_clusterTargetLayout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::StorageBuffer}, {1, ShaderBindingKind::StorageBuffer}}}, "Cluster target");
        m_clusterJobs =
            std::make_unique<BlockRing>(m_device, m_clusterJobLayout, false, sizeof(ClusterJobBlock), "Cluster jobs");
        m_lightClusters = ReadBuiltin(BuiltinProgram::LightClusters, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_lightClusters))
        {
            PipelineResult made = m_device.CreateComputePipeline(
                {variant, {m_clusterJobLayout, m_clusterTargetLayout, {}, {}}}, "LightClusters");
            m_lightClustersPipeline = made.pipeline;
            if (m_lightClustersPipeline.IsEmpty())
            {
                m_logger.Error(
                    "FenrirRenderer: no light clustering pipeline, so every pixel loops over every light: {}",
                    made.diagnostic);
            }
        }
        constexpr std::array<std::uint32_t, 4> noClusters{};
        m_noClusters = m_device.CreateBuffer({sizeof(noClusters), BufferUsage::Storage},
                                             std::as_bytes(std::span(noClusters)), "No light clusters");
        // GPU culling (ADR-0057): without its pipeline, every pass draws directly, as before it.
        m_cullJobLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Cull job");
        m_cullTargetLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::StorageBuffer},
                                                                 {1, ShaderBindingKind::StorageBuffer},
                                                                 {2, ShaderBindingKind::StorageBuffer},
                                                                 {3, ShaderBindingKind::StorageBuffer},
                                                                 {4, ShaderBindingKind::StorageBuffer}}},
                                                               "Cull target");
        m_cullJobs = std::make_unique<BlockRing>(m_device, m_cullJobLayout, false, sizeof(CullJobBlock), "Cull jobs");
        m_cull = ReadBuiltin(BuiltinProgram::Cull, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_cull))
        {
            PipelineResult made =
                m_device.CreateComputePipeline({variant, {m_cullJobLayout, m_cullTargetLayout, {}, {}}}, "Cull");
            m_cullPipeline = made.pipeline;
            if (m_cullPipeline.IsEmpty())
            {
                m_logger.Error("FenrirRenderer: no cull pipeline, so every pass draws directly: {}", made.diagnostic);
            }
        }
        constexpr IndirectionEntry noIndirection{NoIndirection, NoCopy};
        m_noIndirection = m_device.CreateBuffer({sizeof(noIndirection), BufferUsage::Storage}, BytesOf(noIndirection),
                                                "No indirection");
        m_frameBlocks->Reserve(1);
        m_lightBlocks->Reserve(1);
    }

    SceneRenderer::~SceneRenderer()
    {
        for (auto& [key, material] : m_materials)
        {
            DestroyMaterial(material);
        }
        for (auto& [key, mesh] : m_meshes)
        {
            DestroyMesh(mesh);
        }
        for (auto& [key, target] : m_targets)
        {
            m_device.Destroy(target.texture);
        }
        for (auto& [key, image] : m_images)
        {
            m_device.Destroy(image.texture);
        }
        m_skyGroups.Clear();
        m_skyImages.reset();
        for (auto& [desc, sampler] : m_samplers)
        {
            m_device.Destroy(sampler);
        }
        for (auto& [key, pipeline] : m_pipelines)
        {
            m_device.Destroy(pipeline.pipeline);
        }
        for (auto& [key, prepass] : m_prepassPipelines)
        {
            m_device.Destroy(prepass.pipeline.pipeline);
        }
        for (auto& [key, shadow] : m_shadowPipelines)
        {
            m_device.Destroy(shadow.pipeline.pipeline);
        }
        for (auto& [key, shader] : m_shaders)
        {
            for (const VariantEntry& variant : shader.variants)
            {
                m_device.Destroy(variant.materialLayout);
            }
        }
        m_outputGroups.Clear();
        m_reflectionOutputGroups.Clear();
        m_viewGroups.Clear();
        m_drawGroups.Clear();
        for (const FrameGroup& group : m_frameGroups)
        {
            if (group.unshadowed != group.group)
            {
                m_device.Destroy(group.unshadowed);
            }
            m_device.Destroy(group.group);
        }
        m_skyLighting.reset();
        m_gtao.reset();
        m_reflectionTrace.reset();
        m_sceneHistory.reset();
        m_frameBlocks.reset();
        m_lightBlocks.reset();
        m_spotShadows.reset();
        m_pointShadows.reset();
        m_viewBlocks.reset();
        m_backgroundBlocks.reset();
        m_outputBlocks.reset();
        m_drawBlocks.reset();
        m_paletteBlocks.reset();
        m_skinJobs.reset();
        m_skinnedVertices.reset();
        for (const SkinTargetGroup& group : m_skinTargetGroups)
        {
            m_device.Destroy(group.group);
        }
        m_device.Destroy(m_skinningPipeline);
        m_device.Destroy(m_skinJobLayout);
        m_device.Destroy(m_skinSourceLayout);
        m_device.Destroy(m_skinTargetLayout);
        m_clusterTargetGroups.Clear();
        m_clusterJobs.reset();
        m_clusterBuffers.clear();
        m_device.Destroy(m_noClusters);
        m_cullTargetGroups.Clear();
        m_cullJobs.reset();
        m_cullTemplates.ring.reset();
        m_cullGroups.ring.reset();
        m_cullRings.clear();
        for (const CullReadback& readback : m_cullReadbacks)
        {
            m_device.Destroy(readback.buffer);
        }
        m_cullReadbacks.clear();
        m_device.Destroy(m_noIndirection);
        m_device.Destroy(m_cullPipeline);
        m_device.Destroy(m_cullJobLayout);
        m_device.Destroy(m_cullTargetLayout);
        m_device.Destroy(m_lightClustersPipeline);
        m_device.Destroy(m_clusterJobLayout);
        m_device.Destroy(m_clusterTargetLayout);
        m_device.Destroy(m_instanceListBuffer);
        m_device.Destroy(m_fallback.materialLayout);
        m_device.Destroy(m_backgroundPipeline);
        m_device.Destroy(m_skyPipeline);
        m_device.Destroy(m_outputPipeline);
        m_device.Destroy(m_occlusionOutputPipeline);
        m_device.Destroy(m_reflectionOutputPipeline);
        m_device.Destroy(m_aberrationOutputPipeline);
        m_device.Destroy(m_prepassMaskLayout);
        m_device.Destroy(m_backgroundLayout);
        m_device.Destroy(m_skyLayout);
        m_device.Destroy(m_outputLayout);
        m_device.Destroy(m_reflectionOutputLayout);
        m_device.Destroy(m_outputBlockLayout);
        m_device.Destroy(m_frameLayout);
        m_device.Destroy(m_viewLayout);
        m_device.Destroy(m_drawLayout);
        for (const TextureHandle texture : m_defaultTextures)
        {
            m_device.Destroy(texture);
        }
        m_device.Destroy(m_ltcMatrix);
        m_device.Destroy(m_ltcAmplitude);
        m_device.Destroy(m_defaultSampler);
        m_device.Destroy(m_outputSampler);
        m_device.Destroy(m_skySampler);
        m_device.Destroy(m_shadowSampler);
        m_device.Destroy(m_noShadow);
        m_device.Destroy(m_defaultStreams);
    }

    std::uint32_t SceneRenderer::NextId()
    {
        return m_nextId++;
    }

    BindingGroupLayoutHandle SceneRenderer::MaterialLayoutOf(const ShaderVariant& variant)
    {
        BindingGroupLayoutDesc desc;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set == MaterialSet)
            {
                desc.entries.push_back({binding.binding, binding.kind});
            }
        }
        if (desc.entries.empty())
        {
            return {};
        }
        return m_device.CreateBindingGroupLayout(desc, "Material");
    }

    PipelineHandle SceneRenderer::MakeInternalPipeline(const ShaderContainer& container, const ShaderVariant* variant,
                                                       BindingGroupLayoutHandle layout, TextureFormat colour,
                                                       bool depthTested, std::uint32_t samples,
                                                       BindingGroupLayoutHandle setOneLayout,
                                                       BindingGroupLayoutHandle setZeroLayout)
    {
        PipelineDesc desc;
        desc.shader = variant;
        desc.samples = samples;
        desc.sets.at(0) = setZeroLayout;
        desc.sets.at(1) = setOneLayout;
        desc.sets.at(MaterialSet) = layout;
        desc.cull = CullMode::None;
        desc.colourFormats = {colour};
        desc.depth.test = depthTested;
        desc.depth.write = false;
        if (!depthTested)
        {
            desc.depthFormat.reset();
        }
        return MakeBuiltinPipeline(m_device, m_logger, desc, container.name);
    }

    SamplerHandle SceneRenderer::SamplerFor(const SamplerDescription& description)
    {
        const auto found = std::find_if(m_samplers.begin(), m_samplers.end(),
                                        [&](const auto& made) { return made.first == description; });
        if (found != m_samplers.end())
        {
            return found->second;
        }
        const SamplerHandle sampler = m_device.CreateSampler(ToSamplerDesc(description), "Material sampler");
        m_samplers.emplace_back(description, sampler);
        return sampler;
    }

    // ---- Prepare ----

    bool SceneRenderer::IsImageCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_images.find(key);
        return found != m_images.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareImage(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        ReleaseImage(key);
        ImageEntry& entry = m_images[key];
        entry.revision = revision;
        TextureDesc desc;
        desc.format = TextureFormatOf(image);
        desc.width = image.width;
        desc.height = image.height;
        desc.mipLevels = MipCount(image.width, image.height);
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        entry.texture = m_device.CreateTexture(desc, "Material image");
        entry.desc = desc;

        WriteWithMips(m_device, entry.texture, 0, desc.mipLevels, image.pixels, image.width, image.height,
                      CodecOf(image));
        ++m_stats.uploads;
    }

    void SceneRenderer::ReleaseImage(AssetKey key)
    {
        const auto found = m_images.find(key);
        if (found != m_images.end())
        {
            m_device.Destroy(found->second.texture);
            m_images.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::ImageKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_images)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsCubemapCurrent(AssetKey key, std::uint32_t revision) const
    {
        return m_skyImages->IsCubemapCurrent(key, revision);
    }

    void SceneRenderer::PrepareCubemap(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        m_skyImages->PrepareCubemap(key, revision, image);
        ++m_stats.uploads;
    }

    void SceneRenderer::ReleaseCubemap(AssetKey key)
    {
        m_skyImages->ReleaseCubemap(key);
    }

    bool SceneRenderer::IsPanoramaCurrent(AssetKey key, std::uint32_t revision) const
    {
        return m_skyImages->IsPanoramaCurrent(key, revision);
    }

    void SceneRenderer::PreparePanorama(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        m_skyImages->PreparePanorama(key, revision, image);
        ++m_stats.uploads;
    }

    void SceneRenderer::ReleasePanorama(AssetKey key)
    {
        m_skyImages->ReleasePanorama(key);
    }

    std::vector<AssetKey> SceneRenderer::PanoramaKeys() const
    {
        return m_skyImages->PanoramaKeys();
    }

    BindingGroupHandle SceneRenderer::SkyGroup(TextureHandle cube, std::uint64_t frameIndex)
    {
        return m_skyGroups.Get({BindingGroupKeyPart::Of(cube)}, frameIndex, [&] {
            BindingGroupEntry binding;
            binding.binding = 0;
            binding.texture = cube;
            binding.sampler = m_skySampler;
            return m_device.CreateBindingGroup({m_skyLayout, {binding}}, "Sky");
        });
    }

    std::vector<AssetKey> SceneRenderer::CubemapKeys() const
    {
        return m_skyImages->CubemapKeys();
    }

    TextureHandle SceneRenderer::SampledTexture(AssetKey key) const
    {
        if (const auto target = m_targets.find(key); target != m_targets.end())
        {
            return target->second.texture;
        }
        const auto image = m_images.find(key);
        return image == m_images.end() ? TextureHandle{} : image->second.texture;
    }

    TextureHandle SceneRenderer::GetDefaultTexture(DefaultTexture texture) const
    {
        return m_defaultTextures.at(static_cast<std::size_t>(texture));
    }

    bool SceneRenderer::IsTargetCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_targets.find(key);
        return found != m_targets.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareTarget(AssetKey key, std::uint32_t revision, PixelSize size)
    {
        ReleaseTarget(key);
        TargetEntry& entry = m_targets[key];
        entry.revision = revision;
        entry.size = size;
        TextureDesc desc;
        desc.format = TextureFormat::Rgba8Srgb;
        desc.width = size.width;
        desc.height = size.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        entry.texture = m_device.CreateTexture(desc, "Render target image");
    }

    void SceneRenderer::ReleaseTarget(AssetKey key)
    {
        const auto found = m_targets.find(key);
        if (found != m_targets.end())
        {
            m_device.Destroy(found->second.texture);
            m_targets.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::TargetKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_targets)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsShaderCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_shaders.find(key);
        return found != m_shaders.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader)
    {
        ReleaseShader(key);
        ShaderContainerResult read = ReadShaderContainer(shader.bytes);
        if (!read.Ok() || read.container->materialSet != MaterialSet)
        {
            m_logger.Error("FenrirRenderer: shader {} cannot draw meshes: {}", key.index,
                           read.Ok() ? "it is a 2D shader" : read.diagnostic);
        }
        // Kept even when unusable, so it is not read again until its revision changes; its materials draw as the
        // fallback.
        ShaderEntry& entry = m_shaders[key];
        entry.revision = revision;
        entry.id = NextId();
        if (read.Ok() && read.container->materialSet == MaterialSet)
        {
            // The built-in mesh programs have a compute path; a user's SKINNED variant may do more than skin, so it is
            // drawn as it is (ADR-0050). Told by the embedded bytes, which a user shader cannot share by name.
            const std::span<const std::byte> bytes(shader.bytes);
            entry.computeSkinned = std::ranges::any_of(
                std::array{BuiltinProgram::Lit, BuiltinProgram::Unlit, BuiltinProgram::Fallback},
                [&](BuiltinProgram program) { return std::ranges::equal(GetBuiltinShader(program).bytes, bytes); });
            entry.container = std::move(*read.container);
            entry.variant = BaseVariant(entry.container);
            for (const ShaderVariant& variant : entry.container.variants)
            {
                entry.variants.push_back({&variant, MaterialLayoutOf(variant)});
                if (&variant == entry.variant)
                {
                    entry.materialLayout = entry.variants.back().materialLayout;
                }
            }
        }
    }

    void SceneRenderer::ReleaseShader(AssetKey key)
    {
        const auto found = m_shaders.find(key);
        if (found == m_shaders.end())
        {
            return;
        }
        const ShaderEntry& shader = found->second;
        // Its materials' groups use its layout: they are made again once the shader is.
        for (auto it = m_materials.begin(); it != m_materials.end();)
        {
            if (it->second.shader == &shader)
            {
                DestroyMaterial(it->second);
                it = m_materials.erase(it);
            }
            else
            {
                ++it;
            }
        }
        DestroyPipelinesOf(shader);
        for (const VariantEntry& variant : shader.variants)
        {
            m_device.Destroy(variant.materialLayout);
        }
        m_shaders.erase(found);
    }

    void SceneRenderer::DestroyPipelinesOf(const ShaderEntry& shader)
    {
        for (auto it = m_pipelines.begin(); it != m_pipelines.end();)
        {
            if (it->first.shader == &shader)
            {
                m_device.Destroy(it->second.pipeline);
                it = m_pipelines.erase(it);
            }
            else
            {
                ++it;
            }
        }
        // The depth prepass's and the shadow passes' pipelines, which run its vertex stages.
        const auto destroyOf = [&](auto& pipelines) {
            std::erase_if(pipelines, [&](auto& made) {
                if (made.first.shader != &shader)
                {
                    return false;
                }
                m_device.Destroy(made.second.pipeline.pipeline);
                return true;
            });
        };
        destroyOf(m_prepassPipelines);
        destroyOf(m_shadowPipelines);
    }

    bool SceneRenderer::IsMeshCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_meshes.find(key);
        return found != m_meshes.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh)
    {
        ReleaseMesh(key);
        MeshEntry& entry = m_meshes[key];
        entry.revision = revision;
        entry.id = NextId();

        // Each stream the mesh has, back to back in one buffer.
        std::vector<std::byte> vertices;
        const auto append = [&](std::uint32_t location, const auto& stream) {
            if (stream.empty())
            {
                return;
            }
            entry.streams.at(location) = vertices.size();
            const std::span<const std::byte> bytes = std::as_bytes(std::span(stream));
            vertices.insert(vertices.end(), bytes.begin(), bytes.end());
        };
        append(0, mesh.positions);
        append(1, mesh.normals);
        append(2, mesh.tangents);
        append(3, mesh.uv0);
        append(4, mesh.uv1);
        append(5, mesh.colours);
        if (mesh.skin.has_value())
        {
            append(6, mesh.skin->joints);
            append(7, mesh.skin->weights);
            entry.skinned = true;
            entry.boneCount = mesh.skin->boneCount;
        }
        entry.vertexCount = static_cast<std::uint32_t>(mesh.positions.size());
        entry.bounds = mesh.bounds;
        if (!vertices.empty())
        {
            // A skinned mesh's is also builtin:skinning's source, read as storage (ADR-0050).
            const BufferUsage usage = entry.skinned ? BufferUsage::Vertex | BufferUsage::Storage : BufferUsage::Vertex;
            entry.vertices = m_device.CreateBuffer({vertices.size(), usage}, vertices, "Mesh vertices");
            if (entry.skinned)
            {
                BindingGroupEntry source;
                source.binding = 0;
                source.buffer = entry.vertices;
                entry.skinSource = m_device.CreateBindingGroup({m_skinSourceLayout, {source}}, "Skin source");
            }
        }
        if (!mesh.indices.empty())
        {
            const std::span<const std::byte> indices = std::as_bytes(std::span(mesh.indices));
            entry.indices = m_device.CreateBuffer({indices.size(), BufferUsage::Index}, indices, "Mesh indices");
            entry.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        }
        ++m_stats.uploads;
    }

    void SceneRenderer::DestroyMesh(MeshEntry& mesh)
    {
        m_device.Destroy(mesh.skinSource);
        m_device.Destroy(mesh.vertices);
        m_device.Destroy(mesh.indices);
    }

    void SceneRenderer::ReleaseMesh(AssetKey key)
    {
        const auto found = m_meshes.find(key);
        if (found != m_meshes.end())
        {
            DestroyMesh(found->second);
            m_meshes.erase(found);
        }
    }

    bool SceneRenderer::IsMaterialCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_materials.find(key);
        if (found == m_materials.end() || found->second.revision != revision)
        {
            return false;
        }
        const MaterialEntry& material = found->second;
        const auto shader = m_shaders.find(material.shaderKey);
        const ShaderEntry* now = shader == m_shaders.end() ? nullptr : &shader->second;
        if (now != material.shader || (now != nullptr && now->revision != material.shaderRevision))
        {
            return false;
        }
        // An image or render target it samples was made, remade (a new revision or size) or released since.
        return std::all_of(material.images.begin(), material.images.end(),
                           [this](const SampledImage& image) { return SampledTexture(image.image) == image.bound; });
    }

    void SceneRenderer::PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material)
    {
        ReleaseMaterial(key);
        MaterialEntry& entry = m_materials[key];
        entry.revision = revision;
        entry.id = NextId();
        entry.doubleSided = material.doubleSided;
        entry.blendMode = material.blendMode;
        entry.shaderKey = KeyOf(material.shader);
        const auto shader = m_shaders.find(entry.shaderKey);
        entry.shader = shader == m_shaders.end() ? nullptr : &shader->second;
        entry.shaderRevision = entry.shader == nullptr ? 0 : entry.shader->revision;
        if (entry.shader == nullptr || entry.shader->variant == nullptr)
        {
            // Drawn as the fallback material, which has no material group.
            return;
        }
        // The renderer picks the keys (ADR-0024): ALPHA_MASK for a Masked material, if the shader has it; a user
        // shader's own keys stay off.
        const bool masked = material.blendMode == Fenrir::BlendMode::Masked;
        const std::array<std::string_view, 1> maskKeys{AlphaMaskKey};
        const ShaderVariant* picked = entry.shader->container.FindVariant(
            masked ? std::span<const std::string_view>(maskKeys) : std::span<const std::string_view>());
        const auto variantEntry =
            std::find_if(entry.shader->variants.begin(), entry.shader->variants.end(), [&](const VariantEntry& v) {
                return v.variant == (picked != nullptr ? picked : entry.shader->variant);
            });
        entry.variant = &*variantEntry;
        const ShaderVariant* skinned =
            SkinnedVariant(entry.shader->container,
                           masked ? std::span<const std::string_view>(maskKeys) : std::span<const std::string_view>());
        for (const VariantEntry& v : entry.shader->variants)
        {
            if (skinned != nullptr && v.variant == skinned)
            {
                entry.skinnedVariant = &v;
            }
        }
        const ShaderVariant& variant = *entry.variant->variant;

        std::vector<BindingGroupEntry> bindings;
        // The block as written, for the prepass's base colour alpha.
        std::vector<std::byte> written;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set != MaterialSet)
            {
                continue;
            }
            BindingGroupEntry bound;
            bound.binding = binding.binding;
            if (binding.kind == ShaderBindingKind::Texture)
            {
                // A texture left unset, or naming an image that is not prepared, binds its parameter's default texture
                // (ADR-0024) instead of adding a key.
                const TextureHandle fallback =
                    m_defaultTextures.at(static_cast<std::size_t>(DefaultTextureOf(variant, binding.name)));
                bound.texture = fallback;
                bound.sampler = m_defaultSampler;
                const auto value = material.overrides.find(binding.name);
                const MaterialTexture* texture =
                    value == material.overrides.end() ? nullptr : std::get_if<MaterialTexture>(&value->second);
                if (texture != nullptr)
                {
                    bound.sampler = SamplerFor(texture->sampler);
                }
                if (texture != nullptr && !texture->image.IsEmpty())
                {
                    // A render target image binds its texture (ADR-0026); an image with pixels its uploaded texture.
                    SampledImage image{bindings.size(), KeyOf(texture->image), SampledTexture(KeyOf(texture->image)),
                                       fallback};
                    if (!image.bound.IsEmpty())
                    {
                        bound.texture = image.bound;
                    }
                    entry.images.push_back(image);
                }
            }
            else if (variant.materialBlockBinding == binding.binding && !variant.defaultBlock.empty())
            {
                // The default block image, then the material's overrides over it (ADR-0024), and a Masked material's
                // alpha cutoff over those.
                std::vector<std::byte> block = variant.defaultBlock;
                WriteOverrides(variant, material, block);
                if (masked)
                {
                    WriteAlphaCutoff(variant, material.alphaCutoff, block);
                }
                entry.block = m_device.CreateBuffer({block.size(), BufferUsage::Uniform}, block, "Material block");
                bound.buffer = entry.block;
                written = std::move(block);
                ++m_stats.uploads;
            }
            bindings.push_back(bound);
        }
        entry.layout = entry.variant->materialLayout;
        entry.bindings = bindings;
        if (!entry.layout.IsEmpty())
        {
            entry.group = m_device.CreateBindingGroup({entry.layout, std::move(bindings)}, "Material");
        }
        if (masked)
        {
            PrepareMaskedPrepass(entry, variant, material, written);
        }
    }

    void SceneRenderer::PrepareMaskedPrepass(MaterialEntry& entry, const ShaderVariant& variant,
                                             const MaterialData& material, std::span<const std::byte> block)
    {
        // The prepass cuts a Masked item out as builtin:lit does (ADR-0049), by its base colour's alpha and texture; a
        // shader with neither is cut out only by its own fragment stage, in Opaque, which then writes its depth.
        const auto colour = std::find_if(
            variant.materialMembers.begin(), variant.materialMembers.end(),
            [](const ShaderBlockMember& m) { return m.name == BaseColourMember && m.type == ShaderValueType::Vec4; });
        const auto texture =
            std::find_if(entry.bindings.begin(), entry.bindings.end(), [&](const BindingGroupEntry& e) {
                return std::ranges::any_of(variant.bindings, [&](const ShaderBinding& b) {
                    return b.set == MaterialSet && b.binding == e.binding && b.kind == ShaderBindingKind::Texture &&
                           b.name == BaseColourTexture;
                });
            });
        const bool hasColour = colour != variant.materialMembers.end() && colour->offset + 16 <= block.size();
        entry.prepassed = hasColour || texture != entry.bindings.end();
        if (!entry.prepassed)
        {
            return;
        }
        PrepassMaskBlock mask;
        mask.cutoff = material.alphaCutoff;
        if (hasColour)
        {
            std::memcpy(&mask.alpha, block.subspan(colour->offset + 12).data(), sizeof(float));
        }
        entry.prepassBlock = m_device.CreateBuffer({sizeof(mask), BufferUsage::Uniform}, BytesOf(mask), "Prepass mask");
        BindingGroupEntry blockEntry;
        blockEntry.binding = 0;
        blockEntry.buffer = entry.prepassBlock;
        BindingGroupEntry textureEntry;
        textureEntry.binding = 1;
        textureEntry.texture = m_defaultTextures.at(static_cast<std::size_t>(DefaultTexture::White));
        textureEntry.sampler = m_defaultSampler;
        if (texture != entry.bindings.end())
        {
            const auto index = static_cast<std::size_t>(texture - entry.bindings.begin());
            textureEntry.texture = texture->texture;
            textureEntry.sampler = texture->sampler;
            for (std::size_t i = 0; i < entry.images.size(); ++i)
            {
                if (entry.images[i].binding == index)
                {
                    entry.prepassImage = i;
                }
            }
        }
        entry.prepassBindings = {blockEntry, textureEntry};
        entry.prepassGroup = m_device.CreateBindingGroup({m_prepassMaskLayout, entry.prepassBindings}, "Prepass mask");
    }

    BindingGroupHandle SceneRenderer::PrepassGroup(MaterialEntry& material, const std::vector<AssetKey>& replaced)
    {
        if (!material.prepassImage.has_value())
        {
            return material.prepassGroup;
        }
        const SampledImage& image = material.images.at(*material.prepassImage);
        if (image.bound.IsEmpty() || std::find(replaced.begin(), replaced.end(), image.image) == replaced.end())
        {
            return material.prepassGroup;
        }
        // The view may not sample the image (ADR-0026): its default texture, as the material's own group has.
        if (material.prepassWithout.IsEmpty())
        {
            std::vector<BindingGroupEntry> bindings = material.prepassBindings;
            bindings.at(1).texture = image.fallback;
            bindings.at(1).sampler = m_defaultSampler;
            material.prepassWithout =
                m_device.CreateBindingGroup({m_prepassMaskLayout, std::move(bindings)}, "Prepass mask");
        }
        return material.prepassWithout;
    }

    BindingGroupHandle SceneRenderer::GroupWithout(MaterialEntry& material, const std::vector<AssetKey>& replaced)
    {
        std::vector<AssetKey> mine;
        for (const SampledImage& image : material.images)
        {
            if (!image.bound.IsEmpty() && std::find(replaced.begin(), replaced.end(), image.image) != replaced.end())
            {
                mine.push_back(image.image);
            }
        }
        if (mine.empty())
        {
            return material.group;
        }
        std::sort(mine.begin(), mine.end());
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        if (const auto found = material.withoutTargets.find(mine); found != material.withoutTargets.end())
        {
            return found->second;
        }
        std::vector<BindingGroupEntry> bindings = material.bindings;
        for (const SampledImage& image : material.images)
        {
            if (std::binary_search(mine.begin(), mine.end(), image.image))
            {
                bindings.at(image.binding).texture = image.fallback;
                bindings.at(image.binding).sampler = m_defaultSampler;
            }
        }
        const BindingGroupHandle group =
            m_device.CreateBindingGroup({material.layout, std::move(bindings)}, "Material");
        material.withoutTargets.emplace(std::move(mine), group);
        return group;
    }

    void SceneRenderer::DestroyMaterial(MaterialEntry& material)
    {
        for (const auto& [replaced, group] : material.withoutTargets)
        {
            m_device.Destroy(group);
        }
        m_device.Destroy(material.group);
        m_device.Destroy(material.block);
        m_device.Destroy(material.prepassWithout);
        m_device.Destroy(material.prepassGroup);
        m_device.Destroy(material.prepassBlock);
    }

    void SceneRenderer::ReleaseMaterial(AssetKey key)
    {
        const auto found = m_materials.find(key);
        if (found != m_materials.end())
        {
            DestroyMaterial(found->second);
            m_materials.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::ShaderKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_shaders)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsInstanceListCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_instanceLists.find(key);
        return found != m_instanceLists.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareInstanceList(AssetKey key, std::uint32_t revision, const InstanceListData& list,
                                            const Math::AABB& meshBounds)
    {
        InstanceListEntry& entry = m_instanceLists[key];
        entry.revision = revision;
        entry.range.count = static_cast<std::uint32_t>(list.transforms.size());
        entry.meshBounds = meshBounds;
        entry.bounds = CopiesBounds(list.transforms, meshBounds);
        entry.records.resize(list.transforms.size() * sizeof(InstanceBlock));
        for (std::size_t i = 0; i < list.transforms.size(); ++i)
        {
            const Math::Mat4& transform = list.transforms[i];
            const Math::Mat3 normal = glm::transpose(glm::inverse(Math::Mat3(transform)));
            InstanceBlock block;
            block.transform = transform;
            block.normal = {Math::Vec4(normal[0], 0.0f), Math::Vec4(normal[1], 0.0f), Math::Vec4(normal[2], 0.0f)};
            if (!list.colours.empty())
            {
                const Colour& colour = list.colours[i];
                block.colour = {colour.r, colour.g, colour.b, colour.a};
            }
            std::memcpy(entry.records.data() + (i * sizeof(InstanceBlock)), &block, sizeof(InstanceBlock));
        }
        m_instancesChanged = true;
        ++m_stats.uploads;
    }

    std::optional<Math::AABB> SceneRenderer::InstanceListBounds(AssetKey key, const Math::AABB& meshBounds)
    {
        const auto found = m_instanceLists.find(key);
        if (found == m_instanceLists.end() || found->second.range.count == 0)
        {
            return std::nullopt;
        }
        InstanceListEntry& entry = found->second;
        if (entry.meshBounds.min != meshBounds.min || entry.meshBounds.max != meshBounds.max)
        {
            // Another mesh than the list was uploaded with: its copies' transforms are read back from their records.
            std::vector<Math::Mat4> transforms(entry.range.count);
            for (std::size_t i = 0; i < transforms.size(); ++i)
            {
                std::memcpy(&transforms[i], entry.records.data() + (i * sizeof(InstanceBlock)), sizeof(Math::Mat4));
            }
            entry.meshBounds = meshBounds;
            entry.bounds = CopiesBounds(transforms, meshBounds);
        }
        return entry.bounds;
    }

    void SceneRenderer::ReleaseInstanceList(AssetKey key)
    {
        if (m_instanceLists.erase(key) > 0)
        {
            m_instancesChanged = true;
        }
    }

    std::vector<AssetKey> SceneRenderer::InstanceListKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_instanceLists)
        {
            keys.push_back(key);
        }
        return keys;
    }

    void SceneRenderer::PackInstanceLists()
    {
        if (!m_instancesChanged)
        {
            return;
        }
        // Every list again, into a new buffer: the old one is freed once the frames drawing from it are done, so no
        // frame in flight reads a list half rewritten. Lists change rarely; a static field packs once.
        std::size_t total = 0;
        for (auto& [key, entry] : m_instanceLists)
        {
            entry.range.first = static_cast<std::uint32_t>(total / sizeof(InstanceBlock));
            total += entry.records.size();
        }
        std::vector<std::byte> packed(std::max(total, sizeof(InstanceBlock)));
        for (const auto& [key, entry] : m_instanceLists)
        {
            std::ranges::copy(entry.records, packed.begin() + static_cast<std::ptrdiff_t>(entry.range.first) *
                                                                  static_cast<std::ptrdiff_t>(sizeof(InstanceBlock)));
        }
        m_device.Destroy(m_instanceListBuffer);
        m_instanceListBuffer = m_device.CreateBuffer({packed.size(), BufferUsage::Storage}, packed, "Instance lists");
        m_instanceListBytes = packed.size();
        m_instancesChanged = false;
    }

    std::vector<AssetKey> SceneRenderer::MeshKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_meshes)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<AssetKey> SceneRenderer::MaterialKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_materials)
        {
            keys.push_back(key);
        }
        return keys;
    }

    // ---- The frame ----

    void SceneRenderer::SetFrame(SceneFrame frame)
    {
        m_frame = std::move(frame);
    }

    void SceneRenderer::SetSpotShadowBudget(std::uint32_t budget)
    {
        m_spotShadows->SetBudget(budget);
    }

    void SceneRenderer::SetPointShadowBudget(std::uint32_t budget)
    {
        m_pointShadows->SetBudget(budget);
    }

    SceneStats SceneRenderer::TakeStats()
    {
        const SceneStats stats = m_stats;
        m_stats.uploads = 0;
        return stats;
    }

    const SceneRenderer::PipelineEntry& SceneRenderer::MeshPipeline(const ShaderEntry& shader,
                                                                    const VariantEntry& variant, std::uint32_t streams,
                                                                    Fenrir::BlendMode blend, bool doubleSided,
                                                                    bool prepassed)
    {
        const std::uint32_t read = variant.variant != nullptr ? ReadMask(*variant.variant) : 0;
        const PipelineKey key{&shader, variant.variant, read & streams, blend, doubleSided, m_samples, prepassed};
        if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
        {
            return found->second;
        }

        PipelineEntry& entry = m_pipelines[key];
        entry.id = NextId();
        if (variant.variant == nullptr)
        {
            entry.failure = "it has no variant to draw";
            return entry;
        }
        entry.failure = CheckFixedSets(*variant.variant);
        PipelineDesc desc;
        desc.shader = variant.variant;
        desc.sets = {m_frameLayout, m_viewLayout, variant.materialLayout, m_drawLayout};
        if (std::string streams = VertexBuffersOf(*variant.variant, key.streams, desc.vertexBuffers, entry.locations);
            !streams.empty())
        {
            entry.failure = std::move(streams);
        }
        // The render state (ADR-0024): Opaque and Masked write depth; AlphaBlend and Additive blend without writing it.
        // Depth is always tested, greater-or-equal for reversed-Z, and doubleSided turns culling off. Over the depth
        // prepass, Opaque and Masked test for equality instead, and write none (ADR-0049).
        desc.cull = doubleSided ? CullMode::None : CullMode::Back;
        desc.depth.write = !IsTransparent(blend) && !prepassed;
        if (prepassed)
        {
            desc.depth.compare = CompareOp::Equal;
        }
        desc.blend = DeviceBlendOf(blend);
        desc.colourFormats = {TextureFormat::Rgba16Float};
        desc.samples = m_samples;
        if (entry.failure.empty())
        {
            PipelineResult made = m_device.CreatePipeline(desc, shader.container.name);
            entry.pipeline = made.pipeline;
            entry.failure = std::move(made.diagnostic);
        }
        if (!entry.failure.empty())
        {
            m_logger.Error("FenrirRenderer: no pipeline for shader '{}', so its materials draw as the fallback "
                           "material: {}",
                           shader.container.name, entry.failure);
        }
        return entry;
    }

    void SceneRenderer::MakeBehindVertexStage(PrepassEntry& made, const VariantEntry& variant, std::uint32_t streams,
                                              const ShaderContainer& program, bool masked, PipelineDesc desc,
                                              std::string_view label, bool point)
    {
        PipelineEntry& entry = made.pipeline;
        entry.id = NextId();
        std::vector<std::string_view> ourKeys;
        if (masked)
        {
            ourKeys.push_back(AlphaMaskKey);
        }
        if (point)
        {
            ourKeys.push_back(PointShadowKey);
        }
        const ShaderVariant* ours = program.FindVariant(ourKeys);
        const auto stageOf = [](const ShaderVariant* of, ShaderStage stage) -> const ShaderStageCode* {
            if (of == nullptr)
            {
                return nullptr;
            }
            const auto found = std::ranges::find(of->stages, stage, &ShaderStageCode::stage);
            return found == of->stages.end() ? nullptr : &*found;
        };
        const ShaderStageCode* vertex = stageOf(variant.variant, ShaderStage::Vertex);
        const ShaderStageCode* fragment = stageOf(ours, ShaderStage::Fragment);
        if (vertex == nullptr || fragment == nullptr)
        {
            entry.failure = vertex == nullptr ? "it has no vertex stage" : std::format("{} did not read", program.name);
            return;
        }
        // The item's vertex stage, so it lands where its other passes' do (Fenrir/Vertex.glsl's gl_Position is
        // invariant), placed by whatever view block the pass binds; then ours. Its keys, as the probes read them, are
        // the item's.
        made.variant = std::make_unique<ShaderVariant>();
        made.variant->keys = variant.variant->keys;
        made.variant->stages = {*vertex, *fragment};
        made.variant->vertexInputs = variant.variant->vertexInputs;
        made.variant->bindings = variant.variant->bindings;
        entry.failure = CheckFixedSets(*variant.variant);
        desc.shader = made.variant.get();
        // A Masked item's set 2 is its prepass mask group, not its material's.
        desc.sets = {m_frameLayout, m_viewLayout, masked ? m_prepassMaskLayout : variant.materialLayout, m_drawLayout};
        if (std::string missing = VertexBuffersOf(*variant.variant, streams, desc.vertexBuffers, entry.locations);
            !missing.empty())
        {
            entry.failure = std::move(missing);
        }
        if (entry.failure.empty())
        {
            PipelineResult result = m_device.CreatePipeline(desc, label);
            entry.pipeline = result.pipeline;
            entry.failure = std::move(result.diagnostic);
        }
    }

    const SceneRenderer::PipelineEntry& SceneRenderer::PrepassPipeline(const ShaderEntry& shader,
                                                                       const VariantEntry& variant,
                                                                       std::uint32_t streams, bool masked,
                                                                       bool doubleSided)
    {
        const std::uint32_t read = variant.variant != nullptr ? ReadMask(*variant.variant) : 0;
        const PrepassKey key{&shader, variant.variant, read & streams, masked, doubleSided};
        if (const auto found = m_prepassPipelines.find(key); found != m_prepassPipelines.end())
        {
            return found->second.pipeline;
        }
        PrepassEntry& made = m_prepassPipelines[key];
        PipelineDesc desc;
        desc.cull = doubleSided ? CullMode::None : CullMode::Back;
        desc.colourFormats = {TextureFormat::Rg16Float};
        desc.samples = m_samples;
        MakeBehindVertexStage(made, variant, key.streams, m_prepass, masked, desc, "DepthPrepass");
        if (!made.pipeline.failure.empty())
        {
            m_logger.Warn("FenrirRenderer: no depth prepass pipeline for shader '{}', so Opaque writes its items' "
                          "depth and they are missing from the view's normals: {}",
                          shader.container.name, made.pipeline.failure);
        }
        return made.pipeline;
    }

    const SceneRenderer::PipelineEntry& SceneRenderer::ShadowPipeline(const ShaderEntry& shader,
                                                                      const VariantEntry& variant,
                                                                      std::uint32_t streams, bool masked, bool point)
    {
        const std::uint32_t read = variant.variant != nullptr ? ReadMask(*variant.variant) : 0;
        const ShadowKey key{&shader, variant.variant, read & streams, masked, point};
        if (const auto found = m_shadowPipelines.find(key); found != m_shadowPipelines.end())
        {
            return found->second.pipeline;
        }
        PrepassEntry& made = m_shadowPipelines[key];
        // Both faces cast, so an open mesh or a plane shadows from either side; depth only, reversed-Z.
        PipelineDesc desc;
        desc.cull = CullMode::None;
        desc.samples = 1;
        MakeBehindVertexStage(made, variant, key.streams, m_shadow, masked, desc, point ? "PointShadow" : "Shadow",
                              point);
        if (!made.pipeline.failure.empty())
        {
            m_logger.Warn("FenrirRenderer: no shadow pipeline for shader '{}', so its items cast no shadow: {}",
                          shader.container.name, made.pipeline.failure);
        }
        return made.pipeline;
    }

    const SceneRenderer::PipelineEntry* SceneRenderer::LayeredShadowPipeline(const ShaderEntry& shader,
                                                                             const VariantEntry& variant,
                                                                             std::uint32_t streams, bool masked)
    {
        if (!m_device.Features().vertexLayer || variant.variant == nullptr)
        {
            return nullptr;
        }
        const ShaderVariant* layered = PointShadowVariant(shader.container, *variant.variant);
        if (layered == nullptr)
        {
            return nullptr;
        }
        // The POINT_SHADOW variant's vertex stage, keyed as its own variant; its set 2 is the item's.
        const PipelineEntry& entry = ShadowPipeline(shader, {layered, variant.materialLayout}, streams, masked, true);
        return entry.pipeline.IsEmpty() ? nullptr : &entry;
    }

    std::vector<SceneRenderer::ResolvedDraw> SceneRenderer::ResolveDraws(std::vector<SkinDispatch>& skins,
                                                                         bool shadowed, bool pointShadowed)
    {
        // Each skinned mesh and palette range skinned once, however many draws read it (ADR-0050).
        std::map<std::pair<const MeshEntry*, std::uint32_t>, std::size_t> skinIndex;
        std::size_t skinnedBytes = 0;
        std::vector<ResolvedDraw> draws;
        draws.reserve(m_frame.draws.size());
        for (const DrawRequest& request : m_frame.draws)
        {
            const auto mesh = m_meshes.find(request.mesh);
            if (mesh == m_meshes.end() || mesh->second.indexCount == 0)
            {
                continue;
            }
            const InstanceListEntry* list = nullptr;
            if (!request.instances.IsEmpty())
            {
                const auto found = m_instanceLists.find(request.instances);
                if (found == m_instanceLists.end() || found->second.range.count == 0)
                {
                    continue;
                }
                list = &found->second;
            }
            // Only a mesh with skin data draws skinned; the facade asks for it only then.
            const bool skinned = request.skinned && mesh->second.skinned;
            if (skinned && std::uint64_t{request.paletteOffset} + mesh->second.boneCount > m_frame.palette.size())
            {
                m_logger.Error("FenrirRenderer: internal error: a skinned draw of mesh {} reads palette entries {} to "
                               "{}, past the {} written this frame, so it is not drawn",
                               request.mesh.index, request.paletteOffset,
                               std::uint64_t{request.paletteOffset} + mesh->second.boneCount, m_frame.palette.size());
                continue;
            }
            const auto material = m_materials.find(request.material);
            MaterialEntry* found = material == m_materials.end() ? nullptr : &material->second;
            bool usable = found != nullptr && found->variant != nullptr;
            if (skinned && usable && found->skinnedVariant == nullptr)
            {
                // Never drawn unskinned (ADR-0035): the fallback material's SKINNED variant draws it instead.
                if (m_loggedUnskinned.insert(found->shaderKey).second)
                {
                    m_logger.Warn("FenrirRenderer: shader '{}' has no SKINNED variant, so its skinned meshes are drawn "
                                  "with the fallback material",
                                  found->shader->container.name);
                }
                usable = false;
            }
            // Compute-skinned (ADR-0050), it draws its shader's variant without SKINNED, rigidly from the skinned
            // vertex buffer's positions, normals and tangents; else its SKINNED variant skins it.
            const ShaderEntry* drawnBy = usable ? found->shader : &m_fallback;
            const bool computeSkinned = skinned && drawnBy->computeSkinned && m_computeSkinning &&
                                        !m_skinningPipeline.IsEmpty() &&
                                        mesh->second.streams.at(JointLocation).has_value() &&
                                        mesh->second.streams.at(WeightLocation).has_value();
            const bool vertexSkinned = skinned && !computeSkinned;
            const std::uint32_t streams = StreamsOf(mesh->second.streams) | (computeSkinned ? SkinnedStreams : 0u);
            const VariantEntry& fallbackVariant =
                vertexSkinned && m_fallback.variants.size() > 1 ? m_fallback.variants[1] : m_fallback.variants.front();
            const ShaderEntry* shader = usable ? found->shader : &m_fallback;
            const VariantEntry* variant =
                usable ? (vertexSkinned ? found->skinnedVariant : found->variant) : &fallbackVariant;
            Fenrir::BlendMode blend = usable ? found->blendMode : Fenrir::BlendMode::Opaque;
            bool doubleSided = usable && found->doubleSided;
            // Opaque and Masked items draw in the depth prepass (ADR-0049), but for Masked ones it cannot cut out.
            const auto prepassed = [&] { return !IsTransparent(blend) && (shader == &m_fallback || found->prepassed); };
            const PipelineEntry* pipeline = &MeshPipeline(*shader, *variant, streams, blend, doubleSided, prepassed());
            if (pipeline->pipeline.IsEmpty() && shader != &m_fallback)
            {
                shader = &m_fallback;
                variant = &fallbackVariant;
                blend = Fenrir::BlendMode::Opaque;
                doubleSided = false;
                pipeline = &MeshPipeline(m_fallback, *variant, streams, blend, doubleSided, true);
            }
            if (pipeline->pipeline.IsEmpty())
            {
                continue;
            }
            const PipelineEntry* prepass = nullptr;
            const bool masked = blend == Fenrir::BlendMode::Masked;
            // Opaque and Masked items cast (ADR-0050), a Masked one cut out by its prepass mask group if it has one.
            const PipelineEntry* shadow = nullptr;
            const bool shadowMasked = masked && shader != &m_fallback && found->prepassed;
            const PipelineEntry* pointShadow = nullptr;
            const PipelineEntry* pointShadowLayered = nullptr;
            if (shadowed && request.castShadows && !IsTransparent(blend))
            {
                shadow = &ShadowPipeline(*shader, *variant, streams, shadowMasked);
                if (shadow->pipeline.IsEmpty())
                {
                    shadow = nullptr;
                }
            }
            if (pointShadowed && request.castShadows && !IsTransparent(blend))
            {
                pointShadow = &ShadowPipeline(*shader, *variant, streams, shadowMasked, true);
                if (pointShadow->pipeline.IsEmpty())
                {
                    pointShadow = nullptr;
                }
                else
                {
                    pointShadowLayered = LayeredShadowPipeline(*shader, *variant, streams, shadowMasked);
                }
            }
            if (prepassed())
            {
                prepass = &PrepassPipeline(*shader, *variant, streams, masked, doubleSided);
                if (prepass->pipeline.IsEmpty())
                {
                    // Not in the prepass after all: Opaque writes its depth.
                    prepass = nullptr;
                    pipeline = &MeshPipeline(*shader, *variant, streams, blend, doubleSided, false);
                    if (pipeline->pipeline.IsEmpty())
                    {
                        continue;
                    }
                }
            }

            for (const ShaderVertexInput& input : variant->variant->vertexInputs)
            {
                if (input.location < Streams.size() && Streams.at(input.location).logMissing &&
                    !mesh->second.streams.at(input.location).has_value() &&
                    m_loggedStreams.insert({request.mesh, input.location}).second)
                {
                    const StreamFormat& stream = Streams.at(input.location);
                    m_logger.Warn("FenrirRenderer: mesh {} has no {}, which shader '{}' reads; it is drawn with {}",
                                  request.mesh.index, stream.name, shader->container.name, stream.defaultName);
                }
            }

            const bool fallback = shader == &m_fallback;
            ResolvedDraw draw;
            draw.pipeline = pipeline;
            draw.material = fallback ? nullptr : found;
            draw.mesh = &mesh->second;
            draw.world = request.world;
            draw.transparent = IsTransparent(blend);
            draw.paletteOffset = skinned ? request.paletteOffset : 0;
            draw.skinned = skinned;
            draw.sortPoint = request.sortPoint.value_or(Math::Vec3(request.world[3]));
            draw.prepass = prepass;
            draw.prepassMasked = prepass != nullptr && masked;
            draw.shadow = shadow;
            draw.shadowMasked = (shadow != nullptr || pointShadow != nullptr) && shadowMasked;
            draw.pointShadow = pointShadow;
            draw.pointShadowLayered = pointShadowLayered;
            draw.instances = list == nullptr ? InstanceRange{} : list->range;
            if (list == nullptr)
            {
                // The record carries the item's world bounds: its skinned bounds when the facade gave them (ADR-0035),
                // else its mesh's box under its world matrix.
                draw.recordBounds = request.bounds.value_or(Math::Moved(mesh->second.bounds, request.world));
                draw.worldBounds = draw.recordBounds;
            }
            else
            {
                // An instance list's record carries the mesh's local box, which each copy moves; the list as a whole
                // is culled by the box around every copy, under the entity's world matrix.
                draw.recordBounds = mesh->second.bounds;
                draw.worldBounds = Math::Moved(
                    InstanceListBounds(request.instances, mesh->second.bounds).value_or(mesh->second.bounds),
                    request.world);
            }
            if (computeSkinned)
            {
                const auto [skin, added] = skinIndex.try_emplace({&mesh->second, request.paletteOffset}, skins.size());
                if (added)
                {
                    skins.push_back({&mesh->second, request.paletteOffset, skinnedBytes});
                    // Each mesh's skinned vertices start on a 16-byte boundary.
                    skinnedBytes += (std::size_t{mesh->second.vertexCount} * SkinnedVertexBytes + 15) / 16 * 16;
                }
                draw.skinnedVertices = skins[skin->second].skinnedVertices;
            }
            const std::uint64_t stage = draw.transparent ? TransparentStage : OpaqueStage;
            const std::uint64_t materialId = fallback ? 0 : found->id;
            draw.sortKey = (stage << 56u) | ((std::uint64_t{pipeline->id} & 0xFFFFu) << 40u) |
                           ((materialId & 0xFFFFFu) << 20u) | (std::uint64_t{mesh->second.id} & 0xFFFFFu);
            draws.push_back(draw);
        }
        std::stable_sort(draws.begin(), draws.end(),
                         [](const ResolvedDraw& a, const ResolvedDraw& b) { return a.sortKey < b.sortKey; });
        return draws;
    }

    const SceneRenderer::PipelineEntry* SceneRenderer::PipelineOf(const ResolvedDraw& draw, DrawPass pass) noexcept
    {
        switch (pass)
        {
        case DrawPass::Prepass:
            return draw.prepass;
        case DrawPass::Shadow:
            return draw.shadow;
        case DrawPass::PointShadow:
            return draw.pointShadow;
        case DrawPass::PointShadowLayered:
            return draw.pointShadowLayered;
        default:
            return draw.pipeline;
        }
    }

    std::vector<SceneRenderer::DrawRun> SceneRenderer::RunsOf(const std::vector<ResolvedDraw>& draws,
                                                              const std::vector<BindingGroupHandle>& groups,
                                                              const std::vector<std::size_t>& order,
                                                              DrawPass pass) const
    {
        const auto pipelineOf = [&](std::size_t i) { return PipelineOf(draws[i], pass); };
        std::vector<DrawRun> runs;
        for (const std::size_t i : order)
        {
            // The sort key keeps matching items together, so a run is the next record of the same kind; each
            // instance then finds its own record, at the run's first plus its instance index (Fenrir/Draw.glsl).
            if (m_automaticInstancing && !runs.empty())
            {
                DrawRun& run = runs.back();
                const std::size_t last = run.first + run.count - 1;
                if (i == last + 1 && pipelineOf(i) == pipelineOf(last) && draws[i].material == draws[last].material &&
                    groups[i] == groups[last] && draws[i].mesh == draws[last].mesh &&
                    draws[i].skinnedVertices == draws[last].skinnedVertices && draws[i].instances.count == 0 &&
                    draws[last].instances.count == 0)
                {
                    ++run.count;
                    continue;
                }
            }
            runs.push_back({i, 1});
        }
        return runs;
    }

    void SceneRenderer::RecordDraws(PassEncoder& encoder, const std::vector<ResolvedDraw>& draws,
                                    const std::vector<BindingGroupHandle>& groups, const std::vector<DrawRun>& runs,
                                    BindingGroupHandle frameGroup, BindingGroupHandle viewGroup,
                                    BindingGroupHandle drawGroup, BufferHandle skinned, PixelRect viewport,
                                    DrawPass pass, BufferHandle commands) const
    {
        encoder.SetViewport(viewport);
        const PipelineEntry* current = nullptr;
        BindingGroupHandle material;
        for (std::size_t r = 0; r < runs.size(); ++r)
        {
            const DrawRun& run = runs[r];
            const std::size_t i = run.first;
            const ResolvedDraw& draw = draws[i];
            const BindingGroupHandle group = groups[i];
            const PipelineEntry* pipeline = PipelineOf(draw, pass);
            if (pipeline == nullptr)
            {
                // Not drawn in this pass (an item with no prepass draw): its command stays in the view's buffer.
                continue;
            }
            const bool newPipeline = pipeline != current;
            if (newPipeline)
            {
                current = pipeline;
                encoder.SetPipeline(current->pipeline);
                encoder.SetBindingGroup(0, frameGroup);
                encoder.SetBindingGroup(1, viewGroup);
                encoder.SetBindingGroup(3, drawGroup);
            }
            if (!group.IsEmpty() && (newPipeline || group != material))
            {
                encoder.SetBindingGroup(MaterialSet, group);
            }
            material = group;
            for (std::size_t slot = 0; slot < current->locations.size(); ++slot)
            {
                const std::uint32_t location = current->locations[slot];
                const std::optional<std::size_t>& offset = draw.mesh->streams.at(location);
                if (draw.skinnedVertices.has_value() && (SkinnedStreams & (1u << location)) != 0)
                {
                    // Its skinned positions, normals or tangents (ADR-0050).
                    const std::size_t stream = std::size_t{draw.mesh->vertexCount} * SkinnedStreamStarts.at(location);
                    encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), skinned, *draw.skinnedVertices + stream);
                }
                else if (offset.has_value())
                {
                    encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), draw.mesh->vertices, *offset);
                }
                else
                {
                    encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), m_defaultStreams,
                                            Streams.at(location).defaultOffset);
                }
            }
            encoder.SetIndexBuffer(draw.mesh->indices, IndexFormat::UInt32);
            if (!commands.IsEmpty())
            {
                // The run's command, counted into by the view's cull pass (ADR-0057): its instances read the view's
                // indirection from the command's base instance, which Fenrir/Draw.glsl composes as a direct draw's.
                encoder.DrawIndexedIndirect({.buffer = commands, .offset = r * sizeof(DrawIndexedIndirectCommand)});
                continue;
            }
            // Each item reads its own record of the draw buffer, by its place in the draw list; an instance list's
            // copies all read its one record, which Fenrir/Draw.glsl composes with each copy's. Drawing a point light's
            // cube faces as instances, each is six, one a face.
            const std::uint32_t instances = draw.instances.count == 0 ? run.count : draw.instances.count;
            encoder.DrawIndexed(
                {.indexCount = draw.mesh->indexCount,
                 .instanceCount = pass == DrawPass::PointShadowLayered ? instances * CubeFaceCount : instances,
                 .firstInstance = static_cast<std::uint32_t>(i)});
        }
    }

    GraphResource SceneRenderer::AddSkinningPass(RenderGraph& graph, std::uint64_t frameIndex,
                                                 const std::vector<SkinDispatch>& skins)
    {
        if (skins.empty())
        {
            return {};
        }
        const std::size_t bytes =
            skins.back().skinnedVertices + std::size_t{skins.back().mesh->vertexCount} * SkinnedVertexBytes;
        // The skinned vertex buffer, grown by doubling; frames in flight keep their copies (ADR-0019).
        const BufferUsage usage = BufferUsage::Vertex | BufferUsage::Storage;
        if (m_skinnedVertices == nullptr || bytes > m_skinnedCapacity)
        {
            m_skinnedCapacity = std::max(bytes, m_skinnedCapacity * 2);
            m_skinnedVertices =
                std::make_unique<FrameRingBuffer>(m_device, BufferDesc{m_skinnedCapacity, usage}, "Skinned vertices");
        }
        const BufferHandle skinned = m_skinnedVertices->Current(frameIndex);
        const BufferHandle palette = m_paletteBlocks->Buffer(frameIndex);
        // A copy's group outlives the frame, but either buffer may be remade by growing.
        const std::size_t copies = std::max<std::size_t>(m_device.FramesInFlight(), 1);
        m_skinTargetGroups.resize(copies);
        SkinTargetGroup& target = m_skinTargetGroups[static_cast<std::size_t>(frameIndex % copies)];
        if (target.group.IsEmpty() || target.palette != palette || target.skinned != skinned)
        {
            m_device.Destroy(target.group);
            BindingGroupEntry paletteEntry;
            paletteEntry.binding = 0;
            paletteEntry.buffer = palette;
            BindingGroupEntry skinnedEntry;
            skinnedEntry.binding = 1;
            skinnedEntry.buffer = skinned;
            target = {palette, skinned,
                      m_device.CreateBindingGroup({m_skinTargetLayout, {paletteEntry, skinnedEntry}}, "Skin target")};
        }

        // The shader reads the mesh's buffer as words and has no push constants, so each dispatch's offsets go in a
        // uniform slot of its own.
        struct SkinPassJob
        {
            BindingGroupHandle job;
            BindingGroupHandle source;
            std::uint32_t groups = 0;
        };
        std::vector<SkinPassJob> dispatches;
        m_skinJobs->Reserve(skins.size());
        for (std::size_t i = 0; i < skins.size(); ++i)
        {
            const MeshEntry& mesh = *skins[i].mesh;
            const auto word = [&](std::uint32_t location) {
                return static_cast<std::uint32_t>(mesh.streams.at(location).value_or(0) / sizeof(std::uint32_t));
            };
            SkinJobBlock block;
            block.vertexCount = mesh.vertexCount;
            block.paletteOffset = skins[i].paletteOffset;
            block.skinnedFirst = static_cast<std::uint32_t>(skins[i].skinnedVertices / sizeof(std::uint32_t));
            block.flags = (mesh.streams.at(NormalLocation).has_value() ? 1u : 0u) |
                          (mesh.streams.at(TangentLocation).has_value() ? 2u : 0u);
            block.positions = word(PositionLocation);
            block.normals = word(NormalLocation);
            block.tangents = word(TangentLocation);
            block.joints = word(JointLocation);
            block.weights = word(WeightLocation);
            m_skinJobs->Write(frameIndex, i, BytesOf(block));
            dispatches.push_back({m_skinJobs->Group(frameIndex, i), mesh.skinSource,
                                  (mesh.vertexCount + SkinningGroupSize - 1) / SkinningGroupSize});
        }

        const GraphResource resource = graph.ImportBuffer("SkinnedVertices", skinned, {m_skinnedCapacity, usage});
        graph.AddPass(
            "Skinning", RenderStage::Skinning, QueueKind::Compute,
            [=](PassBuilder& pass) { pass.StorageWrite(resource); },
            [pipeline = m_skinningPipeline, group = target.group, dispatches](PassContext& context) {
                PassEncoder& encoder = context.Encoder();
                encoder.SetPipeline(pipeline);
                encoder.SetBindingGroup(2, group);
                for (const SkinPassJob& dispatch : dispatches)
                {
                    encoder.SetBindingGroup(0, dispatch.job);
                    encoder.SetBindingGroup(1, dispatch.source);
                    encoder.Dispatch({.groupsX = dispatch.groups});
                }
            });
        return resource;
    }

    std::pair<BufferHandle, GraphResource> SceneRenderer::AddLightClustersPass(
        RenderGraph& graph, std::uint64_t frameIndex, std::size_t view, const ViewRequest& request,
        std::uint32_t lightCount, ClusterDepths depths)
    {
        if (!m_clusteredLights || m_lightClustersPipeline.IsEmpty() || lightCount == 0)
        {
            return {};
        }
        // Each cluster's count, then its list, with room for every light up to MaxClusterLights. The buffers grow, a
        // copy per frame in flight, so this frame's pass never writes what an earlier frame's passes may still read.
        const std::uint32_t room = std::min(lightCount, MaxClusterLights);
        const std::size_t bytes = std::size_t{ClusterCount} * (1 + room) * sizeof(std::uint32_t);
        if (m_clusterBuffers.size() <= view)
        {
            m_clusterBuffers.resize(view + 1);
        }
        ClusterBuffers& ring = m_clusterBuffers[view];
        if (ring.copies == nullptr || bytes > ring.capacity)
        {
            ring.capacity = bytes;
            ring.copies = std::make_unique<FrameRingBuffer>(m_device, BufferDesc{ring.capacity, BufferUsage::Storage},
                                                            "Light clusters");
        }
        const BufferHandle buffer = ring.copies->Current(frameIndex);
        const std::size_t capacity = ring.capacity;
        const BufferHandle lights = m_lightBlocks->Buffer(frameIndex);
        const BindingGroupHandle targetGroup = m_clusterTargetGroups.Get(
            {BindingGroupKeyPart::Of(lights), BindingGroupKeyPart::Of(buffer)}, frameIndex, [&] {
                BindingGroupEntry lightEntry;
                lightEntry.binding = 0;
                lightEntry.buffer = lights;
                BindingGroupEntry clusterEntry;
                clusterEntry.binding = 1;
                clusterEntry.buffer = buffer;
                return m_device.CreateBindingGroup({m_clusterTargetLayout, {lightEntry, clusterEntry}},
                                                   "Cluster target");
            });
        ClusterJobBlock job;
        job.view = request.view;
        job.inverseProjection = glm::inverse(request.projection);
        job.depths = Math::Vec4(depths.near, depths.far, 0.0f, 0.0f);
        job.counts = {lightCount, room, 0, 0};
        m_clusterJobs->Write(frameIndex, view, BytesOf(job));

        const GraphResource resource = graph.ImportBuffer("LightClusters", buffer, {capacity, BufferUsage::Storage});
        graph.AddPass(
            "LightClusters", RenderStage::DepthPrepass, QueueKind::Compute,
            [=](PassBuilder& pass) { pass.StorageWrite(resource); },
            [pipeline = m_lightClustersPipeline, jobGroup = m_clusterJobs->Group(frameIndex, view),
             targetGroup](PassContext& context) {
                PassEncoder& encoder = context.Encoder();
                encoder.SetPipeline(pipeline);
                encoder.SetBindingGroup(0, jobGroup);
                encoder.SetBindingGroup(1, targetGroup);
                encoder.Dispatch({.groupsX = (ClusterCount + LightClustersGroupSize - 1) / LightClustersGroupSize});
            });
        return {buffer, resource};
    }

    BindingGroupHandle SceneRenderer::DrawGroupFor(std::uint64_t frameIndex, BufferHandle indirection)
    {
        const BufferHandle draws = m_drawBlocks->Buffer(frameIndex);
        const BufferHandle palette = m_paletteBlocks->Buffer(frameIndex);
        const BufferHandle bound = indirection.IsEmpty() ? m_noIndirection : indirection;
        // One group per frame in flight's buffers and indirection (a view's, or none), kept while they all live: a
        // ring remaking its buffers, the instance lists packed again or a view's indirection grown makes a new one.
        return m_drawGroups.Get({BindingGroupKeyPart::Of(draws), BindingGroupKeyPart::Of(palette),
                                 BindingGroupKeyPart::Of(m_instanceListBuffer), BindingGroupKeyPart::Of(bound)},
                                frameIndex, [&] {
                                    BindingGroupEntry drawEntry;
                                    drawEntry.binding = 0;
                                    drawEntry.buffer = draws;
                                    BindingGroupEntry instanceEntry;
                                    instanceEntry.binding = 1;
                                    instanceEntry.buffer = m_instanceListBuffer;
                                    BindingGroupEntry paletteEntry;
                                    paletteEntry.binding = 2;
                                    paletteEntry.buffer = palette;
                                    BindingGroupEntry indirectionEntry;
                                    indirectionEntry.binding = 3;
                                    indirectionEntry.buffer = bound;
                                    return m_device.CreateBindingGroup(
                                        {m_drawLayout, {drawEntry, instanceEntry, paletteEntry, indirectionEntry}},
                                        "Draw");
                                });
    }

    SceneRenderer::CullView SceneRenderer::PrepareCull(RenderGraph& graph, std::uint64_t frameIndex, std::size_t view,
                                                       const Math::Frustum& frustum,
                                                       const std::vector<ResolvedDraw>& draws,
                                                       const std::vector<DrawRun>& runs,
                                                       std::vector<DrawIndexedIndirectCommand>& templates,
                                                       std::vector<CullGroupEntry>& groups)
    {
        CullView cull;
        cull.frustum = frustum;
        if (runs.empty() || m_cullPipeline.IsEmpty())
        {
            return cull;
        }
        // Each run's template, with no instances until the pass counts them, and the base instance where its region
        // of the indirection starts; and its groups, each culling up to 64 of its candidates, so none straddles runs.
        cull.templateOffset = templates.size() * sizeof(DrawIndexedIndirectCommand);
        cull.firstGroup = static_cast<std::uint32_t>(groups.size());
        std::uint32_t candidates = 0;
        for (std::size_t r = 0; r < runs.size(); ++r)
        {
            const DrawRun& run = runs[r];
            const ResolvedDraw& draw = draws[run.first];
            const std::uint32_t count = draw.instances.count == 0 ? run.count : draw.instances.count;
            templates.push_back({.indexCount = draw.mesh->indexCount, .baseInstance = candidates});
            for (std::uint32_t first = 0; first < count; first += CullGroupSize)
            {
                groups.push_back({.command = static_cast<std::uint32_t>(r),
                                  .record = static_cast<std::uint32_t>(run.first),
                                  .count = count,
                                  .first = first});
            }
            candidates += count;
        }
        cull.runs = static_cast<std::uint32_t>(runs.size());
        cull.candidates = candidates;
        cull.templateBytes = runs.size() * sizeof(DrawIndexedIndirectCommand);
        cull.groups = static_cast<std::uint32_t>(groups.size()) - cull.firstGroup;

        // The view's command buffer and indirection, sized to this frame's runs and candidates, kept between frames and
        // grown as the room needs, a copy per frame in flight, so this frame's pass never writes what an earlier
        // frame's draws may still read.
        if (m_cullRings.size() <= view)
        {
            m_cullRings.resize(view + 1);
        }
        CullRings& rings = m_cullRings[view];
        constexpr BufferUsage commandUsage =
            BufferUsage::Indirect | BufferUsage::Storage | BufferUsage::CopyDestination;
        rings.commands.Reserve(m_device, cull.templateBytes, commandUsage, "Cull commands");
        rings.indirection.Reserve(m_device, std::size_t{candidates} * sizeof(IndirectionEntry), BufferUsage::Storage,
                                  "Cull indirection");
        cull.commands = rings.commands.ring->Current(frameIndex);
        cull.indirection = rings.indirection.ring->Current(frameIndex);
        cull.commandsResource =
            graph.ImportBuffer("CullCommands", cull.commands, {rings.commands.capacity, commandUsage});
        cull.indirectionResource =
            graph.ImportBuffer("CullIndirection", cull.indirection, {rings.indirection.capacity, BufferUsage::Storage});
        return cull;
    }

    void SceneRenderer::AddCullPasses(RenderGraph& graph, std::uint64_t frameIndex, const std::vector<CullView>& views,
                                      std::span<const DrawIndexedIndirectCommand> templates,
                                      std::span<const CullGroupEntry> groups)
    {
        if (templates.empty())
        {
            return;
        }
        // The frame's templates and group table, grown by doubling, a copy per frame in flight.
        m_cullTemplates.Reserve(m_device, templates.size_bytes(), BufferUsage::CopySource, "Cull templates");
        m_cullGroups.Reserve(m_device, groups.size_bytes(), BufferUsage::Storage, "Cull groups");
        const BufferHandle templateBuffer = m_cullTemplates.ring->Current(frameIndex);
        const BufferHandle groupBuffer = m_cullGroups.ring->Current(frameIndex);
        m_cullTemplates.ring->Write(frameIndex, 0, std::as_bytes(templates));
        m_cullGroups.ring->Write(frameIndex, 0, std::as_bytes(groups));
        const GraphResource templateResource =
            graph.ImportBuffer("CullTemplates", templateBuffer, {m_cullTemplates.capacity, BufferUsage::CopySource});
        const GraphResource groupResource =
            graph.ImportBuffer("CullGroups", groupBuffer, {m_cullGroups.capacity, BufferUsage::Storage});
        const BufferHandle records = m_drawBlocks->Buffer(frameIndex);
        const GraphResource recordResource = graph.ImportBuffer(
            "DrawRecords", records, {m_drawBlocks->Capacity() * m_drawBlocks->SlotSize(), BufferUsage::Storage});
        const GraphResource copiesResource =
            graph.ImportBuffer("InstanceLists", m_instanceListBuffer, {m_instanceListBytes, BufferUsage::Storage});
        m_cullJobs->Reserve(views.size());

        for (std::size_t v = 0; v < views.size(); ++v)
        {
            const CullView& cull = views[v];
            if (cull.commands.IsEmpty())
            {
                continue;
            }
            // The view's planes go with the job, or none when GPU culling is off, so the pass keeps every candidate
            // and the draws keep their shape for comparison.
            CullJobBlock job{{cull.firstGroup, cull.groups, 0, 0}};
            if (m_gpuCulling)
            {
                const std::span<const Math::Vec4> planes = cull.frustum.Planes();
                job.groups[2] = static_cast<std::uint32_t>(planes.size());
                std::ranges::copy(planes, job.planes.begin());
            }
            m_cullJobs->Write(frameIndex, v, BytesOf(job));
            const BindingGroupHandle targetGroup = m_cullTargetGroups.Get(
                {BindingGroupKeyPart::Of(records), BindingGroupKeyPart::Of(groupBuffer),
                 BindingGroupKeyPart::Of(cull.commands), BindingGroupKeyPart::Of(cull.indirection),
                 BindingGroupKeyPart::Of(m_instanceListBuffer)},
                frameIndex, [&] {
                    std::array<BindingGroupEntry, 5> entries;
                    for (std::uint32_t binding = 0; binding < entries.size(); ++binding)
                    {
                        entries.at(binding).binding = binding;
                    }
                    entries[0].buffer = records;
                    entries[1].buffer = groupBuffer;
                    entries[2].buffer = cull.commands;
                    entries[3].buffer = cull.indirection;
                    entries[4].buffer = m_instanceListBuffer;
                    return m_device.CreateBindingGroup({m_cullTargetLayout, {entries.begin(), entries.end()}},
                                                       "Cull target");
                });
            // The templates over the view's commands, then one dispatch over its groups; the view's DepthPrepass and
            // Opaque passes declare reading both results, which the backend's barriers follow (ADR-0057).
            graph.AddPass(
                "Cull", RenderStage::Cull, QueueKind::Compute,
                [=](PassBuilder& pass) {
                    pass.CopySource(templateResource);
                    pass.CopyDestination(cull.commandsResource);
                    pass.StorageRead(groupResource);
                    pass.StorageRead(recordResource);
                    pass.StorageRead(copiesResource);
                    pass.StorageWrite(cull.commandsResource);
                    pass.StorageWrite(cull.indirectionResource);
                },
                [pipeline = m_cullPipeline, jobGroup = m_cullJobs->Group(frameIndex, v), targetGroup, templateBuffer,
                 cull](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.CopyBuffer({.source = templateBuffer,
                                        .sourceOffset = cull.templateOffset,
                                        .destination = cull.commands,
                                        .size = cull.templateBytes});
                    encoder.SetPipeline(pipeline);
                    encoder.SetBindingGroup(0, jobGroup);
                    encoder.SetBindingGroup(1, targetGroup);
                    encoder.Dispatch({.groupsX = cull.groups});
                });
        }
    }

    void SceneRenderer::ReadCulled(std::uint64_t frameIndex)
    {
        const std::size_t slots = std::max<std::size_t>(m_device.FramesInFlight(), 1);
        if (m_cullReadbacks.size() != slots || frameIndex < slots)
        {
            return;
        }
        // The slot this frame will fill again holds the frame FramesInFlight ago, the most recent to have left
        // flight; one with no runs counts 0, and a slot that frame did not fill leaves the last count standing.
        const CullReadback& readback = m_cullReadbacks[static_cast<std::size_t>(frameIndex % slots)];
        if (readback.frame != frameIndex - slots)
        {
            return;
        }
        std::vector<DrawIndexedIndirectCommand> commands(readback.commands);
        if (!commands.empty() && !m_device.ReadBuffer(readback.buffer, 0, std::as_writable_bytes(std::span(commands))))
        {
            return;
        }
        std::uint32_t drawn = 0;
        for (const DrawIndexedIndirectCommand& command : commands)
        {
            drawn += command.instanceCount;
        }
        m_stats.instancesDrawn = drawn;
    }

    void SceneRenderer::AddCullReadback(RenderGraph& graph, std::uint64_t frameIndex,
                                        const std::vector<CullView>& views, std::size_t commandBytes)
    {
        const std::size_t slots = std::max<std::size_t>(m_device.FramesInFlight(), 1);
        m_cullReadbacks.resize(slots);
        CullReadback& readback = m_cullReadbacks[static_cast<std::size_t>(frameIndex % slots)];
        readback.frame = frameIndex;
        readback.commands = static_cast<std::uint32_t>(commandBytes / sizeof(DrawIndexedIndirectCommand));
        if (commandBytes == 0)
        {
            return;
        }
        // The slot was read at the frame's start, so it can be made again now without losing a frame's count.
        constexpr BufferUsage usage = BufferUsage::CopyDestination | BufferUsage::Readback;
        if (readback.buffer.IsEmpty() || readback.capacity < commandBytes)
        {
            m_device.Destroy(readback.buffer);
            readback.capacity = std::max(commandBytes, readback.capacity * 2);
            readback.buffer = m_device.CreateBuffer({readback.capacity, usage}, {}, "Cull readback");
        }
        const GraphResource resource = graph.ImportBuffer("CullReadback", readback.buffer, {readback.capacity, usage});
        // Each view's commands at its templates' offset, so the slot reads as the frame's template ring did, with
        // the counts the passes left: last in the frame, after every pass that reads the commands.
        std::vector<CullView> copied;
        for (const CullView& cull : views)
        {
            if (!cull.commands.IsEmpty())
            {
                copied.push_back(cull);
            }
        }
        graph.AddPass(
            "CullReadback", RenderStage::Overlay, QueueKind::Compute,
            [=](PassBuilder& pass) {
                for (const CullView& cull : copied)
                {
                    pass.CopySource(cull.commandsResource);
                }
                pass.CopyDestination(resource);
            },
            [copied, destination = readback.buffer](PassContext& context) {
                for (const CullView& cull : copied)
                {
                    context.Encoder().CopyBuffer({.source = cull.commands,
                                                  .destination = destination,
                                                  .destinationOffset = cull.templateOffset,
                                                  .size = cull.templateBytes});
                }
            });
    }

    const Math::Frustum& SceneRenderer::CullFrustumOf(const ViewRequest& request, const Math::Frustum& frustum)
    {
        if (!m_cullingFrozen)
        {
            return frustum;
        }
        for (const FrozenCull& frozen : m_frozenCulls)
        {
            if (frozen.camera == request.camera)
            {
                return frozen.frustum;
            }
        }
        // The camera's first frozen frame keeps what it sees now, its corners unprojected from clip space: the near
        // ones at depth 1, the far ones at depth 0, or, for a perspective view whose far plane is at infinity (w of 0
        // there), a fixed way along each near corner's ray from the camera.
        FrozenCull frozen{request.camera, frustum, request.position};
        const Math::Mat4 inverse = glm::inverse(request.projection * request.view);
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            const float x = (corner & 1) != 0 ? 1.0f : -1.0f;
            const float y = (corner & 2) != 0 ? 1.0f : -1.0f;
            const Math::Vec4 near = inverse * Math::Vec4(x, y, 1.0f, 1.0f);
            const Math::Vec4 far = inverse * Math::Vec4(x, y, 0.0f, 1.0f);
            frozen.corners.at(corner) = Math::Vec3(near) / near.w;
            frozen.corners.at(corner + 4) =
                std::abs(far.w) > 1e-6f
                    ? Math::Vec3(far) / far.w
                    : request.position +
                          glm::normalize(frozen.corners.at(corner) - request.position) * FrozenFarDistance;
        }
        m_frozenCulls.push_back(frozen);
        return m_frozenCulls.back().frustum;
    }

    const SceneRenderer::FrameGroup& SceneRenderer::FrameGroupFor(std::uint64_t frameIndex)
    {
        const BufferHandle block = m_frameBlocks->Buffer(frameIndex);
        const BufferHandle lights = m_lightBlocks->Buffer(frameIndex);
        const BufferHandle spotShadows = m_spotShadows->Records(frameIndex);
        const BufferHandle pointShadows = m_pointShadows->Records(frameIndex);
        const TextureHandle spotAtlas = m_spotShadows->Map();
        const TextureHandle pointShadowCubes = m_pointShadows->Map();
        const SkyLightingTextures& sky = m_skyLighting->Textures();
        // One pair of groups per copy, made again when a ring remade its buffer, the sky's textures changed, or the
        // spot shadow atlas or point shadow cubes came, went or was made again.
        const std::size_t copies = std::max<std::size_t>(m_device.FramesInFlight(), 1);
        m_frameGroups.resize(copies);
        FrameGroup& group = m_frameGroups[static_cast<std::size_t>(frameIndex % copies)];
        if (group.group.IsEmpty() || group.block != block || group.lights != lights ||
            group.irradiance != sky.irradiance || group.specular != sky.specular || group.spotShadows != spotShadows ||
            group.spotAtlas != spotAtlas || group.pointShadows != pointShadows ||
            group.pointShadowCubes != pointShadowCubes)
        {
            if (group.unshadowed != group.group)
            {
                m_device.Destroy(group.unshadowed);
            }
            m_device.Destroy(group.group);
            BindingGroupEntry blockEntry;
            blockEntry.binding = 0;
            blockEntry.buffer = block;
            blockEntry.size = sizeof(FrameBlock);
            BindingGroupEntry irradiance;
            irradiance.binding = 1;
            irradiance.texture = sky.irradiance;
            irradiance.sampler = sky.sampler;
            BindingGroupEntry specular;
            specular.binding = 2;
            specular.texture = sky.specular;
            specular.sampler = sky.sampler;
            BindingGroupEntry brdf;
            brdf.binding = 3;
            brdf.texture = sky.brdfLut;
            brdf.sampler = sky.sampler;
            BindingGroupEntry lightEntry;
            lightEntry.binding = 4;
            lightEntry.buffer = lights;
            BindingGroupEntry ltcMatrix;
            ltcMatrix.binding = 5;
            ltcMatrix.texture = m_ltcMatrix;
            ltcMatrix.sampler = m_skySampler;
            BindingGroupEntry ltcAmplitude;
            ltcAmplitude.binding = 6;
            ltcAmplitude.texture = m_ltcAmplitude;
            ltcAmplitude.sampler = m_skySampler;
            BindingGroupEntry spotEntry;
            spotEntry.binding = 7;
            spotEntry.buffer = spotShadows;
            BindingGroupEntry atlasEntry;
            atlasEntry.binding = 8;
            atlasEntry.texture = m_spotShadows->Bound();
            atlasEntry.sampler = m_shadowSampler;
            BindingGroupEntry pointEntry;
            pointEntry.binding = 9;
            pointEntry.buffer = pointShadows;
            BindingGroupEntry cubesEntry;
            cubesEntry.binding = 10;
            cubesEntry.texture = m_pointShadows->Bound();
            cubesEntry.sampler = m_shadowSampler;
            std::vector<BindingGroupEntry> entries{blockEntry, irradiance, specular,     brdf,
                                                   lightEntry, ltcMatrix,  ltcAmplitude, spotEntry,
                                                   atlasEntry, pointEntry, cubesEntry};
            group = {block,       lights,    sky.irradiance, sky.specular,
                     spotShadows, spotAtlas, pointShadows,   pointShadowCubes};
            group.group = m_device.CreateBindingGroup({m_frameLayout, entries}, "Frame");
            group.unshadowed = group.group;
            if (!spotAtlas.IsEmpty() || !pointShadowCubes.IsEmpty())
            {
                entries.at(8).texture = m_spotShadows->Fallback();
                entries.at(10).texture = m_pointShadows->Fallback();
                group.unshadowed = m_device.CreateBindingGroup({m_frameLayout, entries}, "Frame unshadowed");
            }
        }
        return group;
    }

    SkySource SceneRenderer::SkySourceOf(const SceneLighting& lighting) const
    {
        SkySource source;
        source.colour = lighting.skyColour;
        const SkyCube cube = m_skyImages->Cube(lighting.sky);
        source.cube = cube.texture;
        source.cubeDesc = cube.desc;
        return source;
    }

    BindingGroupHandle SceneRenderer::ViewGroupFor(std::uint64_t frameIndex, std::size_t slot, TextureHandle occlusion,
                                                   TextureHandle shadow, BufferHandle clusters,
                                                   TextureHandle reflectionHits, TextureHandle sceneHistory)
    {
        if (reflectionHits.IsEmpty())
        {
            reflectionHits = GetDefaultTexture(DefaultTexture::Black);
        }
        if (sceneHistory.IsEmpty())
        {
            sceneHistory = GetDefaultTexture(DefaultTexture::Black);
        }
        if (shadow.IsEmpty())
        {
            shadow = m_noShadow;
        }
        if (clusters.IsEmpty())
        {
            clusters = m_noClusters;
        }
        const BufferHandle buffer = m_viewBlocks->Buffer(frameIndex);
        const BindingGroupCache<7>::Key key{
            BindingGroupKeyPart::Of(buffer),      BindingGroupKeyPart::Value(static_cast<std::uint32_t>(slot)),
            BindingGroupKeyPart::Of(occlusion),   BindingGroupKeyPart::Of(shadow),
            BindingGroupKeyPart::Of(clusters),    BindingGroupKeyPart::Of(reflectionHits),
            BindingGroupKeyPart::Of(sceneHistory)};
        return m_viewGroups.Get(key, frameIndex, [&] {
            BindingGroupEntry block;
            block.binding = 0;
            block.buffer = buffer;
            block.offset = slot * m_viewBlocks->SlotSize();
            block.size = m_viewBlocks->SlotSize();
            BindingGroupEntry texture;
            texture.binding = 1;
            texture.texture = occlusion;
            texture.sampler = m_outputSampler;
            BindingGroupEntry shadowAtlas;
            shadowAtlas.binding = 2;
            shadowAtlas.texture = shadow;
            shadowAtlas.sampler = m_shadowSampler;
            BindingGroupEntry clusterLights;
            clusterLights.binding = 3;
            clusterLights.buffer = clusters;
            // The hits are read by texelFetch; the history trilinearly across its mips, for the cone's blur.
            BindingGroupEntry hits;
            hits.binding = 4;
            hits.texture = reflectionHits;
            hits.sampler = m_outputSampler;
            BindingGroupEntry history;
            history.binding = 5;
            history.texture = sceneHistory;
            history.sampler = m_outputSampler;
            return m_device.CreateBindingGroup(
                {m_viewLayout, {block, texture, shadowAtlas, clusterLights, hits, history}}, "View");
        });
    }

    BindingGroupHandle SceneRenderer::OutputGroup(TextureHandle sceneColour, std::uint64_t frameIndex)
    {
        return m_outputGroups.Get({BindingGroupKeyPart::Of(sceneColour)}, frameIndex, [&] {
            BindingGroupEntry entry;
            entry.binding = 0;
            entry.texture = sceneColour;
            entry.sampler = m_outputSampler;
            return m_device.CreateBindingGroup({m_outputLayout, {entry}}, "Output");
        });
    }

    BindingGroupHandle SceneRenderer::ReflectionOutputGroup(TextureHandle hits, TextureHandle history,
                                                            std::uint64_t frameIndex)
    {
        return m_reflectionOutputGroups.Get(
            {BindingGroupKeyPart::Of(hits), BindingGroupKeyPart::Of(history)}, frameIndex, [&] {
                BindingGroupEntry hitsEntry;
                hitsEntry.binding = 0;
                hitsEntry.texture = hits;
                hitsEntry.sampler = m_outputSampler;
                BindingGroupEntry historyEntry;
                historyEntry.binding = 1;
                historyEntry.texture = history;
                historyEntry.sampler = m_outputSampler;
                return m_device.CreateBindingGroup({m_reflectionOutputLayout, {hitsEntry, historyEntry}},
                                                   "Reflection output");
            });
    }

    void SceneRenderer::AddWindowClear(RenderGraph& graph, const FrameContext& frame)
    {
        // A window no view draws into is cleared to black.
        const GraphResource backbuffer = frame.backbuffer;
        graph.AddPass(
            "Clear", RenderStage::Output, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(backbuffer, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}); },
            [](PassContext&) {});
    }

    void SceneRenderer::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        m_stats.draws = 0;
        m_stats.instances = 0;
        m_stats.paletteBytes = 0;
        m_stats.palettes = 0;
        m_stats.skinnedDraws = 0;
        m_stats.shadowDraws = 0;
        m_drawnViews.clear();
        ReadCulled(frame.frameIndex);
        PackInstanceLists();
        m_skyImages->AddPasses(graph);
        m_skyLighting->AddPasses(graph, SkySourceOf(m_frame.lighting));
        if (m_frame.views.empty())
        {
            // No camera keeps its reflection history.
            m_sceneHistory->BeginFrame({});
            m_sceneHistory->EndFrame();
            AddWindowClear(graph, frame);
            return;
        }

        const SceneLighting& lighting = m_frame.lighting;
        // The spot lights that keep their shadows this frame (ADR-0050): the nearest to any camera, within the budget.
        std::vector<Math::Vec3> cameras;
        for (const ViewRequest& view : m_frame.views)
        {
            cameras.push_back(view.position);
        }
        ShadowChoice spotChoice = m_spotShadows->Choose(lighting.lights, cameras);
        // And the point lights, within theirs.
        ShadowChoice pointChoice = m_pointShadows->Choose(lighting.lights, cameras);

        std::vector<SkinDispatch> skins;
        const auto draws = std::make_shared<const std::vector<ResolvedDraw>>(
            ResolveDraws(skins, lighting.shadows.enabled || !spotChoice.lights.empty(), !pointChoice.lights.empty()));
        const std::uint64_t frameIndex = frame.frameIndex;
        // The items that cast the sun's and the spot lights' shadows, and those that cast the point lights', in
        // draw-list order.
        std::vector<std::size_t> casters;
        std::vector<std::size_t> pointCasters;
        for (std::size_t i = 0; i < draws->size(); ++i)
        {
            if ((*draws)[i].shadow != nullptr)
            {
                casters.push_back(i);
            }
            if ((*draws)[i].pointShadow != nullptr)
            {
                pointCasters.push_back(i);
            }
        }
        // Each kind's shadowed lights (none when nothing casts their shadows), their shadows fitted and records
        // written, and the maps they are drawn into.
        const auto spotFrame =
            m_spotShadows->Prepare(std::move(spotChoice), frameIndex, graph, lighting.lights, !casters.empty());
        const auto pointFrame =
            m_pointShadows->Prepare(std::move(pointChoice), frameIndex, graph, lighting.lights, !pointCasters.empty());
        const auto spotCount = static_cast<std::uint32_t>(spotFrame.lights.size());
        const auto pointCount = static_cast<std::uint32_t>(pointFrame.lights.size());
        const FrameBlock frameBlock{Math::Vec4(lighting.lightDirection, 0.0f),
                                    Math::Vec4(lighting.lightColour, 0.0f),
                                    Math::Vec4(m_frame.time, m_frame.deltaTime, 0.0f, 0.0f),
                                    Math::Vec4(m_frame.backgroundIntensity, std::cos(m_frame.backgroundRotation),
                                               std::sin(m_frame.backgroundRotation), 0.0f),
                                    Math::Vec4(lighting.skyIntensity, std::cos(lighting.skyRotation),
                                               std::sin(lighting.skyRotation),
                                               m_skyLighting->Textures().specularMaxLevel),
                                    {static_cast<std::uint32_t>(lighting.lights.size() + lighting.rectLights.size()),
                                     spotCount, pointCount, 0}};
        m_frameBlocks->Write(frameIndex, 0, BytesOf(frameBlock));
        const std::vector<GpuLight> lightRecords =
            LightRecords(lighting.lights, lighting.rectLights, spotFrame.lights, pointFrame.lights);
        m_lightBlocks->Reserve(lightRecords.size());
        for (std::size_t i = 0; i < lightRecords.size(); ++i)
        {
            m_lightBlocks->Write(frameIndex, i, BytesOf(lightRecords[i]));
        }
        const auto lightCount = static_cast<std::uint32_t>(lightRecords.size());
        m_drawBlocks->Reserve(draws->size());
        for (std::size_t i = 0; i < draws->size(); ++i)
        {
            const ResolvedDraw& draw = (*draws)[i];
            DrawBlock block;
            block.world = draw.world;
            block.normal = glm::transpose(glm::inverse(draw.world));
            block.paletteOffset = draw.paletteOffset;
            block.worldSign = WorldSign(draw.world);
            block.instanceFirst = draw.instances.first;
            block.instanceCount = draw.instances.count;
            block.boundsMin = Math::Vec4(draw.recordBounds.min, 0.0f);
            block.boundsMax = Math::Vec4(draw.recordBounds.max, 0.0f);
            m_drawBlocks->Write(frameIndex, i, BytesOf(block));
        }
        // Every palette written again each frame (ADR-0035): nothing is kept across frames.
        m_paletteBlocks->Reserve(m_frame.palette.size());
        for (std::size_t bone = 0; bone < m_frame.palette.size(); ++bone)
        {
            m_paletteBlocks->Write(frameIndex, bone, BytesOf(GpuBone::FromMatrix(m_frame.palette[bone])));
        }
        // Every skinned mesh skinned once, for every pass of every view (ADR-0050), which declare reading it.
        const GraphResource skinnedResource = AddSkinningPass(graph, frameIndex, skins);
        const BufferHandle skinned = skins.empty() ? BufferHandle{} : m_skinnedVertices->Current(frameIndex);
        const auto readSkinned = [skinnedResource](PassBuilder& pass) {
            if (!skinnedResource.IsEmpty())
            {
                pass.VertexInput(skinnedResource);
            }
        };
        m_stats.paletteBytes = static_cast<std::uint32_t>(m_frame.palette.size() * sizeof(GpuBone));
        m_stats.palettes = m_frame.palettes;
        const auto skinnedDraws = static_cast<std::uint32_t>(
            std::count_if(draws->begin(), draws->end(), [](const ResolvedDraw& draw) { return draw.skinned; }));
        const std::vector<ViewRequest>& views = m_frame.views;
        const ViewBlockSlots slots{views.size(), spotCount, pointCount};
        m_viewBlocks->Reserve(slots.Count());

        // The spot shadow atlas's grid of tiles, and the point shadow cubes' faces.
        const SpotAtlasGrid grid = SpotAtlasGridOf(m_spotShadows->Config().budget);
        const std::uint32_t tileSize = m_spotShadows->Config().mapSize;
        const std::uint32_t faceSize = m_pointShadows->Config().mapSize;
        const GraphResource spotAtlas = spotFrame.map;
        const GraphResource pointShadowCubes = pointFrame.map;
        m_gtao->BeginFrame(views.size());
        m_sceneHistory->BeginFrame(views);
        m_reflectionTrace->BeginFrame(views.size());
        m_clusterJobs->Reserve(views.size());
        m_backgroundBlocks->Reserve(views.size());
        m_outputBlocks->Reserve(views.size());

        // The render target images this frame's draws sample, each imported into the graph once.
        std::set<AssetKey> sampled;
        for (const ResolvedDraw& draw : *draws)
        {
            if (draw.material != nullptr)
            {
                for (const SampledImage& image : draw.material->images)
                {
                    if (!image.bound.IsEmpty() && m_targets.contains(image.image))
                    {
                        sampled.insert(image.image);
                    }
                }
            }
        }
        std::map<AssetKey, GraphResource> images;
        const auto imported = [&](AssetKey key) {
            if (const auto found = images.find(key); found != images.end())
            {
                return found->second;
            }
            const TargetEntry& target = m_targets.at(key);
            TextureDesc desc;
            desc.format = TextureFormat::Rgba8Srgb;
            desc.width = target.size.width;
            desc.height = target.size.height;
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            const GraphResource resource =
                graph.ImportTexture(std::format("Image {}", key.index), target.texture, desc);
            images.emplace(key, resource);
            return resource;
        };

        // Which sampled images each view may read (ADR-0026). Each it may not is warned of once per target, and its
        // materials bind the default texture for it.
        std::vector<AssetKey> targets;
        for (const ViewRequest& view : views)
        {
            targets.push_back(view.target);
        }
        const std::vector<ImageReads> imageReads = PlanImageReads(targets, sampled);
        std::vector<std::vector<AssetKey>> replaced(views.size());
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            for (const auto& [image, reason] : imageReads[v].replaced)
            {
                replaced[v].push_back(image);
                if (m_loggedSampling.insert({views[v].target, image}).second)
                {
                    m_logger.Warn(reason == ReplacedBecause::OwnTarget
                                      ? "FenrirRenderer: a view into {} draws materials that sample its own target "
                                        "image, so they sample the default texture there"
                                      : "FenrirRenderer: a view into {} draws materials that sample image {}, whose "
                                        "views wait on it in a cycle, so they sample the default texture there",
                                  TargetName(views[v].target), image.index);
                }
            }
        }

        const FrameGroup& frameGroups = FrameGroupFor(frameIndex);
        const BindingGroupHandle frameGroup = frameGroups.group;
        const BindingGroupHandle unshadowedFrame = frameGroups.unshadowed;
        // The draw set of every direct draw (the shadow and transparent passes), with no indirection (ADR-0057).
        const BindingGroupHandle drawGroup = DrawGroupFor(frameIndex);
        // Each view's cull pass (ADR-0057): its templates and groups are gathered here and uploaded after the views.
        std::vector<CullView> culls;
        std::vector<DrawIndexedIndirectCommand> cullTemplates;
        std::vector<CullGroupEntry> cullGroups;

        if (spotCount > 0)
        {
            // Each casting item's set 2, as in ShadowCascades, but with every render target image a material samples
            // replaced by its default, so the pass waits on no view.
            const std::vector<AssetKey> allSampled(sampled.begin(), sampled.end());
            auto spotGroups = std::make_shared<std::vector<BindingGroupHandle>>(draws->size());
            for (const std::size_t i : casters)
            {
                const ResolvedDraw& draw = (*draws)[i];
                if (draw.material != nullptr)
                {
                    (*spotGroups)[i] = draw.shadowMasked ? PrepassGroup(*draw.material, allSampled)
                                                         : GroupWithout(*draw.material, allSampled);
                }
            }
            auto spotRuns =
                std::make_shared<std::vector<DrawRun>>(RunsOf(*draws, *spotGroups, casters, DrawPass::Shadow));
            // Each tile's own view block, the light's perspective, which the casting items' vertex stages place them
            // by.
            std::vector<BindingGroupHandle> tileGroups;
            std::vector<PixelRect> tiles;
            for (std::uint32_t t = 0; t < spotCount; ++t)
            {
                const SpotShadow& shadow = spotFrame.shadows[t];
                const SpotAtlasTile at = SpotAtlasTileOf(t, grid);
                const PixelRect tile{static_cast<std::int32_t>(at.column * tileSize),
                                     static_cast<std::int32_t>(at.row * tileSize), tileSize, tileSize};
                const ViewBlock tileBlock{shadow.view,
                                          shadow.projection,
                                          shadow.viewProjection,
                                          glm::inverse(shadow.viewProjection),
                                          Math::Vec4(lighting.lights[spotFrame.lights[t]].position, 1.0f),
                                          Math::Vec4(static_cast<float>(tile.x), static_cast<float>(tile.y),
                                                     static_cast<float>(tileSize), static_cast<float>(tileSize))};
                m_viewBlocks->Write(frameIndex, slots.Tile(t), BytesOf(tileBlock));
                tileGroups.push_back(ViewGroupFor(frameIndex, slots.Tile(t), GetDefaultTexture(DefaultTexture::White)));
                tiles.push_back(tile);
            }
            // Every shadowed spot light into its tile of the atlas, in one pass, binding the frame group that does not
            // read the atlas it draws.
            graph.AddPass(
                "SpotShadows", RenderStage::Shadow, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.DepthAttachment(spotAtlas, LoadOp::Clear, 0.0f);
                    readSkinned(pass);
                },
                [this, draws, spotGroups, spotRuns, unshadowedFrame, tileGroups, tiles, drawGroup,
                 skinned](PassContext& context) {
                    for (std::size_t t = 0; t < tiles.size(); ++t)
                    {
                        RecordDraws(context.Encoder(), *draws, *spotGroups, *spotRuns, unshadowedFrame, tileGroups[t],
                                    drawGroup, skinned, tiles[t], DrawPass::Shadow);
                    }
                });
            m_stats.shadowDraws += static_cast<std::uint32_t>(spotRuns->size() * tiles.size());
        }

        if (pointCount > 0)
        {
            // Each casting item's set 2, as in SpotShadows. Those whose shader has a POINT_SHADOW variant the device
            // can draw are drawn into all six faces at once, the rest a face at a time.
            const std::vector<AssetKey> allSampled(sampled.begin(), sampled.end());
            auto pointGroups = std::make_shared<std::vector<BindingGroupHandle>>(draws->size());
            std::vector<std::size_t> layered;
            std::vector<std::size_t> faced;
            for (const std::size_t i : pointCasters)
            {
                const ResolvedDraw& draw = (*draws)[i];
                if (draw.material != nullptr)
                {
                    (*pointGroups)[i] = draw.shadowMasked ? PrepassGroup(*draw.material, allSampled)
                                                          : GroupWithout(*draw.material, allSampled);
                }
                (draw.pointShadowLayered != nullptr ? layered : faced).push_back(i);
            }
            auto layeredRuns = std::make_shared<std::vector<DrawRun>>(
                RunsOf(*draws, *pointGroups, layered, DrawPass::PointShadowLayered));
            auto facedRuns =
                std::make_shared<std::vector<DrawRun>>(RunsOf(*draws, *pointGroups, faced, DrawPass::PointShadow));
            // Each light's view blocks: one from the light, not turned, naming its cube for the instanced faces, and
            // one per face. The camera is the light, its range in w, which builtin:shadow's POINT_SHADOW variant reads.
            const PixelRect face{0, 0, faceSize, faceSize};
            const Math::Vec4 viewport(0.0f, 0.0f, static_cast<float>(faceSize), static_cast<float>(faceSize));
            const TextureHandle white = GetDefaultTexture(DefaultTexture::White);
            std::vector<BindingGroupHandle> cubeGroups;
            std::vector<BindingGroupHandle> faceGroups;
            for (std::uint32_t c = 0; c < pointCount; ++c)
            {
                const PointShadow& shadow = pointFrame.shadows[c];
                const PunctualLight& light = lighting.lights[pointFrame.lights[c]];
                const auto blockFor = [&](const Math::Mat4& view) {
                    const Math::Mat4 viewProjection = shadow.projection * view;
                    ViewBlock block{view,
                                    shadow.projection,
                                    viewProjection,
                                    glm::inverse(viewProjection),
                                    Math::Vec4(light.position, light.range),
                                    viewport};
                    block.shadowInfo = {0, 0, c, 0};
                    return block;
                };
                m_viewBlocks->Write(frameIndex, slots.Cube(c), BytesOf(blockFor(shadow.view)));
                cubeGroups.push_back(ViewGroupFor(frameIndex, slots.Cube(c), white));
                for (std::uint32_t f = 0; f < CubeFaceCount; ++f)
                {
                    m_viewBlocks->Write(frameIndex, slots.Face(c, f), BytesOf(blockFor(shadow.faceViews.at(f))));
                    faceGroups.push_back(ViewGroupFor(frameIndex, slots.Face(c, f), white));
                }
            }
            // The instanced faces of every shadowed point light, in one pass attaching every layer, binding the frame
            // group that does not read the cubes it draws.
            if (!layered.empty())
            {
                graph.AddPass(
                    "PointShadows", RenderStage::Shadow, QueueKind::Graphics,
                    [=](PassBuilder& pass) {
                        pass.LayeredDepthAttachment(pointShadowCubes, LoadOp::Clear, 0.0f);
                        readSkinned(pass);
                    },
                    [this, draws, pointGroups, layeredRuns, unshadowedFrame, cubeGroups, face, drawGroup,
                     skinned](PassContext& context) {
                        for (const BindingGroupHandle cube : cubeGroups)
                        {
                            RecordDraws(context.Encoder(), *draws, *pointGroups, *layeredRuns, unshadowedFrame, cube,
                                        drawGroup, skinned, face, DrawPass::PointShadowLayered);
                        }
                    });
                m_stats.shadowDraws += static_cast<std::uint32_t>(layeredRuns->size() * pointCount);
            }
            // The rest a face at a time, a pass a face, clearing it unless the instanced pass cleared every layer.
            if (!faced.empty())
            {
                const LoadOp load = layered.empty() ? LoadOp::Clear : LoadOp::Load;
                for (std::uint32_t layer = 0; layer < pointCount * CubeFaceCount; ++layer)
                {
                    const BindingGroupHandle faceGroup = faceGroups[layer];
                    graph.AddPass(
                        "PointShadowFaces", RenderStage::Shadow, QueueKind::Graphics,
                        [=](PassBuilder& pass) {
                            pass.DepthAttachmentLayer(pointShadowCubes, layer, load, 0.0f);
                            readSkinned(pass);
                        },
                        [this, draws, pointGroups, facedRuns, unshadowedFrame, faceGroup, face, drawGroup,
                         skinned](PassContext& context) {
                            RecordDraws(context.Encoder(), *draws, *pointGroups, *facedRuns, unshadowedFrame, faceGroup,
                                        drawGroup, skinned, face, DrawPass::PointShadow);
                        });
                }
                m_stats.shadowDraws += static_cast<std::uint32_t>(facedRuns->size() * pointCount * CubeFaceCount);
            }
        }
        // The targets drawn into so far: the first view into each clears it. The window's key is empty.
        std::set<AssetKey> cleared;
        std::vector<BloomView> blooms;
        // The lens dirt images the views' bloom and flare composites read, each imported once.
        std::map<AssetKey, GraphResource> dirtImages;
        const auto lensDirt = [&](AssetKey key) {
            const auto image = m_images.find(key);
            if (key.IsEmpty() || image == m_images.end())
            {
                return GraphResource{};
            }
            if (const auto found = dirtImages.find(key); found != dirtImages.end())
            {
                return found->second;
            }
            const GraphResource resource =
                graph.ImportTexture(std::format("Lens dirt {}", key.index), image->second.texture, image->second.desc);
            dirtImages.emplace(key, resource);
            return resource;
        };
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            const ViewRequest& request = views[v];
            const bool intoImage = !request.target.IsEmpty();
            if (intoImage && !m_targets.contains(request.target))
            {
                continue;
            }
            const GraphResource target = intoImage ? imported(request.target) : frame.backbuffer;
            const PixelRect rect = request.viewport;
            const PixelRect viewport{0, 0, rect.width, rect.height};
            const std::size_t viewSlot = slots.View(v);

            // The sun's cascades for this camera, when it casts and something casts (ADR-0050).
            const SunShadowSettings& sun = lighting.shadows;
            const std::vector<Cascade> cascades =
                sun.enabled && !casters.empty()
                    ? FitCascades(request.view, request.projection, lighting.lightDirection, sun.distance, m_cascades)
                    : std::vector<Cascade>{};
            const auto cascadeCount = static_cast<std::uint32_t>(cascades.size());

            const Math::Mat4 viewProjection = request.projection * request.view;
            ViewBlock viewBlock{request.view,
                                request.projection,
                                viewProjection,
                                glm::inverse(viewProjection),
                                Math::Vec4(request.position, 1.0f),
                                Math::Vec4(static_cast<float>(rect.x), static_cast<float>(rect.y),
                                           static_cast<float>(rect.width), static_cast<float>(rect.height))};
            for (std::uint32_t c = 0; c < cascadeCount; ++c)
            {
                viewBlock.shadowMatrices.at(c) = AtlasMatrix(cascades[c], c, cascadeCount);
                viewBlock.shadowSplits[static_cast<int>(c)] = cascades[c].split;
                viewBlock.shadowTexels[static_cast<int>(c)] = cascades[c].texelSize;
            }
            viewBlock.shadowBias = Math::Vec4(sun.normalBias, sun.slopeBias, ShadowFilterRadius, CascadeBlendShare);
            viewBlock.shadowInfo = {cascadeCount, request.debugView == ViewDebug::ShadowCascades ? 1u : 0u, 0, 0};
            // Its lights listed in its clusters (ADR-0049), the slices running as far as any light reaches.
            ClusterDepths clusterDepths;
            if (m_clusteredLights && lightCount > 0)
            {
                std::vector<LightSphere> spheres;
                spheres.reserve(lightRecords.size());
                for (const GpuLight& light : lightRecords)
                {
                    spheres.push_back(LightSphereOf(light, request.view));
                }
                clusterDepths = ClusterDepthsOf(request.projection, spheres);
            }
            const std::pair<BufferHandle, GraphResource> clustered =
                AddLightClustersPass(graph, frameIndex, v, request, lightCount, clusterDepths);
            const BufferHandle clusterBuffer = clustered.first;
            const GraphResource clusters = clustered.second;
            viewBlock.clusterSlices = Math::Vec4(clusterDepths.near, ClusterSliceScale(clusterDepths), 0.0f, 0.0f);
            viewBlock.clusterInfo = {clusterBuffer.IsEmpty() ? 0u : 1u,
                                     request.debugView == ViewDebug::LightClusters ? 1u : 0u,
                                     std::min(lightCount, MaxClusterLights), 0};
            if (HasReflections(request.reflections))
            {
                viewBlock.reflectionInfo =
                    Math::Vec4(1.0f, request.reflections.intensity, request.reflections.roughnessCutoff,
                               static_cast<float>(MipCount(rect.width, rect.height) - 1));
            }
            m_viewBlocks->Write(frameIndex, viewSlot, BytesOf(viewBlock));
            // Each cascade's own view block, which the casting items' vertex stages place them by.
            std::vector<BindingGroupHandle> cascadeGroups;
            for (std::uint32_t c = 0; c < cascadeCount; ++c)
            {
                const Cascade& cascade = cascades[c];
                const std::size_t slot = slots.Cascade(v, c);
                const auto size = static_cast<float>(m_cascades.mapSize);
                const ViewBlock cascadeBlock{cascade.view,
                                             cascade.projection,
                                             cascade.viewProjection,
                                             glm::inverse(cascade.viewProjection),
                                             Math::Vec4(cascade.centre, 1.0f),
                                             Math::Vec4(static_cast<float>(c) * size, 0.0f, size, size)};
                m_viewBlocks->Write(frameIndex, slot, BytesOf(cascadeBlock));
                cascadeGroups.push_back(ViewGroupFor(frameIndex, slot, GetDefaultTexture(DefaultTexture::White)));
            }
            const BackgroundBlock backgroundBlock{
                Math::Vec4(request.background.r, request.background.g, request.background.b, request.background.a)};
            m_backgroundBlocks->Write(frameIndex, v, BytesOf(backgroundBlock));
            OutputBlock outputBlock;
            outputBlock.exposureScale = ExposureScale(request.exposure);
            outputBlock.tonemapper = static_cast<std::uint32_t>(request.tonemapper);
            if (!IsIdentity(request.colourGrading))
            {
                const ColourGrade grade = BakeColourGrade(request.colourGrading);
                outputBlock.grade = 1;
                for (int c = 0; c < 3; ++c)
                {
                    outputBlock.whiteBalance[c] = Math::Vec4(grade.whiteBalance[c], 0.0f);
                }
                outputBlock.shadowTint = Math::Vec4(grade.shadowTint, grade.contrast);
                outputBlock.highlightTint = Math::Vec4(grade.highlightTint, grade.split);
                outputBlock.slope = Math::Vec4(grade.slope, grade.saturation);
                outputBlock.offset = grade.offset;
                outputBlock.power = grade.power;
            }
            outputBlock.aberration = request.chromaticAberration.intensity;
            outputBlock.vignette = Math::Vec4(request.vignette.intensity, request.vignette.radius,
                                              request.vignette.smoothness, request.vignette.roundness);
            outputBlock.grain =
                Math::Vec3(request.filmGrain.intensity, request.filmGrain.response, request.filmGrain.size);
            outputBlock.frameIndex = static_cast<std::uint32_t>(frameIndex);
            // The reflection hit target's confidence is its blue; every other grey view's value its red.
            outputBlock.greyChannel = request.debugView == ViewDebug::ReflectionConfidence ? 2u : 0u;
            m_outputBlocks->Write(frameIndex, v, BytesOf(outputBlock));
            const BindingGroupHandle outputBlockGroup = m_outputBlocks->Group(frameIndex, v);
            // The white texture as its ambient occlusion, for every pass but Opaque.
            const BindingGroupHandle viewGroup =
                ViewGroupFor(frameIndex, viewSlot, GetDefaultTexture(DefaultTexture::White), {}, clusterBuffer);
            // The Sky cube texture's pipeline and group, when the view draws one that is prepared; else its colour's,
            // which reads no frame block.
            BindingGroupHandle backgroundGroup = m_backgroundBlocks->Group(frameIndex, v);
            PipelineHandle backgroundPipeline = m_backgroundPipeline;
            BindingGroupHandle backgroundFrame;
            if (const SkyCube sky = m_skyImages->Cube(request.sky); !sky.texture.IsEmpty() && !m_skyPipeline.IsEmpty())
            {
                backgroundGroup = SkyGroup(sky.texture, frameIndex);
                backgroundPipeline = m_skyPipeline;
                backgroundFrame = frameGroup;
            }

            // Each draw's material group in this view, with the images it may not sample replaced.
            auto groups = std::make_shared<std::vector<BindingGroupHandle>>();
            groups->reserve(draws->size());
            for (const ResolvedDraw& draw : *draws)
            {
                groups->push_back(draw.material == nullptr ? BindingGroupHandle{}
                                                           : GroupWithout(*draw.material, replaced[v]));
            }
            std::vector<GraphResource> samples;
            for (const AssetKey image : imageReads[v].reads)
            {
                samples.push_back(imported(image));
            }

            TextureDesc colourDesc;
            colourDesc.format = TextureFormat::Rgba16Float;
            colourDesc.width = rect.width;
            colourDesc.height = rect.height;
            colourDesc.samples = m_samples;
            colourDesc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            TextureDesc depthDesc = colourDesc;
            depthDesc.format = TextureFormat::Depth32Float;
            depthDesc.usage = TextureUsage::DepthAttachment;
            TextureDesc normalDesc = colourDesc;
            normalDesc.format = TextureFormat::Rg16Float;
            const GraphResource sceneColour = graph.CreateTexture("SceneColor", colourDesc);
            const GraphResource sceneDepth = graph.CreateTexture("SceneDepth", depthDesc);
            const GraphResource sceneNormals = graph.CreateTexture("SceneNormals", normalDesc);
            GraphResource shadowAtlas;
            if (cascadeCount > 0)
            {
                TextureDesc shadowDesc;
                shadowDesc.format = TextureFormat::Depth32Float;
                shadowDesc.width = m_cascades.mapSize * cascadeCount;
                shadowDesc.height = m_cascades.mapSize;
                shadowDesc.usage = TextureUsage::DepthAttachment | TextureUsage::Sampled;
                shadowAtlas = graph.CreateTexture("ShadowAtlas", shadowDesc);
            }

            // Opaque items in draw-list order and transparent ones back to front by the view-space depth of their sort
            // points, the farthest (most negative view z) first; either kind outside the view's frustum, by its world
            // bounds (a list's the box around every copy), is left out of the view (ADR-0057): the coarse cull by
            // entity, which the cull pass then refines by copy. Frozen, the frustum is the camera's when it froze.
            auto opaque = std::make_shared<std::vector<std::size_t>>();
            auto transparent = std::make_shared<std::vector<std::size_t>>();
            // Each draw's set 2 in the prepass: a Masked item's prepass group, else its material group.
            auto prepassGroups = std::make_shared<std::vector<BindingGroupHandle>>(*groups);
            std::vector<float> depth(draws->size(), 0.0f);
            const Math::Frustum frustum =
                CullFrustumOf(request, Math::Frustum::FromViewProjection(request.projection * request.view));
            std::uint32_t instances = 0;
            for (std::size_t i = 0; i < draws->size(); ++i)
            {
                const ResolvedDraw& draw = (*draws)[i];
                if (!frustum.Intersects(draw.worldBounds))
                {
                    continue;
                }
                if (draw.transparent)
                {
                    transparent->push_back(i);
                }
                else
                {
                    opaque->push_back(i);
                }
                instances += std::max<std::uint32_t>(draw.instances.count, 1);
                if (draw.prepassMasked)
                {
                    (*prepassGroups)[i] = PrepassGroup(*draw.material, replaced[v]);
                }
                depth[i] = (request.view * Math::Vec4(draw.sortPoint, 1.0f)).z;
            }
            std::stable_sort(transparent->begin(), transparent->end(),
                             [&](std::size_t a, std::size_t b) { return depth[a] < depth[b]; });
            // Matching opaque and masked items merge into instanced draws (ADR-0051); transparent ones stay single,
            // in their depth order. The opaque runs are the view's cull pass's candidates (ADR-0057): DepthPrepass and
            // Opaque draw each by its command in the view's command buffer, the prepass skipping the runs it has no
            // pipeline for, reading the view's indirection through the view's own draw set.
            auto opaqueRuns = std::make_shared<std::vector<DrawRun>>(RunsOf(*draws, *groups, *opaque, DrawPass::Main));
            const CullView cull =
                PrepareCull(graph, frameIndex, v, frustum, *draws, *opaqueRuns, cullTemplates, cullGroups);
            culls.push_back(cull);
            m_drawnViews.push_back({v, sceneColour, sceneDepth, sceneNormals, shadowAtlas, viewGroup, cull.commands,
                                    cull.indirection, cull.runs, cull.candidates});
            const BindingGroupHandle viewDrawGroup =
                cull.indirection.IsEmpty() ? drawGroup : DrawGroupFor(frameIndex, cull.indirection);
            const auto readCulled = [cull](PassBuilder& pass) {
                if (!cull.commands.IsEmpty())
                {
                    pass.IndirectRead(cull.commandsResource);
                    pass.StorageRead(cull.indirectionResource);
                }
            };

            if (!shadowAtlas.IsEmpty())
            {
                // Each casting item's set 2 in the shadow passes: a cut-out Masked item's prepass group, else its
                // material group, which its own vertex stage may read.
                auto shadowGroups = std::make_shared<std::vector<BindingGroupHandle>>(*groups);
                for (const std::size_t i : casters)
                {
                    if ((*draws)[i].shadowMasked)
                    {
                        (*shadowGroups)[i] = PrepassGroup(*(*draws)[i].material, replaced[v]);
                    }
                }
                auto shadowRuns =
                    std::make_shared<std::vector<DrawRun>>(RunsOf(*draws, *shadowGroups, casters, DrawPass::Shadow));
                const std::uint32_t mapSize = m_cascades.mapSize;
                // Every cascade into its tile of the atlas, in one pass.
                graph.AddPass(
                    "ShadowCascades", RenderStage::Shadow, QueueKind::Graphics,
                    [=](PassBuilder& pass) {
                        pass.DepthAttachment(shadowAtlas, LoadOp::Clear, 0.0f);
                        for (const GraphResource image : samples)
                        {
                            pass.Sample(image);
                        }
                        readSkinned(pass);
                    },
                    [this, draws, shadowGroups, shadowRuns, frameGroup, cascadeGroups, drawGroup, skinned,
                     mapSize](PassContext& context) {
                        for (std::size_t c = 0; c < cascadeGroups.size(); ++c)
                        {
                            const PixelRect tile{static_cast<std::int32_t>(c * mapSize), 0, mapSize, mapSize};
                            RecordDraws(context.Encoder(), *draws, *shadowGroups, *shadowRuns, frameGroup,
                                        cascadeGroups[c], drawGroup, skinned, tile, DrawPass::Shadow);
                        }
                    });
                m_stats.shadowDraws += static_cast<std::uint32_t>(shadowRuns->size() * cascadeGroups.size());
            }
            auto transparentRuns = std::make_shared<std::vector<DrawRun>>();
            for (const std::size_t i : *transparent)
            {
                transparentRuns->push_back({i, 1});
            }

            // Depth and normals first (ADR-0049), so Opaque shades each pixel once, by equal depth.
            graph.AddPass(
                "DepthPrepass", RenderStage::DepthPrepass, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneNormals, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
                    pass.DepthAttachment(sceneDepth, LoadOp::Clear, 0.0f);
                    for (const GraphResource image : samples)
                    {
                        pass.Sample(image);
                    }
                    readSkinned(pass);
                    readCulled(pass);
                },
                [this, draws, prepassGroups, opaqueRuns, frameGroup, viewGroup, viewDrawGroup, skinned, viewport,
                 commands = cull.commands](PassContext& context) {
                    RecordDraws(context.Encoder(), *draws, *prepassGroups, *opaqueRuns, frameGroup, viewGroup,
                                viewDrawGroup, skinned, viewport, DrawPass::Prepass, commands);
                });
            // GTAO from the prepass's depth and normals, when the camera has it; Opaque samples what it makes.
            const GraphResource occlusion = m_gtao->AddPasses(
                graph, frameIndex,
                {sceneDepth, sceneNormals, {rect.width, rect.height}, request.projection, request.ambientOcclusion});
            // Its reflections' Hi-Z pyramid of the prepass's depth, after GTAO's passes, when the camera has them.
            const HistoryView history =
                m_sceneHistory->Import(graph, request, {rect.width, rect.height}, viewProjection);
            m_sceneHistory->AddHiZPasses(graph, frameIndex, history, sceneDepth);
            // Its mirror rays marched through that pyramid; Opaque samples the hits, and last frame's colour by them.
            const GraphResource reflectionHits = m_reflectionTrace->AddPass(
                graph, frameIndex, {sceneNormals, history, request.projection, request.view, request.reflections});
            const GraphResource sceneHistory = reflectionHits.IsEmpty() ? GraphResource{} : history.history;
            graph.AddPass(
                "Opaque", RenderStage::Opaque, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
                    // Loaded, and written only by items with no prepass draw.
                    pass.DepthAttachment(sceneDepth, LoadOp::Load);
                    for (const GraphResource image : samples)
                    {
                        pass.Sample(image);
                    }
                    for (const GraphResource read :
                         {occlusion, shadowAtlas, spotAtlas, pointShadowCubes, reflectionHits})
                    {
                        if (!read.IsEmpty())
                        {
                            pass.Sample(read);
                        }
                    }
                    // Last frame's colour, which this frame's SceneHistory passes then replace.
                    if (!sceneHistory.IsEmpty())
                    {
                        pass.SamplePrevious(sceneHistory);
                    }
                    if (!clusters.IsEmpty())
                    {
                        pass.StorageRead(clusters);
                    }
                    readSkinned(pass);
                    readCulled(pass);
                },
                [this, draws, groups, opaqueRuns, frameGroup, viewDrawGroup, skinned, viewport, occlusion, shadowAtlas,
                 clusterBuffer, viewSlot, reflectionHits, sceneHistory,
                 commands = cull.commands](PassContext& context) {
                    const auto textureOf = [&](GraphResource resource) {
                        return resource.IsEmpty() ? TextureHandle{} : context.Texture(resource);
                    };
                    const TextureHandle occlusionTexture =
                        occlusion.IsEmpty() ? GetDefaultTexture(DefaultTexture::White) : context.Texture(occlusion);
                    RecordDraws(context.Encoder(), *draws, *groups, *opaqueRuns, frameGroup,
                                ViewGroupFor(context.FrameIndex(), viewSlot, occlusionTexture, textureOf(shadowAtlas),
                                             clusterBuffer, textureOf(reflectionHits), textureOf(sceneHistory)),
                                viewDrawGroup, skinned, viewport, DrawPass::Main, commands);
                });
            if (!transparent->empty())
            {
                // Depth tested against the opaque items, without writing it (ADR-0024).
                graph.AddPass(
                    "Transparent", RenderStage::Transparent, QueueKind::Graphics,
                    [=](PassBuilder& pass) {
                        pass.ColourAttachment(sceneColour, LoadOp::Load);
                        pass.DepthReadOnly(sceneDepth);
                        for (const GraphResource image : samples)
                        {
                            pass.Sample(image);
                        }
                        if (!shadowAtlas.IsEmpty())
                        {
                            pass.Sample(shadowAtlas);
                        }
                        if (!spotAtlas.IsEmpty())
                        {
                            pass.Sample(spotAtlas);
                        }
                        if (!pointShadowCubes.IsEmpty())
                        {
                            pass.Sample(pointShadowCubes);
                        }
                        if (!clusters.IsEmpty())
                        {
                            pass.StorageRead(clusters);
                        }
                        readSkinned(pass);
                    },
                    [this, draws, groups, transparentRuns, frameGroup, viewGroup, drawGroup, skinned, viewport,
                     shadowAtlas, clusterBuffer, viewSlot](PassContext& context) {
                        // Shadowed, but never darkened by the occlusion of what is behind it.
                        const BindingGroupHandle shadowedView =
                            shadowAtlas.IsEmpty()
                                ? viewGroup
                                : ViewGroupFor(context.FrameIndex(), viewSlot, GetDefaultTexture(DefaultTexture::White),
                                               context.Texture(shadowAtlas), clusterBuffer);
                        RecordDraws(context.Encoder(), *draws, *groups, *transparentRuns, frameGroup, shadowedView,
                                    drawGroup, skinned, viewport, DrawPass::Main);
                    });
            }
            m_stats.draws += static_cast<std::uint32_t>(opaqueRuns->size() + transparentRuns->size());
            m_stats.instances += instances;
            m_stats.skinnedDraws += skinnedDraws;

            graph.AddPass(
                "Background", RenderStage::Background, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Load);
                    pass.DepthReadOnly(sceneDepth);
                },
                [backgroundPipeline, backgroundGroup, backgroundFrame, viewGroup, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(backgroundPipeline);
                    if (!backgroundFrame.IsEmpty())
                    {
                        encoder.SetBindingGroup(0, backgroundFrame);
                    }
                    encoder.SetBindingGroup(1, viewGroup);
                    encoder.SetBindingGroup(MaterialSet, backgroundGroup);
                    encoder.Draw({.vertexCount = 3});
                });

            // SceneColor kept for next frame's reflections before bloom glows over it: these passes are added before
            // any view's bloom, so they run first in PostProcess.
            m_sceneHistory->AddHistoryPasses(graph, frameIndex, history, sceneColour);
            blooms.push_back({sceneColour,
                              {rect.width, rect.height},
                              request.bloom,
                              request.anamorphicStreaks,
                              request.lensFlare,
                              lensDirt(request.lensFlare.dirt)});

            // The first view into a target clears all of it; each later one draws over it, in its viewport only.
            const LoadOp load = cleared.insert(request.target).second ? LoadOp::Clear : LoadOp::Load;
            // A view showing its ambient occlusion, its shadow atlas, the spot shadow atlas or its reflections'
            // confidence draws that instead, as grey, black when it has none.
            const bool showGrey =
                (request.debugView == ViewDebug::AmbientOcclusion || request.debugView == ViewDebug::ShadowAtlas ||
                 request.debugView == ViewDebug::SpotShadowAtlas ||
                 request.debugView == ViewDebug::ReflectionConfidence) &&
                !m_occlusionOutputPipeline.IsEmpty();
            const GraphResource grey = request.debugView == ViewDebug::ShadowAtlas            ? shadowAtlas
                                       : request.debugView == ViewDebug::SpotShadowAtlas      ? spotAtlas
                                       : request.debugView == ViewDebug::ReflectionConfidence ? reflectionHits
                                                                                              : occlusion;
            // A view showing its reflections draws last frame's history at its hits, the colour Opaque blended, so
            // it reads the history before this frame's SceneHistory passes replace it; black without reflections.
            const bool showReflections =
                request.debugView == ViewDebug::Reflections && !m_reflectionOutputPipeline.IsEmpty();
            const bool shownBlack = (showGrey && grey.IsEmpty()) || (showReflections && reflectionHits.IsEmpty());
            const GraphResource shown = shownBlack        ? GraphResource{}
                                        : showGrey        ? grey
                                        : showReflections ? reflectionHits
                                                          : sceneColour;
            const GraphResource shownHistory = showReflections && !shownBlack ? sceneHistory : GraphResource{};
            // Black is the 1 x 1 black texture, drawn as grey.
            // Chromatic aberration's variant only when it moves something and its pipeline was made.
            const bool aberrated =
                request.chromaticAberration.intensity > 0.0f && !m_aberrationOutputPipeline.IsEmpty();
            const PipelineHandle outputPipeline = showGrey || shownBlack ? m_occlusionOutputPipeline
                                                  : showReflections      ? m_reflectionOutputPipeline
                                                  : aberrated            ? m_aberrationOutputPipeline
                                                                         : m_outputPipeline;
            const TextureHandle black = GetDefaultTexture(DefaultTexture::Black);
            graph.AddPass(
                "Output", RenderStage::Output, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    if (!shown.IsEmpty())
                    {
                        pass.Sample(shown);
                    }
                    if (!shownHistory.IsEmpty())
                    {
                        pass.SamplePrevious(shownHistory);
                    }
                    pass.ColourAttachment(target, load, {0.0f, 0.0f, 0.0f, 1.0f});
                },
                [this, shown, shownHistory, black, outputPipeline, rect, outputBlockGroup](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(rect);
                    encoder.SetPipeline(outputPipeline);
                    encoder.SetBindingGroup(1, outputBlockGroup);
                    const TextureHandle source = shown.IsEmpty() ? black : context.Texture(shown);
                    encoder.SetBindingGroup(
                        MaterialSet,
                        shownHistory.IsEmpty()
                            ? OutputGroup(source, context.FrameIndex())
                            : ReflectionOutputGroup(source, context.Texture(shownHistory), context.FrameIndex()));
                    encoder.Draw({.vertexCount = 3});
                });
        }
        AddCullPasses(graph, frameIndex, culls, cullTemplates, cullGroups);
        AddCullReadback(graph, frameIndex, culls, cullTemplates.size() * sizeof(DrawIndexedIndirectCommand));
        m_bloom->AddPasses(graph, frameIndex, blooms);
        m_sceneHistory->EndFrame();
        if (!cleared.contains(AssetKey{}))
        {
            AddWindowClear(graph, frame);
        }
    }

    Math::Mat4 ReversedZOrthographic(float height, float aspect, float near, float far)
    {
        Math::Mat4 projection(1.0f);
        projection[0][0] = 2.0f / (height * aspect);
        projection[1][1] = 2.0f / height;
        // Depth is (far + z) / (far - near) for view-space z = -distance: 1 at the near plane, 0 at the far one.
        projection[2][2] = 1.0f / (far - near);
        projection[3][2] = far / (far - near);
        return projection;
    }

    PixelRect ViewportPixels(float x, float y, float width, float height, PixelSize size)
    {
        const auto edge = [](float fraction, std::uint32_t pixels) {
            const float at = std::round(fraction * static_cast<float>(pixels));
            return static_cast<std::int64_t>(std::clamp(at, 0.0f, static_cast<float>(pixels)));
        };
        const std::int64_t left = edge(x, size.width);
        const std::int64_t right = edge(x + width, size.width);
        const std::int64_t top = edge(y, size.height);
        const std::int64_t bottom = edge(y + height, size.height);
        if (right <= left || bottom <= top)
        {
            return {};
        }
        return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
    }

    Math::AABB CopiesBounds(std::span<const Math::Mat4> copies, const Math::AABB& box)
    {
        std::optional<Math::AABB> bounds;
        for (const Math::Mat4& copy : copies)
        {
            const Math::AABB moved = Math::Moved(box, copy);
            bounds = bounds ? Math::Union(*bounds, moved) : moved;
        }
        return bounds.value_or(Math::AABB{Math::Vec3(0.0f), Math::Vec3(0.0f)});
    }

    Math::Mat4 ReversedZPerspective(float verticalFov, float aspect, float near)
    {
        const float focal = 1.0f / std::tan(verticalFov * 0.5f);
        Math::Mat4 projection(0.0f);
        projection[0][0] = focal / aspect;
        projection[1][1] = focal;
        // Depth is near / -z: 1 at the near plane, falling to 0 at infinity.
        projection[3][2] = near;
        projection[2][3] = -1.0f;
        return projection;
    }
} // namespace Fenrir::Render
